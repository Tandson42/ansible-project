#include "app.h"
#include "tui/theme.h"
#include "edit/undo.h"

#include <stdio.h>
#include <string.h>

/*
 * The dashboard is read-only: it answers "what is this project, is the VM up,
 * and what changed last time" without any risk of editing anything.
 */

typedef struct {
    const char *label;
    const char *value;
    uint8_t col;
} Row;

void dashboard_draw(App *a, Buf *b, int x, int y, int w, int h)
{
    int col_w = (w >= 92) ? (w - 6) / 2 : w;
    int right_x = x + col_w + 4;
    int right_w = w - col_w - 4;

    /*
     * The undo strip is pinned to the bottom, so both columns get a hard row
     * budget above it. Without this the left column overruns the strip on a
     * short terminal and the two texts print on top of each other.
     */
    int by = (h > 4) ? y + h - 3 : -1;
#define ROW_OK(r) (by < 0 || (r) < by)

    /* ---- left column: project ---- */
    int yy = y;
    if (ROW_OK(yy)) buf_puts(b, yy++, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "PROJETO");
    yy++;

    Row rows[8];
    int n = 0;
    char v1[256], v2[256], v3[256], v4[256], v5[256], v6[256];

    snprintf(v1, sizeof(v1), "%s", a->proj.root);
    rows[n].label = "raiz";      rows[n].value = v1;      rows[n].col = COL_TEXT; n++;
    snprintf(v2, sizeof(v2), "%s", a->proj.ansible_cfg);
    rows[n].label = "ansible.cfg"; rows[n].value = v2;   rows[n].col = COL_TEXT; n++;
    snprintf(v3, sizeof(v3), "%s", a->proj.playbook);
    rows[n].label = "playbook"; rows[n].value = v3;      rows[n].col = COL_TEXT; n++;
    snprintf(v4, sizeof(v4), "%s", a->proj.inventory);
    rows[n].label = "inventory"; rows[n].value = v4;     rows[n].col = COL_TEXT; n++;
    snprintf(v5, sizeof(v5), "%s", a->proj.roles_path);
    rows[n].label = "roles_path"; rows[n].value = v5;    rows[n].col = COL_TEXT; n++;
    if (a->proj.disk[0]) {
        snprintf(v6, sizeof(v6), "%s", a->proj.disk);
        rows[n].label = "disco"; rows[n].value = v6;     rows[n].col = COL_TEXT; n++;
    }

    for (int i = 0; i < n; i++) {
        if (!ROW_OK(yy)) break;
        int avail = col_w - 14;
        if (avail < 10) avail = 10;
        buf_printf(b, yy, x, 14, COL_DIM, BG_DEFAULT, A_NONE, "%s", rows[i].label);
        buf_printf(b, yy, x + 14, avail, rows[i].col, BG_DEFAULT, A_NONE, "%s",
                   rows[i].value);
        yy++;
    }

    /* ESCALA is a blank line, a heading, a blank line and three values. */
    if (by < 0 || yy + 6 <= by) {
        yy++;
        buf_puts(b, yy++, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "ESCALA");
        yy++;
        int tasks = yml_count_tasks(&a->proj);
        int n_active = 0;
        for (int i = 0; i < a->proj.n_roles; i++) if (a->proj.roles[i].active) n_active++;

        char scale[64];
        snprintf(scale, sizeof(scale), "%d roles  (%d ativas no playbook)", a->proj.n_roles,
                 n_active);
        buf_printf(b, yy++, x, col_w, COL_TEXT, BG_DEFAULT, A_NONE, "%s", scale);
        snprintf(scale, sizeof(scale), "%d hosts em %d grupo(s)", a->inv.n_ents, a->inv.n_groups);
        buf_printf(b, yy++, x, col_w, COL_TEXT, BG_DEFAULT, A_NONE, "%s", scale);
        snprintf(scale, sizeof(scale), "%d tarefas declaradas no playbook", tasks);
        buf_printf(b, yy++, x, col_w, COL_TEXT, BG_DEFAULT, A_NONE, "%s", scale);
    }

    /* ---- left column: idempotency ---- */
    /* Same shape: blank line, heading, blank line, then the values. */
    int idem_vals = a->last.valid ? (a->prev.valid ? 3 : 2) : 1;
    if (by < 0 || yy + 3 + idem_vals <= by) {
        yy++;
        buf_puts(b, yy++, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "IDEMPOTENCIA");
        yy++;
        if (!a->last.valid) {
            buf_printf(b, yy++, x, col_w, COL_DIM, BG_DEFAULT, A_NONE,
                       "nenhuma execucao registrada nesta sessao");
        } else {
            const char *v = recap_verdict(&a->last);
            uint8_t col = COL_OK;
            if (a->last.failed > 0 || a->last.unreachable > 0) col = COL_ERR;
            else if (a->last.changed > 0) col = COL_WARN;
            buf_printf(b, yy++, x, col_w, col, BG_DEFAULT, A_BOLD, "%s", v);
            char line[160];
            snprintf(line, sizeof(line),
                     "%s  ok=%d  changed=%d  unreachable=%d  failed=%d  skipped=%d",
                     a->last.host[0] ? a->last.host : "-", a->last.ok, a->last.changed,
                     a->last.unreachable, a->last.failed, a->last.skipped);
            buf_printf(b, yy++, x, col_w, COL_TEXT, BG_DEFAULT, A_NONE, "%s", line);
            if (a->prev.valid) {
                int drift = a->last.changed - a->prev.changed;
                snprintf(line, sizeof(line), "anterior: changed=%d  (delta %+d)",
                         a->prev.changed, drift);
                buf_printf(b, yy++, x, col_w, COL_DIM, BG_DEFAULT, A_NONE, "%s", line);
            }
        }
    }

    /* ---- right column: VM ---- */
    if (right_w > 24) {
        int ry = y;
#define ROK(r) (by < 0 || (r) < by)
        if (ROK(ry)) buf_puts(b, ry++, right_x, COL_ACCENT, BG_DEFAULT, A_BOLD, "MAQUINA VIRTUAL");
        ry++;

        const char *state = vmstat_summary(&a->vm);
        uint8_t sc = COL_OK;
        if (strcmp(state, "desligada") == 0) sc = COL_DIM;
        else if (strcmp(state, "iniciando") == 0) sc = COL_WARN;
        else if (strcmp(state, "porta ocupada") == 0) sc = COL_ERR;
        if (ROK(ry)) draw_kv(b, ry++, right_x, right_w, "estado", state, sc);
        if (a->vm.qemu_pid > 0) {
            char pid[32];
            snprintf(pid, sizeof(pid), "pid %d", a->vm.qemu_pid);
            if (ROK(ry)) draw_kv(b, ry++, right_x, right_w, "qemu", pid, COL_TEXT);
        } else {
            if (ROK(ry)) draw_kv(b, ry++, right_x, right_w, "qemu", "nao encontrado", COL_DIM);
        }
        char up[32];
        fmt_duration(up, sizeof(up), a->vm.uptime_s);
        if (ROK(ry)) draw_kv(b, ry++, right_x, right_w, "uptime", up, COL_TEXT);
        if (ROK(ry))
            draw_kv(b, ry++, right_x, right_w, "ssh :2222",
                    a->vm.ssh_listening ? "aberta" : "fechada",
                    a->vm.ssh_listening ? COL_OK : COL_DIM);
        if (ROK(ry))
            draw_kv(b, ry++, right_x, right_w, "http :8080",
                    a->vm.http_listening ? "aberta" : "fechada",
                    a->vm.http_listening ? COL_OK : COL_DIM);

        char ssh[160];
        snprintf(ssh, sizeof(ssh), "%s@%s -p 2222", a->vm.ssh_user, a->vm.ssh_host);
        ry++;
        if (ROK(ry)) {
            buf_printf(b, ry++, right_x, right_w, COL_DIM, BG_DEFAULT, A_NONE, "conexao");
            buf_printf(b, ry++, right_x, right_w, COL_ACCENT, BG_DEFAULT, A_NONE, "%s", ssh);
        }

        /* Role checklist. */
        ry++;
        if (ROK(ry)) buf_puts(b, ry++, right_x, COL_ACCENT, BG_DEFAULT, A_BOLD, "ROLES");
        ry++;
        int shown = 0;
        for (int i = 0; i < a->proj.n_roles && shown < 8; i++, shown++) {
            if (!ROK(ry)) break;
            const Role *r = &a->proj.roles[i];
            buf_printf(b, ry, right_x, 3, r->active ? COL_OK : COL_DIM, BG_DEFAULT, A_NONE,
                       "%s", r->active ? "\u25cf" : "\u25cb");
            buf_printf(b, ry, right_x + 3, right_w - 3,
                       r->active ? COL_TEXT : COL_DIM, BG_DEFAULT,
                       r->active ? A_NONE : A_DIM, "%s", r->name);
            ry++;
        }
        if (a->proj.n_roles > shown && ROK(ry))
            buf_printf(b, ry++, right_x, right_w, COL_DIM, BG_DEFAULT, A_NONE,
                       "\u2026 e mais %d", a->proj.n_roles - shown);
#undef ROK
    }

    /* ---- bottom: undo stack ---- */
    if (by >= 0) {
        buf_puts(b, by, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "ALTERACOES NESTA SESSAO");
        int un = undo_count();
        if (un == 0) {
            buf_printf(b, by, x + 24, col_w - 24, COL_DIM, BG_DEFAULT, A_NONE,
                       "nenhuma \u2014 Ctrl+Z reverte a ultima");
        } else {
            const UndoEntry *e = undo_at(0);
            buf_printf(b, by, x + 24, col_w - 24, COL_TEXT, BG_DEFAULT, A_NONE,
                       "%d \u00b7 ultima: %s", un, project_basename(e->path));
        }
    }
#undef ROW_OK
}

void dashboard_key(App *a, Key k)
{
    switch (k.type) {
    case KEY_F10:
        run_playbook(a);
        return;
    case KEY_F5: {
        char *argv[8];
        int i = 0;
        argv[i++] = (char *)"ansible";
        if (a->inv.n_groups) {
            static char grp[NAMEMAX];
            snprintf(grp, sizeof(grp), "%s", a->inv.groups[0].name);
            argv[i++] = grp;
        } else {
            argv[i++] = (char *)"all";
        }
        argv[i++] = (char *)"-m";
        argv[i++] = (char *)"ping";
        argv[i] = NULL;
        run_exec(a, RUN_KIND_PING, "ping de conectividade", argv);
        return;
    }
    case KEY_CHAR:
        switch (k.ch[0]) {
        case 'R':
            project_reload_roles(&a->proj);
            inv_load(&a->inv, a->proj.inventory);
            vmstat_probe(&a->vm, a->proj.disk, a->proj.inventory);
            app_status(a, 1, "projeto recarregado");
            return;
        case 'p': {
            char *argv[8];
            argv[0] = (char *)"ansible";
            argv[1] = (char *)"all";
            argv[2] = (char *)"-m";
            argv[3] = (char *)"ping";
            argv[4] = NULL;
            run_exec(a, RUN_KIND_PING, "ping de conectividade", argv);
            return;
        }
        default:
            return;
        }
    default:
        return;
    }
}
