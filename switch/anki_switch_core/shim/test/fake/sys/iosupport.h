/* Host stand-in for newlib's device table (same fields as devkitA64's). */
#pragma once
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
struct _reent { int _errno; };
struct statvfs;
typedef struct { int device; void *dirStruct; } DIR_ITER;
typedef struct {
    const char *name;
    size_t structSize;
    int (*open_r)(struct _reent *r, void *fileStruct, const char *path, int flags, int mode);
    int (*close_r)(struct _reent *r, void *fd);
    ssize_t (*write_r)(struct _reent *r, void *fd, const char *ptr, size_t len);
    ssize_t (*read_r)(struct _reent *r, void *fd, char *ptr, size_t len);
    off_t (*seek_r)(struct _reent *r, void *fd, off_t pos, int dir);
    int (*fstat_r)(struct _reent *r, void *fd, struct stat *st);
    int (*stat_r)(struct _reent *r, const char *file, struct stat *st);
    int (*link_r)(struct _reent *r, const char *existing, const char *newLink);
    int (*unlink_r)(struct _reent *r, const char *name);
    int (*chdir_r)(struct _reent *r, const char *name);
    int (*rename_r)(struct _reent *r, const char *oldName, const char *newName);
    int (*mkdir_r)(struct _reent *r, const char *path, int mode);
    size_t dirStateSize;
    DIR_ITER *(*diropen_r)(struct _reent *r, DIR_ITER *dirState, const char *path);
    int (*dirreset_r)(struct _reent *r, DIR_ITER *dirState);
    int (*dirnext_r)(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat);
    int (*dirclose_r)(struct _reent *r, DIR_ITER *dirState);
    int (*statvfs_r)(struct _reent *r, const char *path, struct statvfs *buf);
    int (*ftruncate_r)(struct _reent *r, void *fd, off_t len);
    int (*fsync_r)(struct _reent *r, void *fd);
    void *deviceData;
    int (*chmod_r)(struct _reent *r, const char *path, mode_t mode);
    int (*fchmod_r)(struct _reent *r, void *fd, mode_t mode);
    int (*rmdir_r)(struct _reent *r, const char *name);
    int (*lstat_r)(struct _reent *r, const char *file, struct stat *st);
    int (*utimes_r)(struct _reent *r, const char *filename, const struct timeval times[2]);
} devoptab_t;
extern const devoptab_t *devoptab_list[];
int FindDevice(const char *name);
