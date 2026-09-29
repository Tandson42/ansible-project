#ifndef ANSIMGR_BUF_H
#define ANSIMGR_BUF_H

#include <stdint.h>
#include <stddef.h>

/*
 * A double-buffered cell grid. Views paint into the front buffer; render()
 * diffs it against the back buffer and emits the minimal escape sequence
 * stream, which avoids flicker and keeps cursor motion bounded.
 */

typedef struct {
    char ch[5]; /* UTF-8 sequence, NUL terminated (1..4 bytes) */
    uint16_t attr;
    uint8_t fg;
    uint8_t bg;
} Cell;

typedef struct {
    int rows;
    int cols;
    Cell *cells; /* front buffer being painted */
    Cell *back;  /* what is actually on screen */
} Buf;

int buf_init(Buf *b, int rows, int cols);
void buf_free(Buf *b);
int buf_resize(Buf *b, int rows, int cols);
void buf_clear(Buf *b, uint8_t fg, uint8_t bg);

void buf_fill(Buf *b, int y, int x, int h, int w, uint8_t fg, uint8_t bg, uint16_t attr);
void buf_puts(Buf *b, int y, int x, uint8_t fg, uint8_t bg, uint16_t attr, const char *s);
void buf_putc(Buf *b, int y, int x, uint8_t fg, uint8_t bg, uint16_t attr, const char *utf8);
void buf_printf(Buf *b, int y, int x, int maxw, uint8_t fg, uint8_t bg, uint16_t attr,
                const char *fmt, ...) __attribute__((format(printf, 8, 9)));

/* Horizontal rule spanning the full width. */
void buf_hline(Buf *b, int y, uint8_t fg, const char *glyph);
/* Vertical rule. */
void buf_vline(Buf *b, int y, int h, int x, uint8_t fg, const char *glyph);
/* Box border. */
void buf_box(Buf *b, int y, int x, int h, int w, uint8_t fg, const char *title);

/* Visible width of a UTF-8 string, ignoring ANSI SGR sequences. */
int str_width(const char *s);
/* Decode one UTF-8 codepoint; advances *pos. Returns bytes consumed (>=1). */
int utf8_next(const char *s, size_t len, size_t *pos);

void buf_render(Buf *b);

#endif /* ANSIMGR_BUF_H */
