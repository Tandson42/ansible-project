#include "model/vmstat.h"
#include "edit/fsio.h"
#include "edit/text.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*
 * Everything here comes from procfs, so the status view has no dependency on
 * external tools and stays usable even when the PATH is minimal.
 */

static long now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec;
}

/*
 * Column layout of /proc/net/tcp and /proc/net/tcp6 (space separated):
 *
 *   sl  local_address rem_address  st  tx_queue:rx_queue ...
 *   0:  0100007F:AE95 00000000:0000 0A ...
 *
 * The leading "sl" counter is itself "N:" and contains a colon, so it must be
 * skipped before looking for the "ADDR:PORT" pair. The socket state is the 4th
 * field; 0A means LISTEN.
 */
static int net_tcp_line(const char *line, int *port)
{
    const char *p = line;
    int field = 0;

    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        const char *tok = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t tlen = (size_t)(p - tok);

        if (field == 1) {                      /* local_address */
            const char *colon = tok + tlen;
            while (colon > tok && colon[-1] != ':') colon--;
            if (colon <= tok) return 0;
            *port = (int)strtol(colon, NULL, 16);
        } else if (field == 3) {               /* st */
            return tlen == 2 && tok[0] == '0' && tok[1] == 'A';
        }
        field++;
    }
    return 0;
}

int port_is_listening(int port)
{
    static const char *files[] = { "/proc/net/tcp", "/proc/net/tcp6" };

    for (size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
        char *text = NULL;
        size_t len = 0;
        if (fs_read_file(files[f], &text, &len) != 0) continue;

        LineIter it;
        Line ln;
        line_iter_init(&it, text, len);
        int found = 0;
        while (line_next(&it, &ln)) {
            /* The first line is the column header ("sl local_address ..."). */
            if (strncmp(ln.text, "sl", 2) == 0) continue;
            int line_port = -1;
            if (net_tcp_line(ln.text, &line_port) && line_port == port) {
                found = 1;
                break;
            }
        }
        free(text);
        if (found) return 1;
    }
    return 0;
}

static long proc_start_seconds(int pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(path, &text, &len) != 0) return -1;

    /* Field 22 is starttime, but field 2 (comm) may contain spaces. */
    char *p = strrchr(text, ')');
    if (!p) { free(text); return -1; }
    p++;
    int field = 2;
    long v = -1;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        field++;
        char *end = NULL;
        long val = strtol(p, &end, 10);
        if (end == p) { p++; continue; }
        if (field == 22) { v = val; break; }
        p = end;
    }
    free(text);
    if (v < 0) return -1;

    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) hz = 100;
    long uptime_file = -1;
    char *u = NULL;
    if (fs_read_file("/proc/uptime", &u, NULL) == 0) {
        uptime_file = strtol(u, NULL, 10);
        free(u);
    }
    if (uptime_file < 0) return -1;
    return uptime_file - (v / hz);
}

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/*
 * Does one argv token refer to this disk?
 *
 * The launch line in scripts/start_vm.sh is
 *   -drive file=/abs/path/vms/meu_disco.qcow2,if=virtio,aio=threads
 * so the file name is embedded in a comma-separated option value rather than
 * standing as a whole argument. The match therefore has to be a substring, but
 * only where the name is delimited: preceded by the start of the token, '=',
 * ',' or a path separator, and followed by the end of the token, ',', ':' or a
 * space. A bare substring search would also match "outro_meu_disco.qcow2".
 */
static int token_names_disk(const char *tok, const char *disk)
{
    size_t dl = strlen(disk);
    if (dl == 0) return 0;

    for (const char *p = tok; (p = strstr(p, disk)) != NULL; p++) {
        if (p != tok) {
            char before = p[-1];
            if (before != '=' && before != ',' && before != '/') continue;
        }
        char after = p[dl];
        if (after == '\0' || after == ',' || after == ':' || after == ' ') {
            return 1;
        }
    }
    return 0;
}

/* Find a qemu process whose command line references the project's disk. */
static int find_qemu(const char *disk_path, long *uptime)
{
    const char *disk = disk_path ? base_name(disk_path) : NULL;
    if (!disk || !*disk) return 0;

    DIR *d = opendir("/proc");
    if (!d) return 0;

    struct dirent *de;
    int pid = 0;
    while ((de = readdir(d)) != NULL) {
        if (!isdigit((unsigned char)de->d_name[0])) continue;
        int p = atoi(de->d_name);

        char exe[64], cmd[64];
        snprintf(exe, sizeof(exe), "/proc/%s/comm", de->d_name);
        snprintf(cmd, sizeof(cmd), "/proc/%s/cmdline", de->d_name);

        char *comm = NULL;
        if (fs_read_file(exe, &comm, NULL) != 0) continue;
        int is_qemu = (strncmp(comm, "qemu-system", 11) == 0);
        free(comm);
        if (!is_qemu) continue;

        char *cl = NULL;
        size_t cllen = 0;
        if (fs_read_file(cmd, &cl, &cllen) != 0) continue;
        /* cmdline is a NUL-separated argv; walk it token by token. */
        int hit = 0;
        size_t off = 0;
        while (off < cllen) {
            const char *tok = cl + off;
            size_t tlen = 0;
            while (off + tlen < cllen && cl[off + tlen] != '\0') tlen++;
            if (token_names_disk(tok, disk)) { hit = 1; break; }
            off += tlen + 1;
        }
        free(cl);
        if (!hit) continue;

        pid = p;
        if (uptime) *uptime = proc_start_seconds(p);
        break;
    }
    closedir(d);
    return pid;
}

/* Pull `key: value` out of a group_vars YAML file. */
static int group_var(const char *inv_path, const char *group, const char *key,
                     char *out, size_t outcap)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", inv_path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';

    char path[600];
    snprintf(path, sizeof(path), "%s/group_vars/%s.yml", dir, group);

    char *text = NULL;
    if (fs_read_file(path, &text, NULL) != 0) return -1;

    int rc = -1;
    LineIter it;
    Line ln;
    line_iter_init(&it, text, strlen(text));
    while (line_next(&it, &ln)) {
        char k[64] = { 0 }, v[128] = { 0 };
        if (sscanf(ln.text, " %63[^:]:%127s", k, v) != 2) continue;
        if (strcmp(text_trim(k), key) == 0) {
            snprintf(out, outcap, "%s", text_trim(v));
            rc = 0;
            break;
        }
    }
    free(text);
    return rc;
}

static void read_ssh_vars(VmStat *vs, const char *inv_path)
{
    snprintf(vs->ssh_host, sizeof(vs->ssh_host), "127.0.0.1");
    snprintf(vs->ssh_user, sizeof(vs->ssh_user), "?");

    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(inv_path, &text, &len) != 0) return;

    char cur[64] = { 0 };
    int have_host = 0, have_user = 0;

    LineIter it;
    Line ln;
    line_iter_init(&it, text, len);
    while (line_next(&it, &ln)) {
        if (ln.text[0] == '[') {
            char name[64] = { 0 };
            snprintf(name, sizeof(name), "%s", ln.text + 1);
            char *close = strchr(name, ']');
            if (close) *close = '\0';
            snprintf(cur, sizeof(cur), "%s", text_trim(name));
            continue;
        }
        if (!ln.text[0] || ln.text[0] == '#' || ln.text[0] == ';') continue;
        if (have_host) break;

        /* Only the first host matters for the connection summary. */
        char hv[128] = { 0 };
        if (strstr(ln.text, "ansible_host=")) {
            char *e = strstr(ln.text, "ansible_host=") + strlen("ansible_host=");
            size_t o = 0;
            while (e[o] && !isspace((unsigned char)e[o]) && o + 1 < sizeof(hv)) {
                hv[o] = e[o];
                o++;
            }
            hv[o] = '\0';
            if (hv[0] && strcmp(hv, "*") != 0) {
                snprintf(vs->ssh_host, sizeof(vs->ssh_host), "%s", hv);
                have_host = 1;
            }
        }
    }
    free(text);

    if (cur[0]) {
        char u[128] = { 0 };
        if (group_var(inv_path, cur, "ansible_user", u, sizeof(u)) == 0 && u[0]) {
            snprintf(vs->ssh_user, sizeof(vs->ssh_user), "%s", u);
            have_user = 1;
        }
    }
    (void)have_user;
}

void vmstat_probe(VmStat *vs, const char *disk_path, const char *inv_path)
{
    memset(vs, 0, sizeof(*vs));
    vs->uptime_s = -1;
    vs->checked_at = now_s();

    long up = -1;
    vs->qemu_pid = find_qemu(disk_path, &up);
    vs->uptime_s = vs->qemu_pid ? up : -1;

    vs->ssh_listening = port_is_listening(2222);
    vs->http_listening = port_is_listening(8080);

    read_ssh_vars(vs, inv_path);
}

const char *vmstat_summary(const VmStat *vs)
{
    if (!vs->qemu_pid && !vs->ssh_listening) return "desligada";
    if (vs->qemu_pid && !vs->ssh_listening) return "iniciando";
    if (vs->qemu_pid && vs->ssh_listening) return "ligada";
    return "porta ocupada";
}
