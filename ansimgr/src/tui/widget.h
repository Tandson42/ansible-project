#ifndef ANSIMGR_WIDGET_H
#define ANSIMGR_WIDGET_H

#include "tui/buf.h"
#include "tui/input.h"
#include <stddef.h>

/* ---- scrollable log -------------------------------------------------- */

#define LOG_MAX 5000

typedef struct {
    char **lines;
    int n;
    int cap;
    int offset_from_bottom; /* 0 = pinned to the tail */
    int follow;
} LogView;

void log_init(LogView *lv);
void log_free(LogView *lv);
void log_clear(LogView *lv);
void log_push(LogView *lv, const char *line);
int  log_line_count(const LogView *lv);
const char *log_line(const LogView *lv, int i);

/* ---- modal prompt ---------------------------------------------------- */

#define PROMPT_MAX 256

typedef struct {
    int  active;
    char text[PROMPT_MAX];
    char label[64];
    char hint[128];
} Prompt;

void prompt_begin(Prompt *p, const char *label, const char *hint, const char *initial);
int  prompt_handle(Prompt *p, const Key *k); /* 1 = accepted, 0 = still typing, -1 = cancelled */
void prompt_end(Prompt *p);

/* ---- modal confirmation ---------------------------------------------- */

typedef struct {
    int  active;
    char text[256];
    char action[64];
    int  choice; /* 0 = pending, 1 = yes, 2 = no */
} Confirm;

void confirm_begin(Confirm *c, const char *action, const char *text);
int  confirm_handle(Confirm *c, const Key *k); /* 1 = confirmed, 0 = pending, -1 = aborted */
void confirm_end(Confirm *c);

/* ---- selection list -------------------------------------------------- */

typedef struct {
    int  sel;
    int  top;
    int  n;
} ListSel;

void list_move(ListSel *s, int n, int delta);
void list_clamp(ListSel *s, int n);
void list_ensure_visible(ListSel *s, int n, int page);

/* ---- drawing helpers -------------------------------------------------- */

void draw_logview(Buf *b, LogView *lv, int y, int x, int h, int w);
void draw_prompt(Buf *b, const Prompt *p, int y, int x, int w);
void draw_confirm(Buf *b, const Confirm *c, int rows, int cols);
void draw_kv(Buf *b, int y, int x, int w, const char *k, const char *v, uint8_t vcol);
void draw_badge(Buf *b, int y, int x, const char *text, uint8_t fg, uint8_t bg);
void fmt_duration(char *out, size_t cap, long seconds);
void fmt_hhmmss(char *out, size_t cap, long epoch);

#endif /* ANSIMGR_WIDGET_H */
