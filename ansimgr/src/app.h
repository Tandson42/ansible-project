#ifndef ANSIMGR_APP_H
#define ANSIMGR_APP_H

#include "tui/buf.h"
#include "tui/input.h"
#include "tui/widget.h"
#include "proc/job.h"
#include "model/project.h"
#include "model/inventory.h"
#include "model/vmstat.h"
#include "model/recap.h"
#include "edit/ymlrole.h"

typedef enum {
    V_DASHBOARD = 0,
    V_HOSTS,
    V_ROLES,
    V_RUN,
    V_VM,
    V_LINT,
    V_HELP,
    V_COUNT
} ViewId;

typedef struct App {
    Project proj;
    Inventory inv;
    VmStat vm;
    Recap last;        /* recap of the most recent run */
    Recap prev;        /* the one before it, for drift detection */

    Job *job;
    LogView log;
    char job_title[128];
    long job_started;  /* wall-clock seconds when the job was launched */
    int  job_kind;     /* RUN_KIND_* */
    int  run_seq;      /* how many runs this session */

    /* argv of the last playbook run, so it can be repeated with F10 */
    char last_argv[16][256];
    int  last_n;
    int  last_kind;
    char last_title[128];

    int  active;
    int  quit;
    char status[256];
    int  status_kind;  /* 0 info, 1 ok, 2 warn, 3 error */

    Prompt prompt;
    Confirm confirm;
    int prompt_action; /* PA_* — what to do when the prompt is accepted */

    /* per-view selection state */
    ListSel sel_dash;
    ListSel sel_hosts;
    ListSel sel_roles;
    ListSel sel_vm;

    /* playbook run options */
    int opt_check;
    int opt_diff;
    int opt_tags[8];
    int n_tags;
    char limit[128];

    /* pending action for the confirm modal */
    int pending_confirm; /* PC_* */
    char pending_arg[256];
    char pending_name[NAMEMAX]; /* display name, kept apart from the argument */
} App;

/* job kinds */
enum { RUN_KIND_NONE = 0, RUN_KIND_PLAY, RUN_KIND_PING, RUN_KIND_LINT,
       RUN_KIND_SYNTAX, RUN_KIND_CHECK };

/* pending confirms */
enum { PC_NONE = 0, PC_DELETE_ROLE, PC_REMOVE_HOST, PC_KILL_VM, PC_UNDO };

/* prompt actions */
enum { PA_NONE = 0, PA_LIMIT, PA_ADD_HOST, PA_ADD_GROUP, PA_ADD_ROLE };

void app_status(App *a, int kind, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* ---- shared executor, used by Playbook and Lint ------------------------ */

int  run_exec(App *a, int kind, const char *title, char *const argv[]);
void run_on_line(void *ud, const char *line, size_t len);
void run_on_exit(void *ud, int status, int term_status);
int  run_is_busy(const App *a);
void run_scroll(App *a, int delta);
void run_toggle_follow(App *a);
void run_repeat(App *a);
void run_cancel(App *a);
/* Assemble the ansible-playbook command line from the view's option flags. */
void run_build_playbook_argv(App *a, char *argv[16]);
void run_playbook(App *a);

/* ---- views ------------------------------------------------------------- */

const char *view_name(int id);
const char *view_hint(int id);
void view_draw(App *a, Buf *b);
void view_key(App *a, Key k);
void view_tick(App *a);

/* chrome */
void chrome_header(App *a, Buf *b);
void chrome_tabs(App *a, Buf *b);
void chrome_footer(App *a, Buf *b);
void chrome_sidebar(App *a, Buf *b, int x, int y, int w, int h);

void dashboard_draw(App *a, Buf *b, int x, int y, int w, int h);
void dashboard_key(App *a, Key k);
void hosts_draw(App *a, Buf *b, int x, int y, int w, int h);
void hosts_key(App *a, Key k);
void roles_draw(App *a, Buf *b, int x, int y, int w, int h);
void roles_key(App *a, Key k);
void run_draw(App *a, Buf *b, int x, int y, int w, int h);
void run_key(App *a, Key k);
void vm_draw(App *a, Buf *b, int x, int y, int w, int h);
void vm_key(App *a, Key k);
void lint_draw(App *a, Buf *b, int x, int y, int w, int h);
void lint_key(App *a, Key k);
void help_draw(App *a, Buf *b, int x, int y, int w, int h);
void help_key(App *a, Key k);

#endif /* ANSIMGR_APP_H */
