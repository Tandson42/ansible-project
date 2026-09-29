#include "tui/widget.h"
#include "tui/input.h"
#include "tui/theme.h"
#include "proc/ansi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- log ------------------------------------------------------------- */

void log_init(LogView *lv)
{
    memset(lv, 0, sizeof(*lv));
    lv->follow = 1;
}

void log_free(LogView *lv)
{
    for (int i = 0; i < lv->n; i++) free(lv->lines[i]);
    free(lv->lines);
    memset(lv, 0, sizeof(*lv));
}

void log_clear(LogView *lv)
{
    for (int i = 0; i < lv->n; i++) free(lv->lines[i]);
    lv->n = 0;
    lv->offset_from_bottom = 0;
    lv->follow = 1;
}

void log_push(LogView *lv, const char *line)
{
    if (lv->n >= LOG_MAX) {
        /* Drop the oldest half in one move rather than shifting per line. */
        int drop = LOG_MAX / 2;
        for (int i = 0; i < drop; i++) free(lv->lines[i]);
        memmove(lv->lines, lv->lines + drop, (size_t)(lv->n - drop) * sizeof(char *));
        lv->n -= drop;
    }
    if (lv->n >= lv->cap) {
        int cap = lv->cap ? lv->cap * 2 : 256;
        if (cap > LOG_MAX) cap = LOG_MAX;
        char **nl = realloc(lv->lines, (size_t)cap * sizeof(char *));
        if (!nl) return;
        lv->lines = nl;
        lv->cap = cap;
    }
    lv->lines[lv->n++] = strdup(line);
    if (lv->follow) lv->offset_from_bottom = 0;
}

int log_line_count(const LogView *lv)
{
    return lv->n;
}

const char *log_line(const LogView *lv, int i)
{
    if (i < 0 || i >= lv->n) return "";
    return lv->lines[i];
}

void draw_logview(Buf *b, LogView *lv, int y, int x, int h, int w)
{
    if (h <= 0 || w <= 0) return;

    int visible = h;
    int back = lv->offset_from_bottom;
    if (back < 0) back = 0;
    if (back > lv->n) back = lv->n;

    int start = lv->n - back - visible;
    if (start < 0) {
        /* Not enough history: pad the top so the tail stays at the bottom. */
        start = 0;
        visible = lv->n - back;
        if (visible < 0) visible = 0;
    }

    for (int row = 0; row < h; row++) {
        int idx = start + row;
        int yy = y + row;
        if (yy >= b->rows) break;
        if (idx < 0 || idx >= lv->n) {
            buf_puts(b, yy, x, COL_DIM, BG_DEFAULT, A_NONE, "");
            buf_fill(b, yy, x, 1, w, COL_DIM, BG_DEFAULT, A_NONE);
            continue;
        }
        const char *s = lv->lines[idx];
        int sev = ansi_severity(s);
        uint8_t col = COL_TEXT;
        if (sev == 3) col = COL_ERR;
        else if (sev == 2) col = COL_WARN;
        else if (sev == 1) col = COL_OK;

        if (sev >= 2) {
            /* Failed lines stand out from the surrounding output. */
            buf_fill(b, yy, x, 1, w, col, BG_DEFAULT, A_NONE);
            buf_puts(b, yy, x, col, BG_DEFAULT, A_BOLD, s);
        } else {
            buf_puts(b, yy, x, col, BG_DEFAULT, A_NONE, s);
        }
    }

    /* Scrollbar. */
    if (lv->n > visible && visible > 0) {
        int thumb = (visible * visible) / lv->n;
        if (thumb < 1) thumb = 1;
        int range = visible - thumb;
        int pos = (lv->n - back - visible <= 0) ? range : 0;
        if (range > 0) {
            int room = lv->n - visible;
            pos = room > 0 ? ((lv->n - back - visible) * range) / room : 0;
        }
        for (int row = 0; row < visible; row++) {
            int yy = y + row;
            if (yy >= b->rows) break;
            if (row >= pos && row < pos + thumb)
                buf_puts(b, yy, x + w - 1, COL_ACCENT, BG_DEFAULT, A_NONE, "\u2502");
            else
                buf_puts(b, yy, x + w - 1, COL_FRAME, BG_DEFAULT, A_NONE, "\u2502");
        }
    }
}

/* ---- prompt ---------------------------------------------------------- */

void prompt_begin(Prompt *p, const char *label, const char *hint, const char *initial)
{
    memset(p, 0, sizeof(*p));
    p->active = 1;
    snprintf(p->label, sizeof(p->label), "%s", label ? label : "");
    snprintf(p->hint, sizeof(p->hint), "%s", hint ? hint : "");
    if (initial) snprintf(p->text, sizeof(p->text), "%s", initial);
}

void prompt_end(Prompt *p)
{
    p->active = 0;
    p->text[0] = '\0';
}

int prompt_handle(Prompt *p, const Key *k)
{
    size_t len = strlen(p->text);
    switch (k->type) {
    case KEY_ESC:
        prompt_end(p);
        return -1;
    case KEY_ENTER:
        /* Only close the prompt: prompt_end() clears the text, and the caller
         * still has to read it to find out what was typed. */
        p->active = 0;
        return 1;
    case KEY_BACKSPACE:
        if (len > 0) {
            /* Step back over a full UTF-8 sequence. */
            size_t cut = len - 1;
            while (cut > 0 && ((unsigned char)p->text[cut] & 0xC0) == 0x80) cut--;
            p->text[cut] = '\0';
        }
        return 0;
    case KEY_CHAR: {
        size_t kl = strlen(k->ch);
        if (len + kl + 1 < PROMPT_MAX) {
            memcpy(p->text + len, k->ch, kl + 1);
        }
        return 0;
    }
    default:
        return 0;
    }
}

void draw_prompt(Buf *b, const Prompt *p, int y, int x, int w)
{
    if (!p->active || w < 8) return;
    buf_fill(b, y, x, 1, w, COL_KEY, 236, A_NONE);
    buf_printf(b, y, x, w, 245, BG_DEFAULT, A_BOLD, "%s", p->label);

    int label_w = str_width(p->label);
    int room = w - label_w - 2;
    if (room > 2) {
        int used = str_width(p->text);
        /* Scroll the field when it overflows. */
        int start = used > room - 1 ? used - (room - 1) : 0;
        buf_puts(b, y, x + label_w + 1, 231, BG_DEFAULT, A_BOLD, p->text + start);
        /* caret */
        int cx = x + label_w + 1 + (used - start);
        if (cx < x + w) buf_puts(b, y, cx, COL_KEY, BG_DEFAULT, A_REVERSE, " ");
    }
    (void)p->hint;
}

/* ---- confirm --------------------------------------------------------- */

void confirm_begin(Confirm *c, const char *action, const char *text)
{
    memset(c, 0, sizeof(*c));
    c->active = 1;
    snprintf(c->action, sizeof(c->action), "%s", action ? action : "confirmar");
    snprintf(c->text, sizeof(c->text), "%s", text ? text : "");
}

void confirm_end(Confirm *c)
{
    c->active = 0;
    c->choice = 0;
}

int confirm_handle(Confirm *c, const Key *k)
{
    if (k->type == KEY_LEFT || k->type == KEY_TAB || k->type == KEY_BACKTAB)
        c->choice = 0;
    else if (k->type == KEY_RIGHT)
        c->choice = 1;
    else if (k->type == KEY_CHAR) {
        if (k->ch[0] == 's' || k->ch[0] == 'S' || k->ch[0] == 'y' || k->ch[0] == 'Y') {
            confirm_end(c);
            return 1;
        }
        if (k->ch[0] == 'n' || k->ch[0] == 'N') {
            confirm_end(c);
            return -1;
        }
    } else if (k->type == KEY_ESC) {
        confirm_end(c);
        return -1;
    } else if (k->type == KEY_ENTER) {
        int yes = (c->choice == 1);
        confirm_end(c);
        return yes ? 1 : -1;
    }
    return 0;
}

void draw_confirm(Buf *b, const Confirm *c, int rows, int cols)
{
    if (!c->active) return;
    int w = str_width(c->text) + 8;
    if (w > cols - 4) w = cols - 4;
    if (w < 30) w = 30;
    int h = 6;
    int x = (cols - w) / 2;
    int y = (rows - h) / 2;
    if (y < 0) y = 0;

    buf_fill(b, y, x, h, w, COL_TEXT, 235, A_NONE);
    buf_box(b, y, x, h, w, COL_WARN, c->action);
    buf_puts(b, y + 2, x + 3, COL_TEXT, BG_DEFAULT, A_NONE, c->text);

    int by = y + 4;
    int bx = x + 3;
    if (c->choice == 0)
        buf_printf(b, by, bx, 20, COL_OK, 236, A_BOLD, " %s ", "sim");
    else
        buf_printf(b, by, bx, 20, COL_DIM, BG_DEFAULT, A_NONE, " %s ", "sim");

    if (c->choice == 1)
        buf_printf(b, by, bx + 10, 20, COL_ERR, 236, A_BOLD, " %s ", "nao");
    else
        buf_printf(b, by, bx + 10, 20, COL_DIM, BG_DEFAULT, A_NONE, " %s ", "nao");

    buf_printf(b, y + 2, x + 3, w - 6, COL_DIM, BG_DEFAULT, A_NONE,
               "\u2190 \u2192 escolher   Enter confirmar   Esc cancelar");
}

/* ---- misc ------------------------------------------------------------ */

void draw_kv(Buf *b, int y, int x, int w, const char *k, const char *v, uint8_t vcol)
{
    buf_printf(b, y, x, w, COL_DIM, BG_DEFAULT, A_NONE, "%s", k);
    buf_printf(b, y, x + 14, w - 14, vcol, BG_DEFAULT, A_NONE, "%s", v);
}

void draw_badge(Buf *b, int y, int x, const char *text, uint8_t fg, uint8_t bg)
{
    int w = str_width(text) + 2;
    buf_fill(b, y, x, 1, w, fg, bg, A_BOLD);
    buf_printf(b, y, x + 1, w, fg, BG_DEFAULT, A_BOLD, "%s", text);
}

void fmt_duration(char *out, size_t cap, long seconds)
{
    if (seconds < 0) { snprintf(out, cap, "--:--"); return; }
    if (seconds < 3600) snprintf(out, cap, "%ldm%02lds", seconds / 60, seconds % 60);
    else snprintf(out, cap, "%ldh%02ldm", seconds / 3600, (seconds % 3600) / 60);
}

void fmt_hhmmss(char *out, size_t cap, long epoch)
{
    time_t t = (time_t)epoch;
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) { snprintf(out, cap, "--:--:--"); return; }
    snprintf(out, cap, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

/* ---- list selection --------------------------------------------------- */

void list_clamp(ListSel *s, int n)
{
    if (s->sel < 0) s->sel = 0;
    if (n > 0 && s->sel >= n) s->sel = n - 1;
    if (s->sel < 0) s->sel = 0;
}

void list_move(ListSel *s, int n, int delta)
{
    if (n <= 0) { s->sel = 0; s->top = 0; return; }
    s->sel += delta;
    if (s->sel < 0) s->sel = 0;
    if (s->sel >= n) s->sel = n - 1;
}

void list_ensure_visible(ListSel *s, int n, int page)
{
    if (page <= 0) page = 1;
    if (s->sel < s->top) s->top = s->sel;
    if (s->sel >= s->top + page) s->top = s->sel - page + 1;
    if (s->top > n - page) s->top = n - page;
    if (s->top < 0) s->top = 0;
}
