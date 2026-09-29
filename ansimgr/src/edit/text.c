#include "edit/text.h"

#include <ctype.h>
#include <string.h>

void line_iter_init(LineIter *it, const char *buf, size_t len)
{
    it->buf = buf;
    it->len = len;
    it->pos = 0;
}

int line_next(LineIter *it, Line *out)
{
    if (it->pos >= it->len) return 0;

    size_t start = it->pos;
    size_t p = start;
    while (p < it->len && it->buf[p] != '\n') p++;

    size_t raw = p - start;
    /* Tolerate CRLF input. */
    if (raw > 0 && it->buf[start + raw - 1] == '\r') raw--;

    out->start = start;
    out->end = start + raw;
    out->has_nl = (p < it->len);
    out->next = (p < it->len) ? p + 1 : it->len;

    text_copy_trim(out->text, sizeof(out->text), it->buf + start, raw);

    it->pos = out->next;
    return 1;
}

char *text_trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    *e = '\0';
    return s;
}

void text_copy_trim(char *dst, size_t cap, const char *src, size_t n)
{
    if (cap == 0) return;
    size_t s = 0;
    while (s < n && (src[s] == ' ' || src[s] == '\t')) s++;
    size_t e = n;
    while (e > s && (src[e - 1] == ' ' || src[e - 1] == '\t' || src[e - 1] == '\r')) e--;
    size_t o = 0;
    while (s < e && o + 1 < cap) dst[o++] = src[s++];
    dst[o] = '\0';
}
