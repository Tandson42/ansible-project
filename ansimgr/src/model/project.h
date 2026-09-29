#ifndef ANSIMGR_PROJECT_H
#define ANSIMGR_PROJECT_H

#define PATHMAX 512
#define NAMEMAX 128

#define MAX_ROLES 64
#define MAX_ROLE_OCC 64

typedef struct {
    char name[NAMEMAX];
    int  active;    /* 1 = line is enabled in the playbook */
    int  present;   /* 1 = roles/<name> exists on disk */
} Role;

typedef struct {
    char root[PATHMAX];
    char ansible_cfg[PATHMAX];
    char inventory[PATHMAX];
    char roles_path[PATHMAX];
    char playbook[PATHMAX];
    char start_script[PATHMAX];
    char disk[PATHMAX];
    char state_dir[PATHMAX];
    int  n_roles;
    Role roles[MAX_ROLES];
} Project;

/* Walk up from `start` looking for a directory containing ansible.cfg.
   Returns 0 on success. */
int project_discover(Project *p, const char *start);
void project_reload_roles(Project *p);
int  project_role_index(const Project *p, const char *name);
/* Populate roles[].active from the playbook's roles: block. */
void project_sync_roles(Project *p);

const char *project_basename(const char *path);

#endif /* ANSIMGR_PROJECT_H */
