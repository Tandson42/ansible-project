#include "app.h"
#include "proc/ansi.h"
#include "proc/job.h"
#include "tui/term.h"
#include "tui/theme.h"
#include "edit/fsio.h"
#include "edit/text.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * A single executor serves the Playbook and Lint views. Everything runs in a
 * pty so ansible emits its normal coloured output, and lines are pushed into
 * the log as they arrive rather than after the process exits.
 */

static char g_clean[4096];
static char g_accum[262144]; /* rolling window used for the PLAY RECAP scan */
static size_t g_accum_len = 0;

int run_is_busy(const App *a)
{
    return a->job != NULL;
}

static void accum_feed(const char *s)
{
    size_t n = strlen(s);
    if (n + 2 > sizeof(g_accum)) return;
    if (g_accum_len + n + 2 > sizeof(g_accum)) {
        /* Keep the tail: the recap lands at the end of a run. */
        size_t drop = n / 2 + 1;
        if (drop > g_accum_len) drop = g_accum_len;
        memmove(g_accum, g_accum + drop, g_accum_len - drop);
        g_accum_len -= drop;
    }
    memcpy(g_accum + g_accum_len, s, n);
    g_accum_len += n;
    g_accum[g_accum_len++] = '\n';
    g_accum[g_accum_len] = '\0';
}

void run_on_line(void *ud, const char *line, size_t len)
{
    App *a = (App *)ud;
    (void)len;

    ansi_strip(line, strlen(line), g_clean, sizeof(g_clean));
    accum_feed(g_clean);

    /* Blank lines carry no information and make scrolling harder to read. */
    if (g_clean[0] == '\0') return;
    log_push(&a->log, g_clean);
}

static void record_history(App *a, const Recap *r, int status)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.state", a->proj.root);
    fs_mkdir_p(dir);

    char line[1024];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    if (r && r->valid)
        snprintf(line, sizeof(line),
                 "%s  exit=%d  %-34s ok=%d changed=%d unreachable=%d failed=%d  [%s]\n",
                 ts, status, a->job_title, r->ok, r->changed, r->unreachable, r->failed,
                 recap_verdict(r));
    else
        snprintf(line, sizeof(line), "%s  exit=%d  %-34s [sem recap]\n", ts, status,
                 a->job_title);

    char path[600];
    snprintf(path, sizeof(path), "%s/history", dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    ssize_t n = write(fd, line, strlen(line));
    (void)n;
    close(fd);
}

void run_on_exit(void *ud, int status, int term_status)
{
    App *a = (App *)ud;
    (void)term_status;

    Recap r;
    recap_parse(g_accum, g_accum_len, &r);

    if ((a->job_kind == RUN_KIND_PLAY || a->job_kind == RUN_KIND_CHECK) && r.valid) {
        a->prev = a->last;
        a->last = r;
    }

    record_history(a, r.valid ? &r : NULL, status);

    int kind;
    if (status != 0 || (r.valid && (r.failed > 0 || r.unreachable > 0))) kind = 3;
    else if (r.valid && r.changed > 0) kind = 2;
    else if (r.valid) kind = 1;
    else kind = 0;

    if (r.valid) {
        app_status(a, kind, "%s \u00b7 exit=%d \u00b7 ok=%d changed=%d unreachable=%d failed=%d",
                   recap_verdict(&r), status, r.ok, r.changed, r.unreachable, r.failed);
    } else {
        app_status(a, kind == 3 ? 3 : 0, "%s \u00b7 exit=%d",
                   r.valid ? recap_verdict(&r) : "sem PLAY RECAP", status);
    }
    a->run_seq++;
}

int run_exec(App *a, int kind, const char *title, char *const argv[])
{
    if (run_is_busy(a)) {
        app_status(a, 2, "ja existe um comando em execucao");
        return -1;
    }

    log_clear(&a->log);
    g_accum_len = 0;
    g_accum[0] = '\0';

    snprintf(a->job_title, sizeof(a->job_title), "%s", title ? title : "execucao");
    a->job_kind = kind;
    a->job_started = (long)time(NULL);

    /* Remember how to re-issue this command. */
    {
        int k = 0;
        for (; argv[k] && k < 16; k++)
            snprintf(a->last_argv[k], sizeof(a->last_argv[k]), "%s", argv[k]);
        a->last_n = k;
    }
    a->last_kind = kind;
    snprintf(a->last_title, sizeof(a->last_title), "%s", title ? title : "");

    a->job = job_start(argv, a->proj.root, term_cols(), term_rows(),
                       run_on_line, run_on_exit, a);
    if (!a->job) {
        app_status(a, 3, "falha ao executar %s", argv[0]);
        a->job = NULL;
        return -1;
    }
    job_set_title(a->job, title);
    app_status(a, 0, "executando %s \u2026", title ? title : argv[0]);
    return 0;
}

void run_cancel(App *a)
{
    if (!run_is_busy(a)) {
        app_status(a, 0, "nada em execucao");
        return;
    }
    job_cancel(a->job);
    app_status(a, 2, "cancelando \u2026");
}

void run_repeat(App *a)
{
    if (a->last_n == 0) {
        app_status(a, 0, "nada para repetir");
        return;
    }
    if (run_is_busy(a)) {
        app_status(a, 2, "ja existe um comando em execucao");
        return;
    }
    char *argv[16];
    for (int i = 0; i < a->last_n; i++) argv[i] = a->last_argv[i];
    argv[a->last_n] = NULL;
    /* log_clear happens inside run_exec, so remember the log first? no. */
    run_exec(a, a->last_kind, a->last_title, argv);
}

/* ---- command construction --------------------------------------------- */

void run_build_playbook_argv(App *a, char *argv[16])
{
    int i = 0;
    argv[i++] = (char *)"ansible-playbook";
    argv[i++] = (char *)project_basename(a->proj.playbook);
    if (a->opt_check) argv[i++] = (char *)"--check";
    if (a->opt_diff) argv[i++] = (char *)"--diff";
    if (a->limit[0]) {
        static char limitbuf[160];
        snprintf(limitbuf, sizeof(limitbuf), "--limit=%s", a->limit);
        argv[i++] = limitbuf;
    }
    argv[i] = NULL;
}

void run_playbook(App *a)
{
    char *argv[16];
    run_build_playbook_argv(a, argv);

    char title[128];
    if (a->opt_check)
        snprintf(title, sizeof(title), "%s --check", project_basename(a->proj.playbook));
    else if (a->limit[0])
        snprintf(title, sizeof(title), "%s --limit %s", project_basename(a->proj.playbook),
                 a->limit);
    else
        snprintf(title, sizeof(title), "%s", project_basename(a->proj.playbook));
    run_exec(a, RUN_KIND_PLAY, title, argv);
}

/* ---- scrolling -------------------------------------------------------- */

void run_scroll(App *a, int delta)
{
    a->log.offset_from_bottom += delta;
    if (a->log.offset_from_bottom < 0) a->log.offset_from_bottom = 0;
    if (a->log.offset_from_bottom > a->log.n) a->log.offset_from_bottom = a->log.n;
    a->log.follow = (a->log.offset_from_bottom == 0);
}

void run_toggle_follow(App *a)
{
    if (a->log.offset_from_bottom != 0) {
        a->log.offset_from_bottom = 0;
        a->log.follow = 1;
    } else {
        a->log.follow = 0;
        a->log.offset_from_bottom = a->log.n > 0 ? a->log.n : 1;
    }
}

/* ---- view ------------------------------------------------------------- */

void run_draw(App *a, Buf *b, int x, int y, int w, int h)
{
    /* Options strip. */
    int oy = y;
    buf_fill(b, oy, x, 1, w, COL_DIM, BG_DEFAULT, A_NONE);
    int cx = x + 1;
    struct { const char *label; int on; char key; } opts[] = {
        { "--check", a->opt_check, 'c' },
        { "--diff",  a->opt_diff,  'f' },
    };
    for (size_t i = 0; i < sizeof(opts) / sizeof(opts[0]); i++) {
        char t[32];
        snprintf(t, sizeof(t), " %c %s ", opts[i].key, opts[i].label);
        int tw = str_width(t);
        if (cx + tw >= x + w) break;
        if (opts[i].on)
            buf_fill(b, oy, cx, 1, tw, 235, COL_ACCENT, A_BOLD);
        buf_puts(b, oy, cx, opts[i].on ? 231 : COL_DIM, BG_DEFAULT,
                 opts[i].on ? A_BOLD : A_NONE, t);
        cx += tw + 1;
    }
    if (a->limit[0]) {
        char t[64];
        snprintf(t, sizeof(t), " --limit %s ", a->limit);
        int tw = str_width(t);
        if (cx + tw < x + w) buf_puts(b, oy, cx, COL_ACCENT, BG_DEFAULT, A_BOLD, t);
    }
    if (run_is_busy(a)) {
        int rx = x + w - 26;
        if (rx > cx) draw_badge(b, oy, rx, "em execucao", 235, COL_WARN);
    }

    /* Log area. */
    int ly = y + 1;
    int lh = h - 1;
    if (lh < 1) lh = 1;

    buf_box(b, ly, x, lh, w, COL_FRAME, NULL);
    if (log_line_count(&a->log) == 0) {
        buf_puts(b, ly + lh / 2, x + 3, COL_DIM, BG_DEFAULT, A_NONE,
                 "nenhuma execucao ainda \u2014 pressione Enter para rodar o playbook");
        return;
    }
    draw_logview(b, &a->log, ly + 1, x + 1, lh - 2, w - 2);

    /* Position indicator. */
    char pos[64];
    snprintf(pos, sizeof(pos), "linha %d/%d", log_line_count(&a->log) - a->log.offset_from_bottom,
             log_line_count(&a->log));
    buf_printf(b, ly + lh - 1, x + w - 2 - str_width(pos), str_width(pos), COL_DIM,
               BG_DEFAULT, A_NONE, "%s", pos);
}

void run_key(App *a, Key k)
{
    if (run_is_busy(a)) {
        switch (k.type) {
        case KEY_PGUP: run_scroll(a, -15); return;
        case KEY_PGDN: run_scroll(a, 15); return;
        case KEY_UP:   run_scroll(a, -1); return;
        case KEY_DOWN: run_scroll(a, 1); return;
        case KEY_HOME: a->log.offset_from_bottom = a->log.n; a->log.follow = 0; return;
        case KEY_END:  run_toggle_follow(a); return;
        case KEY_CHAR:
            if (k.ch[0] == 'c') { run_cancel(a); return; }
            break;
        case KEY_F10: run_cancel(a); return;
        default: break;
        }
        return;
    }

    switch (k.type) {
    case KEY_ENTER: run_playbook(a); return;
    case KEY_F10:   run_repeat(a); return;
    case KEY_F5:    run_repeat(a); return;
    case KEY_PGUP:  run_scroll(a, -15); return;
    case KEY_PGDN:  run_scroll(a, 15); return;
    case KEY_HOME:  a->log.offset_from_bottom = a->log.n; a->log.follow = 0; return;
    case KEY_END:   run_toggle_follow(a); return;
    case KEY_CHAR:
        switch (k.ch[0]) {
        case 'c': a->opt_check = !a->opt_check;
                  app_status(a, 0, "--check %s", a->opt_check ? "ativado" : "desativado");
                  return;
        case 'f': a->opt_diff = !a->opt_diff;
                  app_status(a, 0, "--diff %s", a->opt_diff ? "ativado" : "desativado");
                  return;
        case 'l':
            a->prompt_action = PA_LIMIT;
            prompt_begin(&a->prompt, " --limit ", "host ou grupo; Esc cancela", a->limit);
            return;
        case 'g': {
            const char *grp = a->inv.n_groups ? a->inv.groups[0].name : "";
            a->prompt_action = PA_LIMIT;
            prompt_begin(&a->prompt, " --limit ", "grupo do inventario", grp);
            return;
        }
        case 'C': run_cancel(a); return;
        case 'r': run_repeat(a); return;
        default: return;
        }
    default: return;
    }
}
