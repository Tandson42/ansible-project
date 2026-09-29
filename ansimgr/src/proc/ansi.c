#include "proc/ansi.h"
#include "tui/buf.h"

#include <string.h>

/* Returns bytes consumed by the escape sequence starting at s[i] == 0x1b. */
static size_t esc_len(const char *s, size_t len, size_t i)
{
    size_t p = i + 1;
    if (p >= len) return 1;
    if (s[p] == '[') { /* CSI: params then a final byte in @..~ */
        p++;
        while (p < len && !((unsigned char)s[p] >= '@' && (unsigned char)s[p] <= '~')) p++;
        if (p < len) p++;
        return p - i;
    }
    if (s[p] == ']') { /* OSC: terminated by BEL or ST */
        p++;
        while (p < len && s[p] != '\007') {
            if (s[p] == '\033' && p + 1 < len && s[p + 1] == '\\') { p += 2; break; }
            p++;
        }
        if (p < len && s[p] == '\007') p++;
        return p - i;
    }
    return 2; /* two-character escape */
}

size_t ansi_strip(const char *in, size_t inlen, char *out, size_t outcap)
{
    size_t i = 0, o = 0;
    while (i < inlen && o + 1 < outcap) {
        if (in[i] == 0x1b) {
            i += esc_len(in, inlen, i);
            continue;
        }
        if (in[i] == '\r' || in[i] == '\n' || in[i] == '\007') { i++; continue; }
        out[o++] = in[i++];
    }
    out[o] = '\0';
    return o;
}

size_t ansi_clean(const char *in, size_t inlen, char *out, size_t outcap)
{
    size_t i = 0, o = 0;
    int last_space = 0;
    int started = 0;
    while (i < inlen && o + 1 < outcap) {
        if (in[i] == 0x1b) { i += esc_len(in, inlen, i); continue; }
        char c = in[i++];
        if (c == '\r' || c == '\n' || c == '\007') continue;
        if (c == ' ' || c == '\t') {
            if (started) { last_space = 1; }
            continue;
        }
        if (last_space) { out[o++] = ' '; last_space = 0; }
        out[o++] = c;
        started = 1;
    }
    if (last_space && started && o + 1 < outcap) out[o++] = ' ';
    out[o] = '\0';
    return o;
}

static int contains(const char *hay, const char *needle)
{
    return hay && strstr(hay, needle) != NULL;
}

/* Read the integer that follows "field=" in a PLAY RECAP line, or -1. */
static int field_count(const char *s, const char *field)
{
    const char *p = strstr(s, field);
    if (!p) return -1;
    p += strlen(field);
    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return -1;
    int v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v;
}

int ansi_severity(const char *s)
{
    if (!s) return 0;
    if (contains(s, "fatal:") || contains(s, "FAILED!") || contains(s, "ERROR!"))
        return 3;
    if (contains(s, "ok=") || contains(s, "changed=")) {
        int failed = field_count(s, "failed=");
        int unreach = field_count(s, "unreachable=");
        if (failed > 0 || unreach > 0) return 3;
        int changed = field_count(s, "changed=");
        if (changed > 0) return 2;
        return 1;
    }
    if (contains(s, "WARNING")) return 2;
    return 0;
}
