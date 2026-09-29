#include "app.h"
#include "tui/term.h"
#include "tui/theme.h"
#include "edit/undo.h"
#include "model/inventory.h"
#include "edit/ymlrole.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAB_NAMES[V_COUNT] = {
    "Dashboard", "Hosts", "Roles", "Playbook", "VM", "Lint", "Ajuda"
};

static const char *TAB_HINTS[V_COUNT] = {
    "panorama do projeto",
    "inventario e grupos",
    "ativar e desativar roles",
    "executar site.yml",
    "controlar a VM QEMU",
    "syntax-check, lint e idempotencia",
    "mapa de teclas"
};

const char *view_name(int id)
{
    if (id < 0 || id >= V_COUNT) return "?";
    return TAB_NAMES[id];
}

const char *view_hint(int id)
{
    if (id < 0 || id >= V_COUNT) return "";
    return TAB_HINTS[id];
}

void app_status(App *a, int kind, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof(a->status), fmt, ap);
    va_end(ap);
    a->status_kind = kind;
}

/* --------------------------------------------------------------------- */
/* chrome                                                                */
/* --------------------------------------------------------------------- */

void chrome_header(App *a, Buf *b)
{
    buf_fill(b, 0, 0, 1, b->cols, COL_TITLE, 236, A_BOLD);

    const char *root = project_basename(a->proj.root);
    buf_printf(b, 0, 1, b->cols / 2, COL_TITLE, BG_DEFAULT, A_BOLD,
               "ansimgr \u00b7 %s", root);

    /* Right side: clock and VM state. */
    char clock[16];
    fmt_hhmmss(clock, sizeof(clock), (long)time(NULL));

    const char *vm = vmstat_summary(&a->vm);
    uint8_t vmcol = COL_OK;
    if (strcmp(vm, "desligada") == 0) vmcol = COL_DIM;
    else if (strcmp(vm, "iniciando") == 0) vmcol = COL_WARN;
    else if (strcmp(vm, "porta ocupada") == 0) vmcol = COL_ERR;

    char right[128];
    snprintf(right, sizeof(right), "vm %s \u00b7 %s", vm, clock);
    int rw = str_width(right);
    buf_printf(b, 0, b->cols - rw - 1, rw + 1, vmcol, BG_DEFAULT, A_BOLD, "%s", right);
}

void chrome_tabs(App *a, Buf *b)
{
    buf_fill(b, 1, 0, 1, b->cols, COL_DIM, BG_DEFAULT, A_NONE);
    int x = 1;
    for (int i = 0; i < V_COUNT; i++) {
        char label[32];
        snprintf(label, sizeof(label), " %s ", TAB_NAMES[i]);
        int w = str_width(label);
        if (x + w >= b->cols - 1) break;
        if (i == a->active) {
            buf_fill(b, 1, x, 1, w, 235, COL_TAB_ACTIVE, A_BOLD);
            buf_puts(b, 1, x, 231, BG_DEFAULT, A_BOLD, label);
            buf_puts(b, 1, x + w - 1, COL_TAB_ACTIVE, BG_DEFAULT, A_BOLD, " ");
        } else {
            buf_puts(b, 1, x, COL_DIM, BG_DEFAULT, A_NONE, label);
        }
        x += w + 1;
    }
}

void chrome_sidebar(App *a, Buf *b, int x, int y, int w, int h)
{
    if (w < 18) return;
    buf_fill(b, y, x, h, w, COL_DIM, BG_DEFAULT, A_NONE);
    buf_vline(b, y, h, x + w, COL_FRAME, "\u2502");

    int yy = y + 1;
    buf_printf(b, yy++, x + 1, w - 2, COL_ACCENT, BG_DEFAULT, A_BOLD, "PROJETO");
    yy++;

    int n_active = 0;
    for (int i = 0; i < a->proj.n_roles; i++)
        if (a->proj.roles[i].active) n_active++;

    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%s",
               "playbook", project_basename(a->proj.playbook));
    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%s",
               "inventory", project_basename(a->proj.inventory));
    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%d",
               "hosts", a->inv.n_ents);
    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%d",
               "grupos", a->inv.n_groups);
    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%d",
               "roles dir", a->proj.n_roles);

    char roles[32];
    snprintf(roles, sizeof(roles), "%d/%d", n_active, a->proj.n_roles);
    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%s",
               "no playbook", roles);
    yy++;

    buf_printf(b, yy++, x + 1, w - 2, COL_ACCENT, BG_DEFAULT, A_BOLD, "VM");
    yy++;
    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%s",
               "estado", vmstat_summary(&a->vm));
    uint8_t pcol = a->vm.ssh_listening ? COL_OK : COL_DIM;
    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%s",
               "ssh :2222", a->vm.ssh_listening ? "aberta" : "fechada");
    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%s",
               "http :8080", a->vm.http_listening ? "aberta" : "fechada");
    char up[32];
    fmt_duration(up, sizeof(up), a->vm.uptime_s);
    buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%s",
               "uptime", up);
    (void)pcol;

    yy++;
    buf_printf(b, yy++, x + 1, w - 2, COL_ACCENT, BG_DEFAULT, A_BOLD, "ULTIMA EXECUCAO");
    yy++;
    if (a->last.valid) {
        const char *v = recap_verdict_short(&a->last);
        uint8_t col = COL_OK;
        if (a->last.failed > 0 || a->last.unreachable > 0) col = COL_ERR;
        else if (a->last.changed > 0) col = COL_WARN;
        buf_printf(b, yy++, x + 1, w - 2, col, BG_DEFAULT, A_BOLD, "%-12s%s",
                   "veredito", v);
        buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "%-12s%d/%d/%d",
                   "ok/chg/fal", a->last.ok, a->last.changed, a->last.failed);
    } else {
        buf_printf(b, yy++, x + 1, w - 2, COL_DIM, BG_DEFAULT, A_NONE, "nenhuma");
    }
}

void chrome_footer(App *a, Buf *b)
{
    int y = b->rows - 2;
    buf_fill(b, y, 0, 2, b->cols, COL_TEXT, 234, A_NONE);

    /* Row 1: context-sensitive key hints. */
    int x = 1;
    struct { const char *k; const char *v; } hints[5];
    int nh = 0;

    const char *nav = "Tab aba";
    if (a->prompt.active) nav = "Enter confirmar  Esc cancelar";
    else if (a->confirm.active) nav = "\u2190\u2192 escolher  Enter  Esc";
    else if (run_is_busy(a)) nav = "PgUp/PgDn rolar  End seguir  Ctrl-C cancelar";
    else if (a->active == V_ROLES) nav = "Enter ligar/desligar  a adicionar  d remover";
    else if (a->active == V_HOSTS) nav = "a adicionar  d remover  Enter ver group_vars";
    else if (a->active == V_RUN) nav = "Enter executar  c --check  f --diff  l --limit";
    else if (a->active == V_VM) nav = "s subir  k derrubar  r reconsultar";
    else if (a->active == V_LINT) nav = "y idempotencia  p ping  s sintaxe  l lint  r recarregar";
    else if (a->active == V_DASHBOARD) nav = "F10 executar  F5 ping  R recarregar";

    hints[nh].k = nav; hints[nh].v = NULL; nh++;
    if (nh < 5) { hints[nh].k = "F1 ajuda"; hints[nh].v = NULL; nh++; }
    if (nh < 5) { hints[nh].k = "q sair"; hints[nh].v = NULL; nh++; }

    /* The contextual hint is the only one that can be long, so give it the room
     * the fixed trailing hints are not using. A flat cap would drop keys like
     * "f --diff  l --limit" even on a wide terminal. */
    int tail_w = 0;
    for (int i = 1; i < nh; i++) tail_w += str_width(hints[i].k) + 2; /* "key" + "· " */
    int nav_room = b->cols - 2 - tail_w;
    if (nav_room < 12) nav_room = 12;

    for (int i = 0; i < nh && x < b->cols - 2; i++) {
        int room = (i == 0) ? nav_room : b->cols - x;
        buf_printf(b, y, x, room, COL_KEY, BG_DEFAULT, A_BOLD, "%s", hints[i].k);
        x += str_width(hints[i].k) + 2;
        if (i + 1 < nh) {
            buf_printf(b, y, x, 1, COL_DIM, BG_DEFAULT, A_NONE, "\u00b7");
            x += 2;
        }
    }

    /* Row 2: status message or live job state. */
    int sy = y + 1;
    if (run_is_busy(a)) {
        long el_s = a->job_started ? (long)time(NULL) - a->job_started : 0;
        char el[32];
        fmt_duration(el, sizeof(el), el_s);

        buf_printf(b, sy, 0, 3, COL_ACCENT, BG_DEFAULT, A_BOLD, "\u25cf");
        buf_printf(b, sy, 4, 40, COL_ACCENT, BG_DEFAULT, A_BOLD, "%s", a->job_title);
        int w = 5 + str_width(a->job_title) + 2;
        buf_printf(b, sy, w, 12, COL_DIM, BG_DEFAULT, A_NONE, "REC %s", el);
    } else {
        uint8_t col = COL_TEXT;
        if (a->status_kind == 1) col = COL_OK;
        else if (a->status_kind == 2) col = COL_WARN;
        else if (a->status_kind == 3) col = COL_ERR;
        buf_printf(b, sy, 1, b->cols - 2, col, BG_DEFAULT,
                   a->status_kind == 3 ? A_BOLD : A_NONE, "%s", a->status);
    }
}

/* --------------------------------------------------------------------- */
/* dispatch                                                              */
/* --------------------------------------------------------------------- */

void view_draw(App *a, Buf *b)
{
    int body_top = 2;
    int body_h = b->rows - 2 - 2;
    if (body_h < 3) body_h = 3;

    int side_w = (b->cols >= 100) ? 26 : 0;
    int body_x = side_w + 1;
    int body_w = b->cols - body_x;
    if (body_w < 20) body_w = b->cols;

    chrome_header(a, b);
    chrome_tabs(a, b);

    if (side_w > 0)
        chrome_sidebar(a, b, 0, body_top, side_w, body_h);

    switch (a->active) {
    case V_DASHBOARD: dashboard_draw(a, b, body_x, body_top, body_w, body_h); break;
    case V_HOSTS:     hosts_draw(a, b, body_x, body_top, body_w, body_h); break;
    case V_ROLES:     roles_draw(a, b, body_x, body_top, body_w, body_h); break;
    case V_RUN:       run_draw(a, b, body_x, body_top, body_w, body_h); break;
    case V_VM:        vm_draw(a, b, body_x, body_top, body_w, body_h); break;
    case V_LINT:      lint_draw(a, b, body_x, body_top, body_w, body_h); break;
    case V_HELP:      help_draw(a, b, body_x, body_top, body_w, body_h); break;
    default: break;
    }

    chrome_footer(a, b);

    if (a->prompt.active)
        draw_prompt(b, &a->prompt, b->rows - 3, 1, b->cols - 2);
    if (a->confirm.active)
        draw_confirm(b, &a->confirm, b->rows, b->cols);
}

void view_key(App *a, Key k)
{
    /* Modal layers take precedence over everything. */
    if (a->confirm.active) {
        char action[64];
        snprintf(action, sizeof(action), "%s", a->confirm.action);
        int pc = a->pending_confirm;
        a->pending_confirm = PC_NONE;

        int r = confirm_handle(&a->confirm, &k);
        if (r == 0) return;
        if (r < 0) { app_status(a, 0, "cancelado"); return; }

        switch (pc) {
        case PC_DELETE_ROLE: {
            char name[NAMEMAX] = { 0 };
            snprintf(name, sizeof(name), "%s", a->pending_name);
            if (yml_delete(a->proj.playbook, atoi(a->pending_arg)) == 0) {
                project_reload_roles(&a->proj);
                app_status(a, 1, "role %s removida de %s", name,
                           project_basename(a->proj.playbook));
            } else {
                app_status(a, 3, "falha ao remover %s", name);
            }
            break;
        }
        case PC_REMOVE_HOST: {
            char host[NAMEMAX] = { 0 }, grp[NAMEMAX] = { 0 };
            snprintf(host, sizeof(host), "%s", a->pending_arg);
            snprintf(grp, sizeof(grp), "%s", a->pending_name);
            int rc = inv_remove_host(&a->inv, grp, host);
            if (rc == 0) app_status(a, 1, "host %s removido de %s", host, grp);
            else if (rc == 1) app_status(a, 2, "%s nao encontrado", host);
            else app_status(a, 3, "falha ao remover %s", host);
            list_clamp(&a->sel_hosts, a->inv.n_ents);
            break;
        }
        case PC_KILL_VM: {
            if (a->vm.qemu_pid > 0) {
                char *argv[4];
                argv[0] = (char *)"kill";
                argv[1] = (char *)"-TERM";
                snprintf(a->job_title, sizeof(a->job_title), "derrubar QEMU pid %d",
                         a->vm.qemu_pid);
                argv[2] = a->pending_arg;
                argv[3] = NULL;
                /* Run it through the same pty path so the log stays consistent. */
                log_clear(&a->log);
                a->job_kind = RUN_KIND_NONE;
                a->job_started = (long)time(NULL);
                a->job = job_start(argv, a->proj.root, term_cols(), term_rows(),
                                   run_on_line, run_on_exit, a);
                if (a->job) app_status(a, 0, "enviando SIGTERM ao QEMU pid %s", a->pending_arg);
                else app_status(a, 3, "falha ao executar kill");
            } else {
                app_status(a, 2, "nenhum processo QEMU deste projeto");
            }
            break;
        }
        case PC_UNDO: {
            if (undo_apply(0) == 0) {
                project_reload_roles(&a->proj);
                inv_load(&a->inv, a->proj.inventory);
                app_status(a, 1, "desfeito: %s", project_basename(undo_at(0)->path));
            } else {
                app_status(a, 3, "nao foi possivel desfazer");
            }
            break;
        }
        default:
            app_status(a, 1, "confirmado: %s", action);
            break;
        }
        return;
    }
    if (a->prompt.active) {
        int r = prompt_handle(&a->prompt, &k);
        if (r != 0) {
            char value[PROMPT_MAX];
            snprintf(value, sizeof(value), "%s", a->prompt.text);
            int action = a->prompt_action;
            a->prompt_action = PA_NONE;
            /* Clear the field now that the value has been read out. */
            prompt_end(&a->prompt);
            if (r < 0) { app_status(a, 0, "cancelado"); return; }

            switch (action) {
            case PA_LIMIT:
                snprintf(a->limit, sizeof(a->limit), "%s", value);
                app_status(a, 0, a->limit[0] ? "--limit %s" : "--limit limpo", a->limit);
                break;
            case PA_ADD_ROLE: {
                if (!value[0]) { app_status(a, 0, "cancelado"); break; }
                if (project_role_index(&a->proj, value) < 0) {
                    app_status(a, 3, "role inexistente: %s", value);
                    break;
                }
                int was_enable = 0;
                int rc = yml_add_role(a->proj.playbook, value);
                if (rc == 2) {
                    /* It was already listed but commented out: enable it.
                     * yml_toggle returns 1 for "now active", 0 for "now off",
                     * so only 1 counts as success here. */
                    RoleOcc occ[MAX_OCC];
                    int n = yml_scan(a->proj.playbook, occ);
                    rc = -1;
                    for (int i = 0; i < n; i++)
                        if (!strcmp(occ[i].name, value)) {
                            rc = yml_toggle(a->proj.playbook, i) == 1 ? 0 : -1;
                            break;
                        }
                    was_enable = (rc == 0);
                }
                if (rc == 0) {
                    project_reload_roles(&a->proj);
                    if (was_enable)
                        app_status(a, 1, "role %s habilitada no playbook", value);
                    else
                        app_status(a, 1, "role %s adicionada ao playbook", value);
                } else if (rc == 1) {
                    app_status(a, 2, "%s ja esta no playbook", value);
                } else {
                    app_status(a, 2, "nao foi possivel adicionar %s", value);
                }
                break;
            }
            case PA_ADD_HOST: {
                if (!value[0]) { app_status(a, 0, "cancelado"); break; }
                int rc = inv_add_host(&a->inv, a->pending_arg, value, NULL);
                if (rc == 0) app_status(a, 1, "host %s adicionado a %s", value, a->pending_arg);
                else if (rc == 1) app_status(a, 2, "%s ja existe em %s", value, a->pending_arg);
                else app_status(a, 3, "falha ao adicionar %s", value);
                break;
            }
            case PA_ADD_GROUP: {
                if (!value[0]) { app_status(a, 0, "cancelado"); break; }
                int rc = inv_add_group(&a->inv, value);
                if (rc == 0) app_status(a, 1, "grupo %s criado", value);
                else if (rc == 1) app_status(a, 2, "grupo %s ja existe", value);
                else app_status(a, 3, "falha ao criar grupo %s", value);
                break;
            }
            default:
                app_status(a, 0, "confirmado");
                break;
            }
        }
        return;
    }

    switch (a->active) {
    case V_DASHBOARD: dashboard_key(a, k); break;
    case V_HOSTS:     hosts_key(a, k); break;
    case V_ROLES:     roles_key(a, k); break;
    case V_RUN:       run_key(a, k); break;
    case V_VM:        vm_key(a, k); break;
    case V_LINT:      lint_key(a, k); break;
    case V_HELP:      help_key(a, k); break;
    default: break;
    }
}

void view_tick(App *a)
{
    vmstat_probe(&a->vm, a->proj.disk, a->proj.inventory);
}
