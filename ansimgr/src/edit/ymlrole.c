#include "edit/ymlrole.h"
#include "edit/fsio.h"
#include "edit/text.h"
#include "edit/undo.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The playbook is edited structurally, never re-serialised. A role entry is
 * recognised by its indent, an optional '#', "- role:" and a bare name; only
 * the byte holding the '#' is touched, so comments, ordering and formatting
 * elsewhere in the file survive untouched.
 */

typedef struct {
    size_t dash;     /* offset of '-' */
    size_t hash;     /* offset of '#', 0 when active */
    char name[NAMEMAX];
} ParsedRole;

static int parse_role_line(const char *text, size_t start, size_t end, ParsedRole *out)
{
    size_t n = end - start;
    if (n == 0 || n > 512) return 0;
    char buf[520];
    memcpy(buf, text + start, n);
    buf[n] = '\0';

    size_t i = 0;
    while (i < n && (buf[i] == ' ' || buf[i] == '\t')) i++;
    out->hash = 0;
    if (i < n && buf[i] == '#') { out->hash = start + i; i++; }
    while (i < n && (buf[i] == ' ' || buf[i] == '\t')) i++;
    if (i >= n || buf[i] != '-') return 0;
    out->dash = start + i;
    i++;
    while (i < n && (buf[i] == ' ' || buf[i] == '\t')) i++;

    if (i + 5 > n || strncmp(buf + i, "role:", 5) != 0) return 0;
    i += 5;
    while (i < n && (buf[i] == ' ' || buf[i] == '\t')) i++;

    size_t o = 0;
    while (i < n && o + 1 < sizeof(out->name)) {
        if (buf[i] == '#' || buf[i] == '\r') break;
        out->name[o++] = buf[i++];
    }
    out->name[o] = '\0';
    while (o > 0 && (out->name[o - 1] == ' ' || out->name[o - 1] == '\t')) o--;
    out->name[o] = '\0';
    return out->name[0] != '\0';
}

/* Indentation width of the raw line slice. */
static size_t indent_of(const char *text, size_t start, size_t end)
{
    size_t i = 0;
    while (start + i < end && (text[start + i] == ' ' || text[start + i] == '\t')) i++;
    return i;
}

int yml_scan(const char *playbook, RoleOcc occ[MAX_OCC])
{
    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(playbook, &text, &len) != 0) return -1;

    int n = 0;
    int in_roles = 0;
    size_t roles_indent = 0;

    LineIter it;
    Line ln;
    line_iter_init(&it, text, len);

    while (line_next(&it, &ln)) {
        size_t ind = indent_of(text, ln.start, ln.end);

        if (in_roles) {
            /* Try to read a role entry first: a commented-out role also
               starts with '#' once trimmed, so the comment check below would
               otherwise swallow it. */
            ParsedRole pr;
            if (ind > roles_indent && n < MAX_OCC &&
                parse_role_line(text, ln.start, ln.end, &pr)) {
                RoleOcc *o = &occ[n++];
                memset(o, 0, sizeof(*o));
                snprintf(o->name, sizeof(o->name), "%s", pr.name);
                o->active = (pr.hash == 0);
                o->in_file = 1;
                o->start = ln.start;
                o->end = ln.end;
                o->next = ln.next;
                o->dash = pr.dash;
                o->hash = pr.hash;
                continue;
            }
            if (!ln.text[0] || ln.text[0] == '#') continue; /* blank / comment */
            if (ind <= roles_indent) in_roles = 0; /* block ended */
            else continue;
        }

        if (strncmp(ln.text, "roles:", 6) == 0) {
            in_roles = 1;
            roles_indent = ind;
        }
    }

    free(text);
    return n;
}

int yml_line_of(const char *playbook, int occ_index)
{
    RoleOcc occ[MAX_OCC];
    int n = yml_scan(playbook, occ);
    if (n <= occ_index) return -1;
    size_t target = occ[occ_index].start;

    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(playbook, &text, &len) != 0) return -1;
    int line = 1;
    for (size_t i = 0; i < target && i < len; i++)
        if (text[i] == '\n') line++;
    free(text);
    return line;
}

/* Replace [from,to) with `repl`, writing the result atomically. */
static int splice_write(const char *playbook, size_t from, size_t to,
                        const char *repl, size_t rlen)
{
    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(playbook, &text, &len) != 0) return -1;
    if (from > len || to > len || from > to) { free(text); return -1; }

    undo_snapshot(playbook);

    size_t out_len = len - (to - from) + rlen;
    char *out = malloc(out_len + 1);
    if (!out) { free(text); return -1; }
    memcpy(out, text, from);
    memcpy(out + from, repl, rlen);
    memcpy(out + from + rlen, text + to, len - to);
    out[out_len] = '\0';

    int rc = fs_write_atomic(playbook, out, out_len);
    free(text);
    free(out);
    return rc;
}

int yml_toggle(const char *playbook, int occ_index)
{
    RoleOcc occ[MAX_OCC];
    int n = yml_scan(playbook, occ);
    if (occ_index < 0 || occ_index >= n) return -1;
    RoleOcc *o = &occ[occ_index];

    if (o->active) {
        /* Comment it out by inserting '#' right before the '-'. */
        if (splice_write(playbook, o->dash, o->dash, "#", 1) != 0) return -1;
        return 0;
    }
    if (o->hash == 0) return -1;
    if (splice_write(playbook, o->hash, o->hash + 1, "", 0) != 0) return -1;
    return 1;
}

int yml_delete(const char *playbook, int occ_index)
{
    RoleOcc occ[MAX_OCC];
    int n = yml_scan(playbook, occ);
    if (occ_index < 0 || occ_index >= n) return -1;
    RoleOcc *o = &occ[occ_index];
    /* Take the newline with the line so no blank gap is left behind. */
    return splice_write(playbook, o->start, o->next, "", 0);
}

int yml_add_role(const char *playbook, const char *role)
{
    if (!role || !*role) return -1;

    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(playbook, &text, &len) != 0) return -1;

    RoleOcc occ[MAX_OCC];
    int n = yml_scan(playbook, occ);

    /* A commented-out entry already names this role, so adding it again would
     * duplicate the line. The caller should uncomment that occurrence instead
     * (rc 2 tells it to do so and try again). */
    for (int i = 0; i < n; i++)
        if (strcmp(occ[i].name, role) == 0) {
            free(text);
            return occ[i].active ? 1 : 2;
        }

    char indent[16];
    size_t ws = (n > 0) ? indent_of(text, occ[0].start, occ[0].end) : 4;
    if (ws == 0 || ws >= sizeof(indent)) ws = 4;
    memset(indent, ' ', ws);
    indent[ws] = '\0';

    if (n > 0) {
        char entry[256];
        snprintf(entry, sizeof(entry), "%s- role: %s\n", indent, role);
        size_t at = occ[n - 1].next;
        free(text);
        return splice_write(playbook, at, at, entry, strlen(entry));
    }

    /* No roles: block yet; add one at the end of the document. */
    size_t at = len;
    while (at > 0 && (text[at - 1] == '\n' || text[at - 1] == '\r')) at--;
    char block[512];
    snprintf(block, sizeof(block), "\n\nroles:\n  - role: %s\n", role);
    free(text);
    return splice_write(playbook, at, at, block, strlen(block));
}

static int count_tasks_in(const char *path)
{
    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(path, &text, &len) != 0) return 0;

    int tasks = 0;
    LineIter it;
    Line ln;
    line_iter_init(&it, text, len);
    while (line_next(&it, &ln)) {
        if (strncmp(ln.text, "- name:", 7) == 0) tasks++;
        else if (strncmp(ln.text, "- block:", 8) == 0) tasks++;
    }
    free(text);
    return tasks;
}

int yml_count_tasks(const Project *proj)
{
    if (!proj) return -1;
    int total = count_tasks_in(proj->playbook);
    for (int i = 0; i < proj->n_roles; i++) {
        char path[PATHMAX];
        snprintf(path, sizeof(path), "%s/%s/tasks/main.yml", proj->roles_path,
                 proj->roles[i].name);
        total += count_tasks_in(path);
    }
    return total;
}
