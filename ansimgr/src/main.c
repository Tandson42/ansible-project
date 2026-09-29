#include "app.h"
#include "tui/buf.h"
#include "tui/input.h"
#include "tui/term.h"
#include "tui/theme.h"
#include "proc/job.h"
#include "edit/undo.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * Single-threaded event loop. stdin and the running job's pty master are
 * polled together, so the interface keeps redrawing while ansible streams its
 * output and keystrokes are never delayed by it.
 */

static volatile sig_atomic_t g_quit = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_quit = 1;
}

static void handle_global(App *a, Key k)
{
    switch (k.type) {
    case KEY_TAB:
        a->active = (a->active + 1) % V_COUNT;
        return;
    case KEY_BACKTAB:
        a->active = (a->active + V_COUNT - 1) % V_COUNT;
        return;
    case KEY_F1:
        a->active = V_HELP;
        return;
    case KEY_CHAR:
        switch (k.ch[0]) {
        case 'q':
            if (run_is_busy(a))
                app_status(a, 2, "ha um comando em execucao \u2014 Ctrl-C para cancelar");
            else
                a->quit = 1;
            return;
        case '1': a->active = V_DASHBOARD; return;
        case '2': a->active = V_HOSTS; return;
        case '3': a->active = V_ROLES; return;
        case '4': a->active = V_RUN; return;
        case '5': a->active = V_VM; return;
        case '6': a->active = V_LINT; return;
        case '7': a->active = V_HELP; return;
        case 'z':
            if (undo_count() > 0 && undo_apply(0) == 0) {
                project_reload_roles(&a->proj);
                inv_load(&a->inv, a->proj.inventory);
                app_status(a, 1, "desfeito: %s", project_basename(undo_at(0)->path));
            } else {
                app_status(a, 3, "nada para desfazer");
            }
            return;
        case 'R':
            project_reload_roles(&a->proj);
            inv_load(&a->inv, a->proj.inventory);
            app_status(a, 1, "projeto recarregado");
            return;
        default:
            return;
        }
    default:
        return;
    }
}

int main(int argc, char **argv)
{
    App app;
    memset(&app, 0, sizeof(app));
    log_init(&app.log);

    const char *start = (argc > 1) ? argv[1] : ".";
    if (project_discover(&app.proj, start) != 0) {
        fprintf(stderr, "ansimgr: nenhum ansible.cfg encontrado a partir de '%s'\n", start);
        fprintf(stderr, "usage: ansimgr [diretorio-do-projeto]\n");
        return 1;
    }
    undo_set_root(app.proj.root);

    if (inv_load(&app.inv, app.proj.inventory) != 0)
        app_status(&app, 2, "inventario nao encontrado: %s", app.proj.inventory);
    else
        app_status(&app, 0, "%s \u00b7 %d host(s) \u00b7 %d role(s) em %s",
                   app.proj.root, app.inv.n_ents, app.proj.n_roles, app.proj.playbook);

    vmstat_probe(&app.vm, app.proj.disk, app.proj.inventory);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    term_init();
    input_init(STDIN_FILENO);

    Buf buf;
    if (buf_init(&buf, term_rows(), term_cols()) != 0) {
        term_shutdown();
        fprintf(stderr, "ansimgr: out of memory\n");
        return 1;
    }

    long last_tick = 0;
    int resized = 1;

    while (!app.quit && !g_quit) {
        if (resized || term_resized()) {
            term_refresh_size();
            buf_resize(&buf, term_rows(), term_cols());
            term_clear_screen();
            resized = 0;
        }

        buf_clear(&buf, FG_DEFAULT, BG_DEFAULT);
        view_draw(&app, &buf);
        buf_render(&buf);

        struct pollfd fds[2];
        int nfds = 0;
        fds[nfds].fd = STDIN_FILENO;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;

        int job_idx = -1;
        if (run_is_busy(&app)) {
            int fd = job_fd(app.job);
            if (fd >= 0) {
                fds[nfds].fd = fd;
                fds[nfds].events = POLLIN;
                fds[nfds].revents = 0;
                job_idx = nfds;
                nfds++;
            }
        }

        int r = poll(fds, (nfds_t)nfds, 250);
        if (r < 0 && errno != EINTR) break;

        if (g_quit) break;

        if (r > 0 && (fds[0].revents & (POLLIN | POLLHUP))) {
            /* Drain everything available, then dispatch each key. */
            for (;;) {
                Key k = input_next(0);
                if (k.type == KEY_NONE) break;
                /* Esc only means "cancel" inside a modal; elsewhere it is idle. */
                if (k.type == KEY_ESC && !app.confirm.active && !app.prompt.active)
                    continue;
                handle_global(&app, k);
                if (app.quit || g_quit) break;
                view_key(&app, k);
                if (app.quit || g_quit) break;
            }
        }

        if (job_idx >= 0 && (fds[job_idx].revents & (POLLIN | POLLHUP | POLLERR)))
            job_drain(app.job);

        if (app.job) {
            job_poll(app.job);
            if (!job_running(app.job)) {
                /* Reap and release; run_on_exit already fired. */
                job_free(app.job);
                app.job = NULL;
                app.job_kind = RUN_KIND_NONE;
                /* Re-probe: a run may have changed the VM's state. */
                vmstat_probe(&app.vm, app.proj.disk, app.proj.inventory);
            }
        }

        long now = (long)time(NULL);
        if (now - last_tick >= 2) {
            last_tick = now;
            view_tick(&app);
        }
    }

    if (app.job) {
        job_cancel(app.job);
        job_drain(app.job);
        job_poll(app.job);
        job_free(app.job);
    }
    log_free(&app.log);
    buf_free(&buf);
    term_shutdown();
    return 0;
}
