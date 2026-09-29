#ifndef ANSIMGR_TERM_H
#define ANSIMGR_TERM_H

#include <stdint.h>
#include <stddef.h>

void term_init(void);
void term_shutdown(void);
int  term_rows(void);
int  term_cols(void);
void term_refresh_size(void);
int  term_resized(void); /* consumes the SIGWINCH flag */

/* Absolute cursor placement, 0-based. */
void term_move(int y, int x);
void term_style(uint8_t fg, uint8_t bg, uint16_t attr);
void term_write(const char *s, size_t n);
void term_puts(const char *s);
void term_flush(void);

void term_clear_screen(void);
void term_reset(void);
void term_hide_cursor(int hide);

#endif /* ANSIMGR_TERM_H */
