#include "app.h"
#include "tui/theme.h"
#include "tui/term.h"
#include "edit/undo.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *key;
    const char *what;
} KeyRow;

/* How many bytes of s fit in `width` columns, backing up to the last space so
 * a word is never split. Always lands on a codepoint boundary. */
static size_t wrap_take(const char *s, int width, int *bytes)
{
    size_t len = strlen(s), p = 0, last_space = (size_t)-1, end = 0;
    int acc = 0;
    while (p < len && acc < width) {
        if (s[p] == ' ') last_space = p;
        size_t next = p;
        utf8_next(s, len, &next);
        p = next;
        acc++;
        end = p;
    }
    /* Only prefer the space if the rest actually fits in another line. */
    if (p < len && last_space != (size_t)-1) end = last_space;
    *bytes = (int)end;
    return end;
}

/* Draw a key/description block, wrapping the description onto extra rows.
 * Stops once `limit` is reached so a short terminal cannot spill into the
 * footer. Returns how many rows were consumed. */
static int kv_block(Buf *b, int y, int x, int w, int keyw, int limit,
                    const KeyRow *rows, size_t n)
{
    int descw = w - keyw;
    if (descw < 10) descw = 10;
    int ry = y;
    for (size_t i = 0; i < n; i++) {
        const char *s = rows[i].what;
        int first = 1;
        for (;;) {
            if (ry >= limit) return ry - y;
            int take = 0;
            wrap_take(s, descw, &take);
            if (first)
                buf_printf(b, ry, x, keyw, COL_KEY, BG_DEFAULT, A_BOLD, "%-*s", keyw,
                           rows[i].key);
            if (take > 0)
                buf_printf(b, ry, x + keyw, descw, COL_TEXT, BG_DEFAULT, A_NONE,
                           "%.*s", take, s);
            ry++;
            first = 0;
            s += take;
            while (*s == ' ') s++;
            if (*s == '\0') break;
        }
    }
    return ry - y;
}

void help_draw(App *a, Buf *b, int x, int y, int w, int h)
{
    (void)a;
    static const KeyRow global[] = {
        { "Tab / Shift-Tab", "trocar de aba" },
        { "1 .. 7",          "ir direto para uma aba" },
        { "F1",              "esta ajuda" },
        { "q  /  Ctrl-C",    "sair" },
        { "Ctrl-Z  /  z",    "desfazer a ultima alteracao" },
    };
    static const KeyRow per_view[] = {
        { "Enter",        "executar / alternar, depende da aba" },
        { "c  /  f",      "Playbook: ligar --check / --diff" },
        { "l  /  g",      "Playbook: --limit por host / por grupo" },
        { "a  /  d",      "adicionar / remover (hosts, roles)" },
        { "g",            "Hosts: criar um grupo" },
        { "s  /  k",      "VM: subir / derrubar" },
        { "y  /  p",      "Lint: idempotencia / ping" },
        { "PgUp / PgDn",  "rolar o log" },
        { "End",          "voltar a seguir o log ao vivo" },
        { "C",            "cancelar o comando em execucao" },
        { "R",            "recarregar o projeto do disco" },
    };
    static const KeyRow actions[] = {
        { "F10", "repetir o ultimo comando" },
        { "F5",  "ping de conectividade" },
        { "Esc", "cancelar prompt ou confirmacao" },
        { "\u2190 \u2192", "escolher no dialogo de confirmacao" },
    };

    const int keyw = 16;
    int col_w = (w >= 88) ? (w - 4) / 2 : w;
    /* The last two body rows are left to the pending-changes line. */
    int limit = y + h - 2;
    if (limit < y) limit = y;

    int yy = y + 2;
    if (y < limit) buf_puts(b, y, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "NAVEGACAO");
    yy += kv_block(b, yy, x, col_w, keyw, limit, global,
                   sizeof(global) / sizeof(global[0]));

    yy += 1;
    if (yy < limit) buf_puts(b, yy++, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "POR ABA");
    yy++;
    kv_block(b, yy, x, col_w, keyw, limit, per_view,
             sizeof(per_view) / sizeof(per_view[0]));

    if (w >= 88) {
        int rx = x + col_w + 4;
        int rw = w - col_w - 4;
        int rkw = 8;
        int ry = y;
        if (ry < limit) buf_puts(b, ry++, rx, COL_ACCENT, BG_DEFAULT, A_BOLD, "ATALHOS");
        ry += 2;
        ry += kv_block(b, ry, rx, rw, rkw, limit, actions,
                       sizeof(actions) / sizeof(actions[0]));

        ry += 1;
        if (ry < limit) buf_puts(b, ry++, rx, COL_ACCENT, BG_DEFAULT, A_BOLD, "SOBRE");
        ry++;
        static const char *about[] = {
            "Interface de terminal para gerenciar este",
            "projeto Ansible. Todo o estado e lido do",
            "disco; nenhum valor fica hard-coded.",
            "",
            "Alteracoes em site.yml e no inventario sao",
            "por edicao estrutural: apenas o byte do '#'",
            "e movido, e um snapshot e gravado em",
            ".state/backup antes de cada escrita.",
            "",
            "O log roda em uma pty, por isso a saida",
            "colorida do Ansible aparece como no terminal.",
        };
        for (size_t i = 0; i < sizeof(about) / sizeof(about[0]); i++, ry++) {
            if (ry >= limit) break;
            buf_printf(b, ry, rx, rw, COL_DIM, BG_DEFAULT, A_NONE, "%s", about[i]);
        }

        ry += 1;
        if (ry < limit) buf_puts(b, ry++, rx, COL_ACCENT, BG_DEFAULT, A_BOLD, "CORES DO LOG");
        ry++;
        struct { const char *t; uint8_t c; } sev[] = {
            { "ok=... changed=0", COL_OK },
            { "changed=N (N>0)", COL_WARN },
            { "failed / unreachable", COL_ERR },
        };
        for (size_t i = 0; i < sizeof(sev) / sizeof(sev[0]); i++, ry++) {
            if (ry >= limit) break;
            buf_puts(b, ry, rx, sev[i].c, BG_DEFAULT, A_NONE, "\u25cf");
            buf_printf(b, ry, rx + 4, rw - 4, COL_DIM, BG_DEFAULT, A_NONE, "%s", sev[i].t);
        }
    }

    if (h > 4) {
        int un = undo_count();
        int by = y + h - 2;
        if (by < b->rows - 2)
            buf_printf(b, by, x, w, COL_DIM, BG_DEFAULT, A_NONE,
                       "alteracoes pendentes nesta sessao: %d", un);
    }
}

void help_key(App *a, Key k)
{
    if (k.type == KEY_CHAR && k.ch[0] == 'R') {
        project_reload_roles(&a->proj);
        app_status(a, 1, "projeto recarregado");
    }
}
