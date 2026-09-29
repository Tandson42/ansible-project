#include "app.h"
#include "tui/theme.h"
#include "tui/term.h"
#include "edit/fsio.h"
#include "edit/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Hosts view: read the inventory as a table, add or remove entries, and show
 * the group_vars that apply to the selected host. Removal is destructive, so
 * it goes through the confirm modal and leaves a snapshot behind.
 */

void hosts_draw(App *a, Buf *b, int x, int y, int w, int h)
{
    /* Box occupies rows y+2 .. y+h-2, so the body keeps one spare row at the
     * bottom. Its interior holds the column header plus the data rows. */
    int box_h = h - 3;
    if (box_h < 3) box_h = 3;
    int page = box_h - 2;
    if (page < 1) page = 1;

    list_clamp(&a->sel_hosts, a->inv.n_ents);
    list_ensure_visible(&a->sel_hosts, a->inv.n_ents, page);

    buf_puts(b, y, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "INVENTARIO");
    char src[256];
    snprintf(src, sizeof(src), "%s  \u00b7  %d host(s) em %d grupo(s)",
             a->proj.inventory, a->inv.n_ents, a->inv.n_groups);
    buf_printf(b, y, x + 13, w - 13, COL_DIM, BG_DEFAULT, A_NONE, "%s", src);

    int ty = y + 2;
    buf_box(b, ty, x, box_h, w, COL_FRAME, NULL);

    /* column header */
    int cx = x + 2;
    buf_printf(b, ty, cx, 20, COL_ACCENT, BG_DEFAULT, A_BOLD, "%-18s", "GRUPO");
    cx += 20;
    buf_printf(b, ty, cx, 20, COL_ACCENT, BG_DEFAULT, A_BOLD, "%-18s", "HOST");
    cx += 20;
    buf_printf(b, ty, cx, 22, COL_ACCENT, BG_DEFAULT, A_BOLD, "%-22s", "CONEXAO");
    if (w > 70) buf_printf(b, ty, cx + 24, 20, COL_ACCENT, BG_DEFAULT, A_BOLD,
                           "%-18s", "GROUP_VARS");

    for (int row = 0; row < page; row++) {
        int i = a->sel_hosts.top + row;
        int yy = ty + 1 + row;
        if (yy >= y + h - 1) break;
        if (i >= a->inv.n_ents) break;
        const HostEnt *e = &a->inv.ents[i];
        int selected = (i == a->sel_hosts.sel);

        uint8_t bg = selected ? COL_SELBG : BG_DEFAULT;
        if (selected) buf_fill(b, yy, x + 1, 1, w - 2, COL_TEXT, bg, A_NONE);

        cx = x + 2;
        buf_printf(b, yy, cx, 20, selected ? COL_TEXT : COL_DIM, bg, A_NONE, "%-18s",
                   e->group);
        cx += 20;
        buf_printf(b, yy, cx, 20, selected ? 231 : COL_TEXT, bg,
                   selected ? A_BOLD : A_NONE, "%-18s", e->host);
        cx += 20;

        char conn[128];
        if (e->val[0]) snprintf(conn, sizeof(conn), "%s", e->val);
        else snprintf(conn, sizeof(conn), "(padrao do grupo)");
        buf_printf(b, yy, cx, 22, selected ? COL_TEXT : COL_DIM, bg, A_NONE, "%-22s",
                   conn);

        if (w > 70) {
            const char *gv = inv_group_vars_path(&a->inv, e->group);
            buf_printf(b, yy, cx + 24, 20, gv ? COL_OK : COL_DIM, bg, A_NONE, "%-18s",
                       gv ? "sim" : "nao");
        }
    }

    if (a->inv.n_ents == 0)
        buf_printf(b, ty + 2, x + 3, w - 6, COL_DIM, BG_DEFAULT, A_NONE,
                   "inventario vazio \u2014 'a' adiciona um host ao grupo selecionado");

    /* Detail of the selected host. */
    int dy = y + h - 2;
    if (dy < ty + page + 2) dy = ty + page + 2;
    if (dy < b->rows - 2 && a->inv.n_ents > 0 && a->sel_hosts.sel < a->inv.n_ents) {
        const HostEnt *e = &a->inv.ents[a->sel_hosts.sel];
        char line[512];
        snprintf(line, sizeof(line), "%s \u2192 %s", e->group, e->host);
        buf_printf(b, dy, x, w, COL_TEXT, BG_DEFAULT, A_NONE, "%s", line);

        const char *gv = inv_group_vars_path(&a->inv, e->group);
        if (gv) {
            char *text = NULL;
            if (fs_read_file(gv, &text, NULL) == 0) {
                char keys[400] = { 0 };
                LineIter it;
                Line ln;
                line_iter_init(&it, text, strlen(text));
                int k = 0;
                while (line_next(&it, &ln) && k < 3) {
                    char key[64] = { 0 };
                    if (sscanf(ln.text, "%63[^:]:", key) != 1) continue;
                    char *t = text_trim(key);
                    if (!t[0] || t[0] == '#') continue;
                    if (k > 0) strncat(keys, ", ", sizeof(keys) - strlen(keys) - 1);
                    strncat(keys, t, sizeof(keys) - strlen(keys) - 1);
                    k++;
                }
                free(text);
                buf_printf(b, dy, x + 34, w - 34, COL_DIM, BG_DEFAULT, A_NONE,
                           "group_vars: %s", keys);
            }
        }
    }
}

void hosts_key(App *a, Key k)
{
    int page = term_rows() > 12 ? term_rows() - 8 : 4;
    list_clamp(&a->sel_hosts, a->inv.n_ents);

    switch (k.type) {
    case KEY_UP:   list_move(&a->sel_hosts, a->inv.n_ents, -1); return;
    case KEY_DOWN: list_move(&a->sel_hosts, a->inv.n_ents, 1); return;
    case KEY_PGUP: list_move(&a->sel_hosts, a->inv.n_ents, -page); return;
    case KEY_PGDN: list_move(&a->sel_hosts, a->inv.n_ents, page); return;
    case KEY_HOME: a->sel_hosts.sel = 0; return;
    case KEY_END:  a->sel_hosts.sel = a->inv.n_ents - 1; return;

    case KEY_CHAR:
        switch (k.ch[0]) {
        case 'a': {
            if (a->inv.n_groups == 0) {
                app_status(a, 2, "crie um grupo primeiro: 'g'");
                return;
            }
            int gi = a->sel_hosts.sel;
            if (gi < 0 || gi >= a->inv.n_ents) gi = 0;
            snprintf(a->pending_arg, sizeof(a->pending_arg), "%s", a->inv.ents[gi].group);
            a->prompt_action = PA_ADD_HOST;
            prompt_begin(&a->prompt, " host em ", a->pending_arg, "");
            return;
        }
        case 'g': {
            a->prompt_action = PA_ADD_GROUP;
            prompt_begin(&a->prompt, " novo grupo ", "cria [grupo] vazio no inventario", "");
            return;
        }
        case 'd': {
            if (a->sel_hosts.sel < 0 || a->sel_hosts.sel >= a->inv.n_ents) {
                app_status(a, 2, "nenhum host selecionado");
                return;
            }
            snprintf(a->pending_arg, sizeof(a->pending_arg), "%s",
                     a->inv.ents[a->sel_hosts.sel].host);
            snprintf(a->pending_name, sizeof(a->pending_name), "%s",
                     a->inv.ents[a->sel_hosts.sel].group);
            a->pending_confirm = PC_REMOVE_HOST;
            char msg[256];
            snprintf(msg, sizeof(msg), "Remover o host %s do inventario?",
                     a->pending_arg);
            confirm_begin(&a->confirm, "remover host", msg);
            return;
        }
        case 'R':
            inv_load(&a->inv, a->proj.inventory);
            app_status(a, 1, "inventario recarregado");
            return;
        default:
            return;
        }
    default:
        return;
    }
}
