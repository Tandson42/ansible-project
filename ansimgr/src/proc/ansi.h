#ifndef ANSIMGR_ANSI_H
#define ANSIMGR_ANSI_H

#include <stddef.h>

/*
 * Ansible colours its output and rewrites lines with \r. The log view needs
 * the text with the escape noise removed so that column maths is correct.
 */

/* Copy `in` to `out` stripping CSI/OSC sequences and carriage returns.
 * Returns the number of bytes written (excluding NUL). Truncates safely. */
size_t ansi_strip(const char *in, size_t inlen, char *out, size_t outcap);

/* Copy stripping sequences and collapsing runs of blanks. */
size_t ansi_clean(const char *in, size_t inlen, char *out, size_t outcap);

/* Heuristic severity for a log line: 0 normal, 1 success, 2 warning, 3 error. */
int ansi_severity(const char *s);

#endif /* ANSIMGR_ANSI_H */
