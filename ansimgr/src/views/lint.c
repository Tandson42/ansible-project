#include "app.h"
#include "tui/theme.h"
#include "tui/term.h"
#include "edit/fsio.h"
#include "edit/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Lint view: static checks plus the idempotency console. The history file
 * written by the executor is read back so a regression stays visible after
 * the process is restarted.
 */

#define HIST_MAX 12

typedef struct {
    char ts[32];
    char title[40];
    int  exit_code;
    int  ok, changed, unreachable, failed;
    char verdict[32];
} HistRow;

static int load_history(App *a, HistRow *rows, int cap)
{
    char path[600];
    snprintf(path, sizeof(path), "%s/.state/history", a->proj.root);

    char *text = NULL;
    if (fs_read_file(path, &text, NULL) != 0) return 0;

    /* Read backwards so the newest entries win the limited slots. */
    char *lines[4096];
    int n = 0;
    char *save = text;
    for (char *l = strtok_r(save, "\n", &save); l && n < 4096;
         l = strtok_r(NULL, "\n", &save))
        lines[n++] = l;

    int count = 0;
    for (int i = n - 1; i >= 0 && count < cap; i--) {
        HistRow *r = &rows[count];
        memset(r, 0, sizeof(*r));
        char verdict[64] = { 0 };
        int m = sscanf(lines[i],
                       "%15s %*s exit=%d %39s ok=%d changed=%d unreachable=%d failed=%d [%63[^]]]",
                       r->ts, &r->exit_code, r->title, &r->ok, &r->changed,
                       &r->unreachable, &r->failed, verdict);
        if (m >= 4) {
            snprintf(r->verdict, sizeof(r->verdict), "%s", verdict);
            count++;
        }
    }
    free(text);
    return count;
}

void lint_draw(App *a, Buf *b, int x, int y, int w, int h)
{
    buf_puts(b, y, x, COL_ACCENT, BG_DEFAULT, A_BOLD, "DIAGNOSTICO");
    buf_printf(b, y, x + 12, w - 12, COL_DIM, BG_DEFAULT, A_NONE,
               "checagens estaticas e verificacao de idempotencia");

    int yy = y + 2;
    buf_box(b, yy, x, 7, w, COL_FRAME, "ACOES");
    /* The label column is 34 wide and the command starts at x+42, so both
     * fields have to stay short enough not to collide. Built per draw so the
     * playbook name stays in sync with the project. */
    const char *pb = project_basename(a->proj.playbook);
    struct { char key; const char *label; char cmd[80]; } acts[4];
    acts[0].key = 'y';
    acts[0].label = "idempotencia (re-executa)";
    snprintf(acts[0].cmd, sizeof(acts[0].cmd), "ansible-playbook %s", pb);
    acts[1].key = 'p';
    acts[1].label = "ping de conectividade";
    snprintf(acts[1].cmd, sizeof(acts[1].cmd), "ansible all -m ping");
    acts[2].key = 's';
    acts[2].label = "syntax-check";
    snprintf(acts[2].cmd, sizeof(acts[2].cmd), "ansible-playbook %s --syntax-check", pb);
    acts[3].key = 'l';
    acts[3].label = "ansible-lint";
    snprintf(acts[3].cmd, sizeof(acts[3].cmd), "ansible-lint %s roles/", pb);
    for (int i = 0; i < 4; i++) {
        int ry = yy + 1 + i;
        buf_printf(b, ry, x + 2, 4, COL_KEY, BG_DEFAULT, A_BOLD, "%c", acts[i].key);
        buf_printf(b, ry, x + 6, 34, COL_TEXT, BG_DEFAULT, A_NONE, "%s", acts[i].label);
        buf_printf(b, ry, x + 42, w - 44, COL_DIM, BG_DEFAULT, A_NONE, "%s", acts[i].cmd);
    }
    yy += 8;

    /* Idempotency verdict. */
    buf_box(b, yy, x, 4, w, COL_FRAME, "VEREDITO DA ULTIMA EXECUCAO");
    if (a->last.valid) {
        const char *v = recap_verdict(&a->last);
        uint8_t col = COL_OK;
        if (a->last.failed > 0 || a->last.unreachable > 0) col = COL_ERR;
        else if (a->last.changed > 0) col = COL_WARN;
        buf_printf(b, yy + 1, x + 2, 30, col, BG_DEFAULT, A_BOLD, "%s", v);
        char line[200];
        snprintf(line, sizeof(line),
                 "host %s  \u00b7  ok=%d  changed=%d  unreachable=%d  failed=%d  skipped=%d  rescued=%d  ignored=%d",
                 a->last.host[0] ? a->last.host : "-", a->last.ok, a->last.changed,
                 a->last.unreachable, a->last.failed, a->last.skipped, a->last.rescued,
                 a->last.ignored);
        buf_printf(b, yy + 2, x + 2, w - 4, COL_TEXT, BG_DEFAULT, A_NONE, "%s", line);
    } else {
        buf_printf(b, yy + 1, x + 2, w - 4, COL_DIM, BG_DEFAULT, A_NONE,
                   "ainda nao houve execucao \u2014 pressione 'y'");
    }
    yy += 5;

    /* History. */
    if (h - (yy - y) > 6) {
        static HistRow hist[HIST_MAX];
        int n = load_history(a, hist, HIST_MAX);
        int box_h = h - (yy - y);
        if (box_h > 4) {
            buf_box(b, yy, x, box_h, w, COL_FRAME, "HISTORICO");
            if (n == 0) {
                buf_printf(b, yy + 2, x + 2, w - 4, COL_DIM, BG_DEFAULT, A_NONE,
                           "sem registros em .state/history");
            } else {
                buf_printf(b, yy + 1, x + 2, 20, COL_ACCENT, BG_DEFAULT, A_BOLD,
                           "%-20s", "QUANDO");
                buf_printf(b, yy + 1, x + 22, 30, COL_ACCENT, BG_DEFAULT, A_BOLD,
                           "%-30s", "COMANDO");
                buf_printf(b, yy + 1, x + 54, 30, COL_ACCENT, BG_DEFAULT, A_BOLD,
                           "%-30s", "CONTAGENS");
                for (int i = 0; i < n && i + 2 < box_h; i++) {
                    int ry = yy + 2 + i;
                    uint8_t col = COL_OK;
                    if (hist[i].failed > 0 || hist[i].unreachable > 0) col = COL_ERR;
                    else if (hist[i].changed > 0) col = COL_WARN;
                    buf_printf(b, ry, x + 2, 20, COL_DIM, BG_DEFAULT, A_NONE, "%-20s",
                               hist[i].ts);
                    buf_printf(b, ry, x + 22, 30, COL_TEXT, BG_DEFAULT, A_NONE, "%-30s",
                               hist[i].title);
                    char c[80];
                    snprintf(c, sizeof(c), "ok=%d chg=%d unr=%d fail=%d", hist[i].ok,
                             hist[i].changed, hist[i].unreachable, hist[i].failed);
                    buf_printf(b, ry, x + 54, 30, col, BG_DEFAULT, A_NONE, "%-30s", c);
                }
            }
        }
    }
}

void lint_key(App *a, Key k)
{
    if (k.type != KEY_CHAR) return;

    switch (k.ch[0]) {
    case 'y': {
        char *argv[16];
        run_build_playbook_argv(a, argv);
        run_exec(a, RUN_KIND_CHECK, "verificacao de idempotencia", argv);
        return;
    }
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
    case 's': {
        char *argv[8];
        argv[0] = (char *)"ansible-playbook";
        argv[1] = (char *)project_basename(a->proj.playbook);
        argv[2] = (char *)"--syntax-check";
        argv[3] = NULL;
        run_exec(a, RUN_KIND_SYNTAX, "syntax-check", argv);
        return;
    }
    case 'l': {
        char *argv[8];
        argv[0] = (char *)"ansible-lint";
        argv[1] = (char *)project_basename(a->proj.playbook);
        argv[2] = (char *)"roles/";
        argv[3] = NULL;
        run_exec(a, RUN_KIND_LINT, "ansible-lint", argv);
        return;
    }
    case 'r':
        app_status(a, 1, "diagnostico atualizado");
        return;
    default:
        return;
    }
}
