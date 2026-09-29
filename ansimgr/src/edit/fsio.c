#include "edit/fsio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int fs_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int fs_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int fs_mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t n = strlen(tmp);
    if (n == 0) return -1;
    if (tmp[n - 1] == '/') tmp[n - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

int fs_read_file(const char *path, char **out, size_t *len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }
    size_t cap = (size_t)st.st_size + 1;
    char *buf = malloc(cap);
    if (!buf) { close(fd); return -1; }

    size_t got = 0;
    for (;;) {
        ssize_t n = read(fd, buf + got, cap - got - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf);
            close(fd);
            return -1;
        }
        if (n == 0) break;
        got += (size_t)n;
        if (got + 1 >= cap) {
            cap = cap * 2 + 1;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); close(fd); return -1; }
            buf = nb;
        }
    }
    buf[got] = '\0';
    close(fd);
    if (out) *out = buf; else free(buf);
    if (len) *len = got;
    return 0;
}

int fs_write_atomic(const char *path, const char *data, size_t len)
{
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp.%d", path, (int)getpid());

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp);
            return -1;
        }
        off += (size_t)n;
    }
    if (fsync(fd) != 0) {
        close(fd);
        unlink(tmp);
        return -1;
    }
    /* Preserve the original mode if the file already existed. */
    struct stat st;
    if (stat(path, &st) == 0) fchmod(fd, st.st_mode & 07777);
    if (close(fd) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

int fs_backup(const char *src, const char *dir, char *out_path, size_t outcap)
{
    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(src, &text, &len) != 0) return -1;
    if (fs_mkdir_p(dir) != 0) { free(text); return -1; }

    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);

    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;

    int rc = -1;
    for (int attempt = 0; attempt < 1000 && rc != 0; attempt++) {
        char dest[700];
        if (attempt == 0)
            snprintf(dest, sizeof(dest), "%s/%s.%s", dir, base, stamp);
        else
            snprintf(dest, sizeof(dest), "%s/%s.%s-%03d", dir, base, stamp, attempt);
        if (fs_exists(dest)) continue;
        rc = fs_write_atomic(dest, text, len);
        if (rc == 0 && out_path) snprintf(out_path, outcap, "%s", dest);
    }
    free(text);
    return rc;
}
