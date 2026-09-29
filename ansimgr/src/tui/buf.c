#include "tui/buf.h"
#include "tui/term.h"
#include "tui/theme.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Cell cell_blank(uint8_t fg, uint8_t bg, uint16_t attr)
{
    Cell c;
    c.ch[0] = ' ';
    c.ch[1] = c.ch[2] = c.ch[3] = c.ch[4] = '\0';
    c.fg = fg;
    c.bg = bg;
    c.attr = attr;
    return c;
}

int buf_init(Buf *b, int rows, int cols)
{
    memset(b, 0, sizeof(*b));
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    b->rows = rows;
    b->cols = cols;
    b->cells = calloc((size_t)rows * cols, sizeof(Cell));
    b->back = calloc((size_t)rows * cols, sizeof(Cell));
    if (!b->cells || !b->back) return -1;
    for (int i = 0; i < rows * cols; i++) {
        b->cells[i] = cell_blank(FG_DEFAULT, BG_DEFAULT, A_NONE);
        b->back[i] = cell_blank(FG_DEFAULT, BG_DEFAULT, A_NONE);
    }
    return 0;
}

void buf_free(Buf *b)
{
    free(b->cells);
    free(b->back);
    memset(b, 0, sizeof(*b));
}

int buf_resize(Buf *b, int rows, int cols)
{
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    if (rows == b->rows && cols == b->cols) return 0;
    free(b->cells);
    free(b->back);
    return buf_init(b, rows, cols);
}

void buf_clear(Buf *b, uint8_t fg, uint8_t bg)
{
    Cell c = cell_blank(fg, bg, A_NONE);
    for (int i = 0; i < b->rows * b->cols; i++) b->cells[i] = c;
}

void buf_fill(Buf *b, int y, int x, int h, int w, uint8_t fg, uint8_t bg, uint16_t attr)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= b->rows) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= b->cols) continue;
            b->cells[yy * b->cols + xx] = cell_blank(fg, bg, attr);
        }
    }
}

int utf8_next(const char *s, size_t len, size_t *pos)
{
    size_t p = *pos;
    if (p >= len) return 0;
    unsigned char c = (unsigned char)s[p];
    int n = 1;
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    if (p + (size_t)n > len) n = 1;
    /* Reject obviously invalid continuations; fall back to a single byte. */
    for (int k = 1; k < n; k++) {
        if (((unsigned char)s[p + k] & 0xC0) != 0x80) { n = 1; break; }
    }
    *pos = p + (size_t)n;
    return n;
}

int str_width(const char *s)
{
    size_t len = strlen(s), p = 0;
    int w = 0;
    while (p < len) {
        if (s[p] == '\033') { /* skip CSI/SGR */
            p++;
            if (p < len && s[p] == '[') {
                p++;
                while (p < len && !(s[p] >= '@' && s[p] <= '~')) p++;
                if (p < len) p++;
            }
            continue;
        }
        utf8_next(s, len, &p);
        w++;
    }
    return w;
}

void buf_putc(Buf *b, int y, int x, uint8_t fg, uint8_t bg, uint16_t attr, const char *utf8)
{
    if (y < 0 || y >= b->rows || x < 0 || x >= b->cols) return;
    int n = (int)strlen(utf8);
    if (n > 4) n = 4;
    if (n < 1) n = 1;
    Cell *c = &b->cells[y * b->cols + x];
    memcpy(c->ch, utf8, (size_t)n);
    c->ch[n] = '\0';
    c->fg = fg;
    c->bg = bg;
    c->attr = attr;
}

void buf_puts(Buf *b, int y, int x, uint8_t fg, uint8_t bg, uint16_t attr, const char *s)
{
    if (y < 0 || y >= b->rows) return;
    size_t len = strlen(s), p = 0;
    int xx = x;
    while (p < len) {
        if (s[p] == '\033') {
            size_t q = p + 1;
            if (q < len && s[q] == '[') {
                q++;
                while (q < len && !(s[q] >= '@' && s[q] <= '~')) q++;
                if (q < len) q++;
            }
            p = q;
            continue;
        }
        size_t start = p;
        utf8_next(s, len, &p);
        if (xx >= b->cols) return;
        int n = (int)(p - start);
        if (n > 4) n = 4;
        if (n < 1) n = 1;
        Cell *c = &b->cells[y * b->cols + xx];
        memcpy(c->ch, s + start, (size_t)n);
        c->ch[n] = '\0';
        c->fg = fg;
        c->bg = bg;
        c->attr = attr;
        xx++;
    }
}

void buf_printf(Buf *b, int y, int x, int maxw, uint8_t fg, uint8_t bg, uint16_t attr,
                const char *fmt, ...)
{
    char stackbuf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    /* maxw counts codepoints, not bytes, so the walk below is the only reliable
     * bound: a byte-length shortcut would both over- and under-truncate. */
    size_t len = strlen(stackbuf);
    if (maxw > 0) {
        /* Stop on a codepoint boundary so a multi-byte glyph is never split. */
        size_t cut = 0, acc = 0;
        while (cut < len && acc < (size_t)maxw) {
            size_t next = cut;
            utf8_next(stackbuf, len, &next);
            cut = next;
            acc++;
        }
        stackbuf[cut] = '\0';
    }
    buf_puts(b, y, x, fg, bg, attr, stackbuf);
}

static void repeat_puts(Buf *b, int y, int x, int w, uint8_t fg, uint8_t bg,
                        uint16_t attr, const char *glyph)
{
    for (int i = 0; i < w; i++) buf_puts(b, y, x + i, fg, bg, attr, glyph);
}

void buf_hline(Buf *b, int y, uint8_t fg, const char *glyph)
{
    repeat_puts(b, y, 0, b->cols, fg, BG_DEFAULT, A_NONE, glyph);
}

void buf_vline(Buf *b, int y, int h, int x, uint8_t fg, const char *glyph)
{
    for (int i = 0; i < h; i++) repeat_puts(b, y + i, x, 1, fg, BG_DEFAULT, A_NONE, glyph);
}

void buf_box(Buf *b, int y, int x, int h, int w, uint8_t fg, const char *title)
{
    if (h < 2 || w < 2) return;
    buf_puts(b, y, x, fg, BG_DEFAULT, A_NONE, "\u250c");
    buf_puts(b, y, x + w - 1, fg, BG_DEFAULT, A_NONE, "\u2510");
    buf_puts(b, y + h - 1, x, fg, BG_DEFAULT, A_NONE, "\u2514");
    buf_puts(b, y + h - 1, x + w - 1, fg, BG_DEFAULT, A_NONE, "\u2518");
    for (int i = 1; i < w - 1; i++) {
        buf_puts(b, y, x + i, fg, BG_DEFAULT, A_NONE, "\u2500");
        buf_puts(b, y + h - 1, x + i, fg, BG_DEFAULT, A_NONE, "\u2500");
    }
    for (int j = 1; j < h - 1; j++) {
        buf_puts(b, y + j, x, fg, BG_DEFAULT, A_NONE, "\u2502");
        buf_puts(b, y + j, x + w - 1, fg, BG_DEFAULT, A_NONE, "\u2502");
    }
    if (title && *title && w > 6) {
        char label[128];
        snprintf(label, sizeof(label), " %s ", title);
        int room = w - 4;
        if ((int)str_width(label) > room) {
            /* Cut on a codepoint boundary, then mark the cut with "..". */
            int keep = room - 2;
            if (keep < 0) keep = 0;
            size_t len = strlen(label), cut = 0;
            int acc = 0;
            while (cut < len && acc < keep) {
                size_t next = cut;
                utf8_next(label, len, &next);
                cut = next;
                acc++;
            }
            label[cut] = '\0';
            strncat(label, "..", sizeof(label) - strlen(label) - 1);
        }
        buf_puts(b, y, x + 2, fg, BG_DEFAULT, A_NONE, label);
    }
}

void buf_render(Buf *b)
{
    int cur_y = -1, cur_x = -1;
    uint16_t cur_attr = 0xFFFF;
    uint8_t cur_fg = 0xFF, cur_bg = 0xFF;
    int emitted = 0;

    for (int y = 0; y < b->rows; y++) {
        for (int x = 0; x < b->cols; x++) {
            int i = y * b->cols + x;
            Cell *f = &b->cells[i];
            Cell *k = &b->back[i];
            if (f->ch[0] == k->ch[0] && f->ch[1] == k->ch[1] && f->ch[2] == k->ch[2] &&
                f->ch[3] == k->ch[3] && f->attr == k->attr && f->fg == k->fg && f->bg == k->bg)
                continue;

            if (cur_y != y || cur_x != x) term_move(y, x);
            if (f->attr != cur_attr || f->fg != cur_fg || f->bg != cur_bg)
                term_style(f->fg, f->bg, f->attr);
            term_write(f->ch, strlen(f->ch));
            *k = *f;
            cur_y = y;
            /* One cell is exactly one codepoint, i.e. one column. Using the
             * byte length here would skip the next term_move after any
             * multi-byte glyph and land the text in the wrong column. */
            cur_x = x + 1;
            emitted = 1;
        }
    }
    if (emitted) {
        term_style(FG_DEFAULT, BG_DEFAULT, A_NONE);
        term_flush();
    }
}
