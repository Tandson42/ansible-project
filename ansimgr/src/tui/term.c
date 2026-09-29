#include "tui/term.h"
#include "tui/theme.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_saved;
static int g_saved_ok = 0;
static int g_rows = 24, g_cols = 80;
static volatile sig_atomic_t g_winch = 0;
static char g_out[16384];
static size_t g_out_len = 0;

static void on_winch(int sig)
{
    (void)sig;
    g_winch = 1;
}

static void emit(const char *s, size_t n)
{
    if (g_out_len + n >= sizeof(g_out)) term_flush();
    if (n >= sizeof(g_out)) n = sizeof(g_out) - 1;
    memcpy(g_out + g_out_len, s, n);
    g_out_len += n;
}

void term_refresh_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        g_rows = ws.ws_row;
        g_cols = ws.ws_col;
    } else {
        g_rows = 24;
        g_cols = 80;
    }
}

int term_rows(void) { return g_rows; }
int term_cols(void) { return g_cols; }

int term_resized(void)
{
    if (g_winch) {
        g_winch = 0;
        return 1;
    }
    return 0;
}

void term_init(void)
{
    struct termios t;

    if (tcgetattr(STDIN_FILENO, &g_saved) == 0) {
        g_saved_ok = 1;
        t = g_saved;
        t.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG | IEXTEN);
        t.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        t.c_oflag &= (tcflag_t)~(OPOST);
        t.c_cflag |= CS8;
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }

    term_refresh_size();
    signal(SIGWINCH, on_winch);
    signal(SIGPIPE, SIG_IGN);

    emit("\033[?1049h", 8);   /* alternate screen */
    emit("\033[?25l", 6);     /* hide cursor */
    emit("\033[2J", 4);       /* clear */
    emit("\033[H", 3);
    term_flush();
}

void term_shutdown(void)
{
    term_style(FG_DEFAULT, BG_DEFAULT, A_NONE);
    emit("\033[0m", 4);
    emit("\033[?25h", 6);     /* show cursor */
    emit("\033[?1049l", 8);   /* leave alternate screen */
    term_flush();
    if (g_saved_ok) tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
}

void term_move(int y, int x)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "\033[%d;%dH", y + 1, x + 1);
    if (n > 0) emit(tmp, (size_t)n);
}

void term_style(uint8_t fg, uint8_t bg, uint16_t attr)
{
    char tmp[48];
    int n = snprintf(tmp, sizeof(tmp), "\033[0");
    if (attr & A_BOLD) n += snprintf(tmp + n, sizeof(tmp) - (size_t)n, ";1");
    if (attr & A_DIM) n += snprintf(tmp + n, sizeof(tmp) - (size_t)n, ";2");
    if (attr & A_UNDERLINE) n += snprintf(tmp + n, sizeof(tmp) - (size_t)n, ";4");
    if (attr & A_REVERSE) n += snprintf(tmp + n, sizeof(tmp) - (size_t)n, ";7");
    if (fg != FG_DEFAULT) n += snprintf(tmp + n, sizeof(tmp) - (size_t)n, ";38;5;%u", fg);
    if (bg != BG_DEFAULT) n += snprintf(tmp + n, sizeof(tmp) - (size_t)n, ";48;5;%u", bg);
    n += snprintf(tmp + n, sizeof(tmp) - (size_t)n, "m");
    if (n > 0) emit(tmp, (size_t)n);
}

void term_write(const char *s, size_t n)
{
    emit(s, n);
}

void term_puts(const char *s)
{
    emit(s, strlen(s));
}

void term_flush(void)
{
    size_t off = 0;
    while (off < g_out_len) {
        ssize_t w = write(STDOUT_FILENO, g_out + off, g_out_len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (w == 0) break;
        off += (size_t)w;
    }
    g_out_len = 0;
}

void term_clear_screen(void)
{
    emit("\033[2J", 4);
    emit("\033[H", 3);
}

void term_reset(void)
{
    emit("\033[0m", 4);
    emit("\033[2J", 4);
    emit("\033[H", 3);
}

void term_hide_cursor(int hide)
{
    emit(hide ? "\033[?25l" : "\033[?25h", 6);
}
