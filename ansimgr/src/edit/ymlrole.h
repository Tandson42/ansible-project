#ifndef ANSIMGR_YMLROLE_H
#define ANSIMGR_YMLROLE_H

#include "model/project.h"
#include <stddef.h>

#define MAX_OCC 64

typedef struct {
    char name[NAMEMAX];
    int  active;      /* 1 = enabled in the playbook */
    int  in_file;     /* 1 = present in the playbook at all */
    size_t start;     /* byte offset of the line start */
    size_t end;       /* byte offset just past the line's last byte */
    size_t next;      /* byte offset of the following line */
    size_t dash;      /* byte offset of the '-' introducing the item */
    size_t hash;      /* byte offset of the '#' when inactive, else 0 */
} RoleOcc;

/* Scan the playbook for `roles:` entries. Returns the count, or -1 on error. */
int yml_scan(const char *playbook, RoleOcc occ[MAX_OCC]);

/* Add or remove the single '#' on the occurrence's line, leaving the rest of
   the file byte-for-byte identical. Returns the new active state, or -1. */
int yml_toggle(const char *playbook, int occ_index);

/* Delete the occurrence's line entirely, including its newline. */
int yml_delete(const char *playbook, int occ_index);

/* Append `- role: <name>` at the end of the roles: block (or create the
   block if the playbook has none). */
int yml_add_role(const char *playbook, const char *role);

/* Number of task lines in the playbook plus every role's tasks/main.yml,
   which is a cheap proxy for how much work a run will do. */
int yml_count_tasks(const Project *proj);

/* Line number (1-based) of an occurrence, for display. */
int yml_line_of(const char *playbook, int occ_index);

#endif /* ANSIMGR_YMLROLE_H */
