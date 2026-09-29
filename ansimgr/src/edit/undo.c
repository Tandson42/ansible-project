#include "edit/undo.h"
#include "edit/fsio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static UndoEntry g_stack[UNDO_MAX];
static int g_n = 0;
static char g_root[512] = ".";

void undo_set_root(const char *root)
{
    if (root && *root) snprintf(g_root, sizeof(g_root), "%s", root);
}

void undo_snapshot(const char *path)
{
    if (!path || !*path) return;
    if (g_n >= UNDO_MAX) {
        /* Drop the oldest. */
        memmove(&g_stack[0], &g_stack[1], sizeof(UndoEntry) * (UNDO_MAX - 1));
        g_n = UNDO_MAX - 1;
    }

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.state/backup", g_root);
    char dest[512];
    if (fs_backup(path, dir, dest, sizeof(dest)) != 0) return;

    UndoEntry *e = &g_stack[g_n++];
    snprintf(e->path, sizeof(e->path), "%s", path);
    snprintf(e->backup, sizeof(e->backup), "%s", dest);
}

int undo_count(void)
{
    return g_n;
}

const UndoEntry *undo_at(int i)
{
    if (i < 0 || i >= g_n) return NULL;
    return &g_stack[i];
}

int undo_apply(int i)
{
    if (i < 0 || i >= g_n) return -1;
    char *text = NULL;
    size_t len = 0;
    if (fs_read_file(g_stack[i].backup, &text, &len) != 0) return -1;
    int rc = fs_write_atomic(g_stack[i].path, text, len);
    free(text);
    if (rc != 0) return -1;

    /* Keep the applied entry so the user can toggle back and forth. */
    if (i != 0) {
        UndoEntry top = g_stack[i];
        memmove(&g_stack[1], &g_stack[0], sizeof(UndoEntry) * (size_t)i);
        g_stack[0] = top;
    }
    return 0;
}

void undo_clear(void)
{
    g_n = 0;
}
