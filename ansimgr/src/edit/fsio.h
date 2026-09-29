#ifndef ANSIMGR_FSIO_H
#define ANSIMGR_FSIO_H

#include <stddef.h>

/* Read a whole file. Caller frees *out. Returns 0 on success. */
int fs_read_file(const char *path, char **out, size_t *len);

/*
 * Write `data` atomically: temporary file in the same directory, fsync, then
 * rename over the target. Returns 0 on success.
 */
int fs_write_atomic(const char *path, const char *data, size_t len);

int fs_exists(const char *path);
int fs_is_dir(const char *path);
int fs_mkdir_p(const char *path);

/* Copy `src` to a timestamped path under `dir`. Returns 0 on success. */
int fs_backup(const char *src, const char *dir, char *out_path, size_t outcap);

#endif /* ANSIMGR_FSIO_H */
