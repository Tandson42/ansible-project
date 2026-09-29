#ifndef ANSIMGR_UNDO_H
#define ANSIMGR_UNDO_H

#include <stddef.h>

/*
 * Every mutating operation snapshots the file it is about to touch. The undo
 * stack is what the UI offers as Ctrl+Z; the on-disk copies under
 * <project>/.state/backup are what survive a crash.
 */

#define UNDO_MAX 32

typedef struct {
    char path[512];
    char backup[512];
} UndoEntry;

void undo_snapshot(const char *path);
int  undo_count(void);
const UndoEntry *undo_at(int i); /* 0 = most recent */
/* Restore entry `i` (moving it to the front). Returns 0 on success. */
int  undo_apply(int i);
void undo_clear(void);
/* Directory where snapshots are written for a given project root. */
void undo_set_root(const char *root);

#endif /* ANSIMGR_UNDO_H */
