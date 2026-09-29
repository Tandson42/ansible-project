#include "model/project.h"
#include "edit/fsio.h"
#include "edit/ymlrole.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

const char *project_basename(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Read a simple `key = value` from an ini section header we care about. */
static int cfg_value(const char *text, const char *section, const char *key,
                     char *out, size_t outcap)
{
    const char *p = text;
    int in_section = 0;
    size_t klen = strlen(key);

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);

        if (linelen && p[0] == '[') {
            /* Compare the section name only; trailing comments are tolerated. */
            char sec[64] = { 0 };
            size_t i = 1;
            while (i < linelen && p[i] != ']' && i < sizeof(sec) - 1) {
                sec[i - 1] = p[i];
                i++;
            }
            in_section = (strcasecmp(sec, section) == 0);
        } else if (in_section && linelen > klen) {
            const char *q = p;
            while (*q == ' ' || *q == '\t') q++;
            if (strncasecmp(q, key, klen) == 0) {
                const char *v = q + klen;
                while (*v == ' ' || *v == '\t') v++;
                if (*v == '=') v++;
                while (*v == ' ' || *v == '\t') v++;
                size_t o = 0;
                while (v < p + linelen && *v != '#' && *v != '\r' && o + 1 < outcap)
                    out[o++] = *v++;
                while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t')) o--;
                out[o] = '\0';
                return 0;
            }
        }

        if (!eol) break;
        p = eol + 1;
    }
    return -1;
}

static void join(char *dst, size_t cap, const char *a, const char *b)
{
    size_t la = strlen(a);
    if (la > 0 && a[la - 1] == '/') snprintf(dst, cap, "%s%s", a, b);
    else snprintf(dst, cap, "%s/%s", a, b);
}

int project_discover(Project *p, const char *start)
{
    char dir[PATHMAX];
    snprintf(dir, sizeof(dir), "%s", start);

    for (int depth = 0; depth < 12; depth++) {
        char cfg[PATHMAX];
        join(cfg, sizeof(cfg), dir, "ansible.cfg");
        if (is_file(cfg)) {
            memset(p, 0, sizeof(*p));
            snprintf(p->root, sizeof(p->root), "%s", dir);
            snprintf(p->ansible_cfg, sizeof(p->ansible_cfg), "%s", cfg);
            break;
        }
        char parent[PATHMAX];
        snprintf(parent, sizeof(parent), "%s", dir);
        char *slash = strrchr(parent, '/');
        if (!slash || slash == parent) {
            if (slash) slash[1] = '\0';
            else snprintf(parent, sizeof(parent), ".");
        } else {
            *slash = '\0';
        }
        if (strcmp(parent, dir) == 0) break;
        snprintf(dir, sizeof(dir), "%s", parent);
        if (strcmp(dir, "/") == 0) break;
    }

    if (!p->root[0]) {
        if (is_file("ansible.cfg")) {
            memset(p, 0, sizeof(*p));
            if (!getcwd(p->root, sizeof(p->root))) snprintf(p->root, sizeof(p->root), ".");
            snprintf(p->ansible_cfg, sizeof(p->ansible_cfg), "%s/ansible.cfg", p->root);
        } else {
            return -1;
        }
    }

    /* Defaults, then whatever ansible.cfg overrides. */
    snprintf(p->inventory, sizeof(p->inventory), "%s/inventory/hosts.ini", p->root);
    snprintf(p->roles_path, sizeof(p->roles_path), "%s/roles", p->root);
    snprintf(p->playbook, sizeof(p->playbook), "%s/site.yml", p->root);
    snprintf(p->start_script, sizeof(p->start_script), "%s/scripts/start_vm.sh", p->root);
    snprintf(p->state_dir, sizeof(p->state_dir), "%s/.state", p->root);

    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(p->ansible_cfg, &text, &len) == 0 && text) {
        char v[PATHMAX];
        if (cfg_value(text, "defaults", "inventory", v, sizeof(v)) == 0 && v[0]) {
            char cand[PATHMAX];
            join(cand, sizeof(cand), p->root, v);
            snprintf(p->inventory, sizeof(p->inventory), "%s", cand);
        }
        if (cfg_value(text, "defaults", "roles_path", v, sizeof(v)) == 0 && v[0]) {
            char cand[PATHMAX];
            join(cand, sizeof(cand), p->root, v);
            snprintf(p->roles_path, sizeof(p->roles_path), "%s", cand);
        }
        if (cfg_value(text, "defaults", "host_key_checking", v, sizeof(v)) == 0) {
            /* informational only */
        }
        free(text);
    }

    /* Fall back to whatever playbook actually exists at the root. */
    if (!is_file(p->playbook)) {
        static const char *cands[] = { "site.yml", "playbook.yml", "main.yml" };
        for (size_t i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
            char cand[PATHMAX];
            join(cand, sizeof(cand), p->root, cands[i]);
            if (is_file(cand)) {
                snprintf(p->playbook, sizeof(p->playbook), "%s", cand);
                break;
            }
        }
    }

    if (!is_file(p->inventory)) {
        /* The default may be a directory holding hosts.ini or a single file. */
        if (is_dir(p->inventory)) {
            char cand[PATHMAX];
            join(cand, sizeof(cand), p->inventory, "hosts.ini");
            if (is_file(cand)) snprintf(p->inventory, sizeof(p->inventory), "%s", cand);
        } else {
            char cand[PATHMAX];
            join(cand, sizeof(cand), p->root, "inventory/hosts");
            if (is_file(cand)) snprintf(p->inventory, sizeof(p->inventory), "%s", cand);
        }
    }

    /* Locate the QEMU disk image: the single *.qcow2 under vms/, if present. */
    snprintf(p->disk, sizeof(p->disk), "%s", "");
    {
        char vms[PATHMAX];
        join(vms, sizeof(vms), p->root, "vms");
        DIR *d = opendir(vms);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                size_t n = strlen(de->d_name);
                if (n < 6 || strcmp(de->d_name + n - 6, ".qcow2") != 0) continue;
                join(p->disk, sizeof(p->disk), vms, de->d_name);
                break;
            }
            closedir(d);
        }
    }

    project_reload_roles(p);
    return 0;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp(((const Role *)a)->name, ((const Role *)b)->name);
}

void project_reload_roles(Project *p)
{
    p->n_roles = 0;

    DIR *d = opendir(p->roles_path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL && p->n_roles < MAX_ROLES) {
            if (de->d_name[0] == '.') continue;
            char sub[PATHMAX];
            join(sub, sizeof(sub), p->roles_path, de->d_name);
            if (!is_dir(sub)) continue;
            Role *r = &p->roles[p->n_roles++];
            snprintf(r->name, sizeof(r->name), "%s", de->d_name);
            r->active = 0;
            r->present = 1;
        }
        closedir(d);
    }
    qsort(p->roles, (size_t)p->n_roles, sizeof(Role), name_cmp);
    project_sync_roles(p);
}

int project_role_index(const Project *p, const char *name)
{
    for (int i = 0; i < p->n_roles; i++)
        if (strcmp(p->roles[i].name, name) == 0) return i;
    return -1;
}

/*
 * A role is "active" when the playbook enables it, which is a property of
 * site.yml rather than of the roles/ directory. Scanning it here keeps every
 * view agreeing on the same answer.
 */
void project_sync_roles(Project *p)
{
    for (int i = 0; i < p->n_roles; i++) p->roles[i].active = 0;
    if (!p->playbook[0]) return;

    RoleOcc occ[MAX_OCC];
    int n = yml_scan(p->playbook, occ);
    for (int i = 0; i < n; i++) {
        int idx = project_role_index(p, occ[i].name);
        if (idx >= 0) p->roles[idx].active = occ[i].active;
    }
}
