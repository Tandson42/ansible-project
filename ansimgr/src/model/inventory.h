#ifndef ANSIMGR_INVENTORY_H
#define ANSIMGR_INVENTORY_H

#include "model/project.h"

#define MAX_HOSTS 128
#define MAX_GROUPS 64

typedef struct {
    char group[NAMEMAX];
    char host[NAMEMAX];
    char key[NAMEMAX];
    char val[NAMEMAX];
} HostEnt;

typedef struct {
    char name[NAMEMAX];
    int  n_hosts;
    int  first;   /* index into ents of the group's first host */
    char group_vars[PATHMAX];
    int  has_group_vars;
} Group;

typedef struct {
    HostEnt ents[MAX_HOSTS];
    Group groups[MAX_GROUPS];
    int n_ents;
    int n_groups;
    char path[PATHMAX];
} Inventory;

int  inv_load(Inventory *iv, const char *path);
int  inv_group_index(const Inventory *iv, const char *name);
const char *inv_group_vars_path(const Inventory *iv, const char *group);

/* Editing primitives (all rewrite the file atomically after snapshotting). */
int inv_add_host(Inventory *iv, const char *group, const char *host, const char *keyval);
int inv_remove_host(Inventory *iv, const char *group, const char *host);
int inv_add_group(Inventory *iv, const char *group);

#endif /* ANSIMGR_INVENTORY_H */
