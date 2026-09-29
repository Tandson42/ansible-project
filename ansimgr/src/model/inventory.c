#include "model/inventory.h"
#include "edit/fsio.h"
#include "edit/text.h"
#include "edit/undo.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int group_index(const Inventory *iv, const char *name)
{
    for (int i = 0; i < iv->n_groups; i++)
        if (strcmp(iv->groups[i].name, name) == 0) return i;
    return -1;
}

int inv_group_index(const Inventory *iv, const char *name)
{
    return group_index(iv, name);
}

static int ensure_group(Inventory *iv, const char *name)
{
    int gi = group_index(iv, name);
    if (gi >= 0) return gi;
    if (iv->n_groups >= MAX_GROUPS) return -1;
    Group *g = &iv->groups[iv->n_groups++];
    memset(g, 0, sizeof(*g));
    snprintf(g->name, sizeof(g->name), "%s", name);
    g->first = iv->n_ents;
    return iv->n_groups - 1;
}

static void link_group_vars(Inventory *iv, const char *invpath)
{
    char base[PATHMAX];
    snprintf(base, sizeof(base), "%s", invpath);
    char *slash = strrchr(base, '/');
    if (slash) *slash = '\0';
    else snprintf(base, sizeof(base), ".");

    for (int i = 0; i < iv->n_groups; i++) {
        char p[PATHMAX];
        snprintf(p, sizeof(p), "%s/group_vars/%s.yml", base, iv->groups[i].name);
        iv->groups[i].has_group_vars = fs_exists(p);
        snprintf(iv->groups[i].group_vars, sizeof(iv->groups[i].group_vars), "%s", p);
    }
}

int inv_load(Inventory *iv, const char *path)
{
    memset(iv, 0, sizeof(*iv));
    snprintf(iv->path, sizeof(iv->path), "%s", path);

    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(path, &text, &len) != 0) return -1;

    char cur[NAMEMAX] = { 0 };
    LineIter it;
    Line ln;
    line_iter_init(&it, text, len);

    while (line_next(&it, &ln)) {
        if (ln.text[0] == '[') {
            char name[NAMEMAX] = { 0 };
            snprintf(name, sizeof(name), "%s", ln.text + 1);
            char *close = strchr(name, ']');
            if (close) *close = '\0';
            snprintf(cur, sizeof(cur), "%s", text_trim(name));
            if (cur[0]) ensure_group(iv, cur);
            continue;
        }
        if (!ln.text[0] || ln.text[0] == '#' || ln.text[0] == ';') continue;
        if (!cur[0]) continue;
        if (iv->n_ents >= MAX_HOSTS) continue;

        char namebuf[NAMEMAX] = { 0 };
        char kv[NAMEMAX] = { 0 };
        size_t i = 0;
        while (ln.text[i] && !isspace((unsigned char)ln.text[i])) i++;
        memcpy(namebuf, ln.text, i);
        namebuf[i] = '\0';
        if (i < strlen(ln.text)) snprintf(kv, sizeof(kv), "%s", text_trim(ln.text + i));
        if (!namebuf[0]) continue;

        int gi = ensure_group(iv, cur);
        if (gi < 0) continue;
        HostEnt *h = &iv->ents[iv->n_ents++];
        memset(h, 0, sizeof(*h));
        snprintf(h->group, sizeof(h->group), "%s", cur);
        snprintf(h->host, sizeof(h->host), "%s", namebuf);
        if (kv[0]) {
            /* Inventory lines routinely carry several key=value pairs
               (ansible_host, ansible_port, ...). Keep them all: `key`/`val`
               are only populated for the single-pair case, otherwise the
               whole tail is preserved verbatim in `val`. */
            if (strchr(kv + 1, '=') == NULL) {
                char *eq = strchr(kv, '=');
                if (eq) {
                    *eq = '\0';
                    snprintf(h->key, sizeof(h->key), "%s", text_trim(kv));
                    snprintf(h->val, sizeof(h->val), "%s", text_trim(eq + 1));
                } else {
                    snprintf(h->key, sizeof(h->key), "%s", kv);
                }
            } else {
                snprintf(h->val, sizeof(h->val), "%s", kv);
            }
        }
        iv->groups[gi].n_hosts++;
    }

    free(text);
    link_group_vars(iv, path);
    return 0;
}

const char *inv_group_vars_path(const Inventory *iv, const char *group)
{
    int gi = group_index(iv, group);
    if (gi < 0 || !iv->groups[gi].has_group_vars) return NULL;
    return iv->groups[gi].group_vars;
}

/* Byte offset just past the last non-blank line of a section, or the header. */
static int section_insert_point(const char *text, size_t len, const char *group,
                                size_t *insert_at, int *found)
{
    char header[NAMEMAX];
    snprintf(header, sizeof(header), "[%s]", group);

    LineIter it;
    Line ln;
    line_iter_init(&it, text, len);
    *found = 0;
    *insert_at = len;

    while (line_next(&it, &ln)) {
        if (ln.text[0] == '[') {
            if (strcmp(ln.text, header) == 0) {
                *found = 1;
                *insert_at = ln.next;
                /* Extend over following non-blank lines. */
                Line l2;
                while (line_next(&it, &l2)) {
                    if (!l2.text[0]) break;
                    *insert_at = l2.next;
                }
                return 0;
            }
            continue;
        }
    }
    return 0;
}

int inv_add_group(Inventory *iv, const char *group)
{
    if (!group || !*group) return -1;
    if (group_index(iv, group) >= 0) return 1;

    undo_snapshot(iv->path);
    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(iv->path, &text, &len) != 0) return -1;

    size_t cap = len + 256;
    char *out = malloc(cap);
    if (!out) { free(text); return -1; }
    memcpy(out, text, len);
    size_t o = len;
    if (o > 0 && out[o - 1] != '\n') out[o++] = '\n';
    o += (size_t)snprintf(out + o, cap - o, "\n[%s]\n", group);

    int rc = fs_write_atomic(iv->path, out, o);
    free(text);
    free(out);
    if (rc == 0) inv_load(iv, iv->path);
    return rc;
}

int inv_add_host(Inventory *iv, const char *group, const char *host, const char *keyval)
{
    if (!group || !*group || !host || !*host) return -1;
    if (iv->n_ents >= MAX_HOSTS) return -1;
    for (int i = 0; i < iv->n_ents; i++)
        if (strcmp(iv->ents[i].group, group) == 0 && strcmp(iv->ents[i].host, host) == 0)
            return 1;

    undo_snapshot(iv->path);
    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(iv->path, &text, &len) != 0) return -1;

    char newline[NAMEMAX * 2 + 8];
    if (keyval && *keyval)
        snprintf(newline, sizeof(newline), "%s %s\n", host, keyval);
    else
        snprintf(newline, sizeof(newline), "%s\n", host);
    size_t nlen = strlen(newline);

    size_t insert_at = len;
    int found = 0;
    section_insert_point(text, len, group, &insert_at, &found);
    if (found && insert_at > len) insert_at = len;

    int rc;
    if (found) {
        /* Insert inside the existing section, shifting the tail right. */
        size_t cap = len + nlen + 2;
        char *out = malloc(cap);
        if (!out) { free(text); return -1; }
        memcpy(out, text, insert_at);
        memcpy(out + insert_at, newline, nlen);
        memcpy(out + insert_at + nlen, text + insert_at, len - insert_at);
        rc = fs_write_atomic(iv->path, out, insert_at + nlen + (len - insert_at));
        free(text);
        free(out);
    } else {
        /* The section is new: append "[group]" and the host at end of file. */
        char header[NAMEMAX + 4];
        snprintf(header, sizeof(header), "[%s]\n", group);
        size_t hlen = strlen(header);
        size_t sep = (len > 0 && text[len - 1] != '\n') ? 1 : 0;

        size_t cap = len + sep + hlen + nlen + 2;
        char *out = malloc(cap);
        if (!out) { free(text); return -1; }
        size_t o = 0;
        memcpy(out, text, len);
        o = len;
        if (sep) out[o++] = '\n';
        memcpy(out + o, header, hlen);
        o += hlen;
        memcpy(out + o, newline, nlen);
        o += nlen;
        rc = fs_write_atomic(iv->path, out, o);
        free(text);
        free(out);
    }

    if (rc == 0) inv_load(iv, iv->path);
    return rc;
}

int inv_remove_host(Inventory *iv, const char *group, const char *host)
{
    if (!group || !*host) return -1;

    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(iv->path, &text, &len) != 0) return -1;

    char header[NAMEMAX];
    snprintf(header, sizeof(header), "[%s]", group);

    LineIter it;
    Line ln;
    line_iter_init(&it, text, len);

    int in_group = 0, found = 0;
    size_t del_start = 0, del_end = 0;

    while (line_next(&it, &ln)) {
        if (ln.text[0] == '[') { in_group = (strcmp(ln.text, header) == 0); continue; }
        if (!in_group || !ln.text[0] || ln.text[0] == '#') continue;

        char namebuf[NAMEMAX] = { 0 };
        size_t i = 0;
        while (ln.text[i] && !isspace((unsigned char)ln.text[i])) i++;
        memcpy(namebuf, ln.text, i);
        namebuf[i] = '\0';

        if (strcmp(namebuf, host) == 0) {
            del_start = ln.start;
            del_end = ln.has_nl ? ln.next : ln.end;
            found = 1;
            break;
        }
    }
    if (!found) { free(text); return 1; }

    undo_snapshot(iv->path);

    size_t out_len = len - (del_end - del_start);
    char *out = malloc(out_len + 1);
    if (!out) { free(text); return -1; }
    memcpy(out, text, del_start);
    memcpy(out + del_start, text + del_end, len - del_end);
    out[out_len] = '\0';

    int rc = fs_write_atomic(iv->path, out, out_len);
    free(text);
    free(out);
    if (rc == 0) inv_load(iv, iv->path);
    return rc;
}
