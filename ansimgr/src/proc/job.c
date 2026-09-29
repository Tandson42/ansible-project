#include "proc/job.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define LINE_MAX 8192

struct Job {
    pid_t pid;
    int master;
    int status;
    int term_status;
    int running;
    int reaped;
    char *acc;      /* partial line accumulator */
    size_t acc_len;
    size_t acc_cap;
    char title[128];
    JobLineFn on_line;
    JobExitFn on_exit;
    void *ud;
    int pending_cr;  /* a bare '\r' was seen; its meaning depends on the next byte */
};

static void acc_reset(Job *j)
{
    j->acc_len = 0;
    if (j->acc) j->acc[0] = '\0';
}

static void acc_push(Job *j, const char *s, size_t n)
{
    if (j->acc_len + n + 1 > j->acc_cap) {
        size_t cap = j->acc_cap ? j->acc_cap : 1024;
        while (cap < j->acc_len + n + 1) cap *= 2;
        if (cap > 1u << 20) {
            /* Flush the oldest half rather than growing without bound. */
            memmove(j->acc, j->acc + j->acc_len / 2, j->acc_len - j->acc_len / 2);
            j->acc_len -= j->acc_len / 2;
        } else {
            char *nb = realloc(j->acc, cap);
            if (!nb) return;
            j->acc = nb;
            j->acc_cap = cap;
        }
    }
    memcpy(j->acc + j->acc_len, s, n);
    j->acc_len += n;
    j->acc[j->acc_len] = '\0';
}

static void acc_emit(Job *j)
{
    /* A '\r' that was never followed by anything means the program was
     * redrawing a line and stopped there: the partial text is not a line. */
    if (j->pending_cr) {
        j->pending_cr = 0;
        acc_reset(j);
        return;
    }
    if (j->acc_len == 0) return;
    if (j->on_line) j->on_line(j->ud, j->acc, j->acc_len);
    acc_reset(j);
}

/* Split a raw chunk into logical lines.
 *
 * A pty in cooked mode turns every '\n' into "\r\n" (ONLCR), so a '\r' is only
 * a repaint request when it is NOT followed by '\n'. Treating every '\r' as a
 * repaint throws the whole line away and then emits nothing. The decision is
 * deferred by one byte because a chunk boundary can fall between the two. */
static void feed(Job *j, const char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = buf[i];

        if (j->pending_cr) {
            /* "\r\r\n" happens when a program writes "\r\n" itself and the
             * pty adds its own CR: still just a line ending. */
            if (c == '\r') continue;
            j->pending_cr = 0;
            if (c == '\n') { acc_emit(j); continue; }
            acc_reset(j);  /* genuine repaint: the text so far is gone */
        }

        if (c == '\r') {
            j->pending_cr = 1;
        } else if (c == '\n') {
            acc_emit(j);
        } else if (c == '\007') {
            /* Bell. */
        } else {
            acc_push(j, &c, 1);
        }
    }
}

Job *job_start(char *const argv[], const char *cwd, int cols, int rows,
               JobLineFn on_line, JobExitFn on_exit, void *ud)
{
    int master = -1, slave = -1;
    struct winsize ws;

    Job *j = calloc(1, sizeof(*j));
    if (!j) return NULL;
    j->master = -1;
    j->on_line = on_line;
    j->on_exit = on_exit;
    j->ud = ud;

    if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
        free(j);
        return NULL;
    }
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)(cols > 0 ? cols : 80);
    ws.ws_row = (unsigned short)(rows > 0 ? rows : 24);
    ioctl(master, TIOCSWINSZ, &ws);

    pid_t pid = fork();
    if (pid < 0) {
        close(master);
        close(slave);
        free(j);
        return NULL;
    }

    if (pid == 0) {
        /* Child: become a session leader owning the pty as controlling terminal. */
        close(master);
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        if (cwd && *cwd && chdir(cwd) != 0) _exit(127);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) close(slave);
        /* Keep going when the pty master goes away. */
        signal(SIGPIPE, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(slave);
    int fl = fcntl(master, F_GETFL, 0);
    if (fl >= 0) fcntl(master, F_SETFL, fl | O_NONBLOCK);
    j->pid = pid;
    j->master = master;
    j->running = 1;
    return j;
}

int job_fd(const Job *j)
{
    if (!j) return -1;
    return j->running ? j->master : -1;
}

int job_running(const Job *j)
{
    return j && j->running;
}

int job_pid(const Job *j)
{
    return j ? (int)j->pid : -1;
}

void job_drain(Job *j)
{
    if (!j || j->master < 0) return;
    char buf[4096];
    for (;;) {
        ssize_t n = read(j->master, buf, sizeof(buf));
        if (n > 0) {
            feed(j, buf, (size_t)n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n < 0 && errno == EINTR) continue;
        break; /* EOF */
    }
    if (j->acc_len) acc_emit(j);
}

void job_cancel(Job *j)
{
    if (!j || !j->running || j->pid <= 0) return;
    /* Deliver to the whole group: ansible-playbook fans out over ssh. */
    kill(-j->pid, SIGINT);
    kill(j->pid, SIGINT);
}

void job_poll(Job *j)
{
    if (!j || !j->running) return;
    int st = 0;
    pid_t r = waitpid(j->pid, &st, WNOHANG);
    if (r != j->pid) return;

    j->running = 0;
    if (WIFEXITED(st)) j->status = WEXITSTATUS(st);
    else if (WIFSIGNALED(st)) j->status = 128 + WTERMSIG(st);
    else j->status = -1;
    j->term_status = (int)st;
    if (j->acc_len) acc_emit(j);
    if (j->on_exit) j->on_exit(j->ud, j->status, j->term_status);
}

void job_free(Job *j)
{
    if (!j) return;
    if (j->running && j->pid > 0) {
        kill(-j->pid, SIGKILL);
        kill(j->pid, SIGKILL);
        int st;
        waitpid(j->pid, &st, 0);
    }
    if (j->master >= 0) close(j->master);
    free(j->acc);
    free(j);
}

const char *job_title(const Job *j)
{
    return j ? j->title : "";
}

void job_set_title(Job *j, const char *t)
{
    if (!j || !t) return;
    snprintf(j->title, sizeof(j->title), "%s", t);
}
