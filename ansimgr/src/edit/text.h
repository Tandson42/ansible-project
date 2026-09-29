#ifndef ANSIMGR_TEXT_H
#define ANSIMGR_TEXT_H

#include <stddef.h>

/*
 * Non-destructive line iteration over a memory buffer. strtok_r() cannot be
 * used here because the buffers are rewritten verbatim afterwards, and the
 * NUL bytes it leaves behind would corrupt the file on write.
 */

typedef struct {
    const char *buf;
    size_t len;
    size_t pos;   /* cursor: byte offset of the next line */
} LineIter;

typedef struct {
    size_t start; /* byte offset of the line start (including its indent) */
    size_t end;   /* byte offset just past the line's last visible byte */
    size_t next;  /* byte offset of the next line */
    char text[1024];
    int  has_nl;  /* 1 if the line was terminated by '\n' */
} Line;

void line_iter_init(LineIter *it, const char *buf, size_t len);
int  line_next(LineIter *it, Line *out); /* 1 = line produced, 0 = end */

char *text_trim(char *s);
void  text_copy_trim(char *dst, size_t cap, const char *src, size_t n);

#endif /* ANSIMGR_TEXT_H */
