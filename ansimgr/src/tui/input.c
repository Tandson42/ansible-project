#include "tui/input.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#define IBUF 1024
#define ESC_TIMEOUT 40

static int g_fd = 0;
static char g_buf[IBUF];
static int g_len = 0;
static int g_pos = 0;

/* Returns 1 if at least one byte is available (blocking up to timeout_ms). */
static int fill(int timeout_ms)
{
    if (g_pos < g_len) return 1;
    g_len = g_pos = 0;
    struct pollfd p = { .fd = g_fd, .events = POLLIN, .revents = 0 };
    int r = poll(&p, 1, timeout_ms);
    if (r <= 0) return 0;
    ssize_t n = read(g_fd, g_buf, sizeof(g_buf));
    if (n <= 0) {
        if (n < 0 && errno == EINTR) return 0;
        return 0;
    }
    g_len = (int)n;
    g_pos = 0;
    return 1;
}

void input_init(int fd)
{
    g_fd = fd;
    g_len = g_pos = 0;
}

static Key mk(KeyType t, const char *s)
{
    Key k;
    memset(&k, 0, sizeof(k));
    k.type = t;
    if (s) {
        size_t n = strlen(s);
        if (n > 4) n = 4;
        memcpy(k.ch, s, n);
    }
    return k;
}

/* Consume `n` bytes if available, else return 0. */
static int have(int n)
{
    return (g_len - g_pos) >= n;
}

static int peek(int off)
{
    int i = g_pos + off;
    return (i < g_len) ? (unsigned char)g_buf[i] : -1;
}

static int csi_tilde(int final)
{
    /* \033 [ <digits> ~  */
    int i = 1, val = -1, digits = 0;
    while (have(i)) {
        int c = peek(i);
        if (c >= '0' && c <= '9') {
            if (val < 0) val = 0;
            val = val * 10 + (c - '0');
            digits++;
            i++;
            continue;
        }
        if (c == final) { i++; break; }
        return -1; /* not a tilde sequence */
    }
    if (!have(i) || digits == 0) return -1;
    g_pos += i;
    return val;
}

Key input_next(int timeout_ms)
{
    if (!fill(timeout_ms)) return mk(KEY_NONE, NULL);

    int c = peek(0);

    if (c == 0x1b) {
        if (!have(2)) {
            /* Might be a bare ESC or the start of a sequence. Wait a little. */
            struct pollfd p = { .fd = g_fd, .events = POLLIN, .revents = 0 };
            if (poll(&p, 1, ESC_TIMEOUT) > 0) {
                ssize_t n = read(g_fd, g_buf + g_len, (size_t)(IBUF - g_len));
                if (n > 0) g_len += (int)n;
            }
        }
        if (!have(2)) { g_pos++; return mk(KEY_ESC, NULL); }

        if (peek(1) == '[' || peek(1) == 'O') {
            g_pos += 2;

            static const struct { const char *seq; KeyType t; } direct[] = {
                { "A", KEY_UP },    { "B", KEY_DOWN },  { "C", KEY_RIGHT },
                { "D", KEY_LEFT },  { "H", KEY_HOME },  { "F", KEY_END },
                { "Z", KEY_BACKTAB },
            };
            for (size_t i = 0; i < sizeof(direct) / sizeof(direct[0]); i++) {
                char one = direct[i].seq[0];
                if (have(1) && peek(0) == one) {
                    g_pos++;
                    return mk(direct[i].t, 0);
                }
            }

            int v = csi_tilde('~');
            if (v > 0) {
                switch (v) {
                case 2: return mk(KEY_INSERT, NULL);
                case 3: return mk(KEY_DELETE, NULL);
                case 5: return mk(KEY_PGUP, NULL);
                case 6: return mk(KEY_PGDN, NULL);
                case 1: case 7: return mk(KEY_HOME, NULL);
                case 4: case 8: return mk(KEY_END, NULL);
                case 11: return mk(KEY_F1, NULL);
                case 12: return mk(KEY_F2, NULL);
                case 13: return mk(KEY_F3, NULL);
                case 14: return mk(KEY_F4, NULL);
                case 15: return mk(KEY_F5, NULL);
                case 17: return mk(KEY_F6, NULL);
                case 18: return mk(KEY_F7, NULL);
                case 19: return mk(KEY_F8, NULL);
                case 20: return mk(KEY_F9, NULL);
                case 21: return mk(KEY_F10, NULL);
                case 23: return mk(KEY_F11, NULL);
                case 24: return mk(KEY_F12, NULL);
                default: break;
                }
            }
            /* Unknown CSI: drop what we consumed. */
            return mk(KEY_NONE, NULL);
        }
        g_pos++;
        return mk(KEY_ESC, NULL);
    }

    g_pos++;
    switch (c) {
    case '\r':
    case '\n': return mk(KEY_ENTER, NULL);
    case '\t': return mk(KEY_TAB, NULL);
    case 0x7f:
    case 0x08: return mk(KEY_BACKSPACE, NULL);
    default:
        if (c < 0x20) return mk(KEY_CHAR, (char[]){ (char)(c + 'a' - 1), 0 });
        if (c < 0x80) return mk(KEY_CHAR, (char[]){ (char)c, 0 });
        /* Multi-byte: gather the full sequence so the view sees one glyph. */
        {
            char mb[5] = { (char)c, 0, 0, 0, 0 };
            int n = 1;
            int need = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
            while (n < need && have(1)) {
                int nx = peek(0);
                if (nx < 0) break;
                mb[n++] = (char)nx;
                g_pos++;
            }
            mb[n] = '\0';
            return mk(KEY_CHAR, mb);
        }
    }
}
