#include "app.h"
#include "tui/theme.h"
#include "tui/term.h"
#include "edit/undo.h"

#include <stdio.h>
#include <string.h>

/*
 * Roles view: the union of the directories under roles/ and the entries
 * actually present in site.yml. Toggling edits the playbook in place by moving
 * a single '#', which is why a comment in the file is never at risk.
 *
 * Draw and key handling both go through role_rows() so the highlighted row and
 * the row acted upon are guaranteed to be the same one.
 */

#define MAX_ROWS (MAX_OCC + MAX_ROLES)

typedef struct {
    char name[NAMEMAX];
    int  occ;      /* index into the playbook's occurrence list, or -1 */
    int  active;   /* enabled in the playbook */
    int  on_disk;  /* roles/<name> exists */
    int  line;     /* 1-based line in the playbook, 0 when not present */
} RoleRow;

static int role_rows(App *a, RoleRow *rows, int cap)
{
    RoleOcc occ[MAX_OCC];
    int n_occ = yml_scan(a->proj.playbook, occ);
    if (n_occ < 0) n_occ = 0;

    int n = 0;
    for (int i = 0; i < n_occ && n < cap; i++) {
        RoleRow *r = &rows[n++];
        memset(r, 0, sizeof(*r));
        snprintf(r->name, sizeof(r->name), "%s", occ[i].name);
        r->occ = i;
        r->active = occ[i].active;
        r->on_disk = (project_role_index(&a->proj, occ[i].name) >= 0);
        r->line = yml_line_of(a->proj.playbook, i);
    }
    for (int j = 0; j < a->proj.n_roles && n < cap; j++) {
        int dup = 0;
        for (int i = 0; i < n_occ; i++)
            if (strcmp(occ[i].name, a->proj.roles[j].name) == 0) { dup = 1; break; }
        if (dup) continue;
        RoleRow *r = &rows[n++];
        memset(r, 0, sizeof(*r));
        snprintf(r->name, sizeof(r->name), "%s", a->proj.roles[j].name);
        r->occ = -1;
        r->active = 0;
        r->on_disk = 1;
        r->line = 0;
    }
    return n;
}

void roles_draw(App *a, Buf *b, int x, int y, int w, int h)
{
    static RoleRow rows[MAX_ROWS];
    int n = role_rows(a, rows, MAX_ROWS);
    int n_active = 0;
    for (int i = 0; i < n; i++) if (rows[i].active) n_active++;

    /* Box occupies rows y+2 .. y+h-2, keeping one spare row at the bottom. */
    int box_h = h - 3;
    if (box_h < 3) box_h = 3;
    int page = box_h - 2;
    if (page < 1) page = 1;

    list_clamp(&a->sel_roles, n);
    list_ensure_visible(&a->sel_roles, n, page);

    buf_puts(b, y, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "ROLES");
    char src[256];
    snprintf(src, sizeof(src), "%s  \u00b7  %d entrada(s), %d ativa(s)",
             project_basename(a->proj.playbook), n, n_active);
    buf_printf(b, y, x + 7, w - 7, COL_DIM, BG_DEFAULT, A_NONE, "%s", src);

    int ty = y + 2;
    buf_box(b, ty, x, box_h, w, COL_FRAME, NULL);

    int cx = x + 2;
    cx += 4;
    buf_printf(b, ty, cx, 24, COL_ACCENT, BG_DEFAULT, A_BOLD, "%-24s", "ROLE");
    cx += 24;
    buf_printf(b, ty, cx, 12, COL_ACCENT, BG_DEFAULT, A_BOLD, "%-12s", "ESTADO");
    cx += 12;
    buf_printf(b, ty, cx, 8, COL_ACCENT, BG_DEFAULT, A_BOLD, "%-8s", "PASTA");
    cx += 8;
    if (w > 78) buf_printf(b, ty, cx, 8, COL_ACCENT, BG_DEFAULT, A_BOLD, "%-8s", "LINHA");

    for (int i = 0; i < n && i < page; i++) {
        int yy = ty + 1 + i;
        if (yy >= y + h - 1) break;

        const RoleRow *r = &rows[a->sel_roles.top + i];
        int selected = ((a->sel_roles.top + i) == a->sel_roles.sel);
        uint8_t bg = selected ? COL_SELBG : BG_DEFAULT;
        if (selected) buf_fill(b, yy, x + 1, 1, w - 2, COL_TEXT, bg, A_NONE);

        cx = x + 2;
        buf_printf(b, yy, cx, 4, r->active ? COL_OK : COL_DIM, bg, A_NONE, "%s",
                   r->active ? "\u25cf" : "\u25cb");
        cx += 4;
        buf_printf(b, yy, cx, 24, r->active ? (selected ? 231 : COL_TEXT) : COL_DIM, bg,
                   selected ? A_BOLD : A_NONE, "%-24s", r->name);
        cx += 24;
        buf_printf(b, yy, cx, 12, r->active ? COL_OK : COL_DIM, bg,
                   r->active ? A_BOLD : A_NONE, "%-12s",
                   r->occ >= 0 ? (r->active ? "ativa" : "comentada") : "no playbook");
        cx += 12;
        buf_printf(b, yy, cx, 8, r->on_disk ? COL_TEXT : COL_DIM, bg, A_NONE, "%-8s",
                   r->on_disk ? "sim" : "nao");
        cx += 8;
        if (w > 78) {
            char ln[16];
            snprintf(ln, sizeof(ln), "%d", r->line);
            buf_printf(b, yy, cx, 8, COL_DIM, bg, A_NONE, "%-8s",
                       r->line ? ln : "-");
        }
    }

    if (n == 0)
        buf_printf(b, ty + 2, x + 3, w - 6, COL_DIM, BG_DEFAULT, A_NONE,
                   "nenhuma role encontrada \u2014 'a' adiciona ao playbook");

    int dy = y + h - 2;
    if (dy < b->rows - 2) {
        if (n_active == 0)
            buf_printf(b, dy, x, w, COL_WARN, BG_DEFAULT, A_NONE,
                       "atencao: nenhuma role ativa \u2014 o playbook nao fara nada");
        else
            buf_printf(b, dy, x, w, COL_DIM, BG_DEFAULT, A_NONE,
                       "Enter liga/desliga  \u00b7  a adiciona  \u00b7  d remove  \u00b7  z desfaz");
    }
}

void roles_key(App *a, Key k)
{
    static RoleRow rows[MAX_ROWS];
    int n = role_rows(a, rows, MAX_ROWS);
    int page = term_rows() > 12 ? term_rows() - 8 : 4;

    list_clamp(&a->sel_roles, n);

    switch (k.type) {
    case KEY_UP:   list_move(&a->sel_roles, n, -1); return;
    case KEY_DOWN: list_move(&a->sel_roles, n, 1); return;
    case KEY_PGUP: list_move(&a->sel_roles, n, -page); return;
    case KEY_PGDN: list_move(&a->sel_roles, n, page); return;
    case KEY_HOME: a->sel_roles.sel = 0; return;
    case KEY_END:  a->sel_roles.sel = n > 0 ? n - 1 : 0; return;

    case KEY_ENTER: {
        if (a->sel_roles.sel < 0 || a->sel_roles.sel >= n) return;
        const RoleRow *r = &rows[a->sel_roles.sel];
        if (r->occ < 0) {
            app_status(a, 2, "%s existe em roles/ mas nao esta no playbook \u2014 use 'a'", r->name);
            return;
        }
        int rc = yml_toggle(a->proj.playbook, r->occ);
        if (rc < 0) {
            app_status(a, 3, "falha ao editar %s", project_basename(a->proj.playbook));
            return;
        }
        project_reload_roles(&a->proj);
        app_status(a, 1, "role %s %s", r->name, rc ? "ativada" : "comentada");
        return;
    }

    case KEY_CHAR:
        switch (k.ch[0]) {
        case 'a':
            a->prompt_action = PA_ADD_ROLE;
            prompt_begin(&a->prompt, " role ", "nome em roles/", "");
            return;
        case 'd': {
            if (a->sel_roles.sel < 0 || a->sel_roles.sel >= n) return;
            const RoleRow *r = &rows[a->sel_roles.sel];
            if (r->occ < 0) {
                app_status(a, 2, "%s nao esta no playbook", r->name);
                return;
            }
            /* The name goes in pending_arg for the status message, but the
             * occurrence index has to survive too, so keep it out of the
             * name-parsing path: store the index and let app.c read it. */
            snprintf(a->pending_name, sizeof(a->pending_name), "%s", r->name);
            snprintf(a->pending_arg, sizeof(a->pending_arg), "%d", r->occ);
            char msg[256];
            snprintf(msg, sizeof(msg), "Apagar a linha da role %s do playbook?", r->name);
            a->pending_confirm = PC_DELETE_ROLE;
            confirm_begin(&a->confirm, "remover role", msg);
            return;
        }
        case 'R':
            project_reload_roles(&a->proj);
            app_status(a, 1, "projeto recarregado");
            return;
        case 'z':
            if (undo_count() > 0 && undo_apply(0) == 0) {
                project_reload_roles(&a->proj);
                app_status(a, 1, "desfeito: %s", project_basename(undo_at(0)->path));
            } else {
                app_status(a, 3, "nada para desfazer");
            }
            return;
        default:
            return;
        }
    default:
        return;
    }
}
