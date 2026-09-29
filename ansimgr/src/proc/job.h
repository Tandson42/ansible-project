#ifndef ANSIMGR_JOB_H
#define ANSIMGR_JOB_H

#include <stddef.h>
#include <sys/types.h>

/*
 * Runs a child process attached to a pseudo-terminal so that programs which
 * switch to colour output (ansible-playbook does) behave as they would in a
 * real terminal. The parent reads the master side without blocking and feeds
 * complete lines to a callback, which lets the UI stay responsive.
 */

typedef struct Job Job;

typedef void (*JobLineFn)(void *ud, const char *line, size_t len);
typedef void (*JobExitFn)(void *ud, int status, int term_status);

/* argv is NULL-terminated. Returns NULL on failure to spawn. */
Job *job_start(char *const argv[], const char *cwd, int cols, int rows,
               JobLineFn on_line, JobExitFn on_exit, void *ud);

int  job_fd(const Job *j);        /* master fd to poll, or -1 */
int  job_running(const Job *j);
int  job_pid(const Job *j);
void job_drain(Job *j);          /* read what is available, emit lines */
void job_cancel(Job *j);         /* SIGINT the child's process group */
void job_poll(Job *j);           /* non-blocking reap check */
void job_free(Job *j);
const char *job_title(const Job *j);
void job_set_title(Job *j, const char *t);

#endif /* ANSIMGR_JOB_H */
