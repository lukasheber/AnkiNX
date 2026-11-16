/* Media files spread over folders (see media_shards.h for why and where).
 *
 * Done below everything that opens files: the SD card's entry in newlib's
 * device table is replaced by a copy whose path functions look for a media
 * file in its shard first, then in collection.media itself, and create new
 * ones in the shard. Listing collection.media gives the files of both. So
 * Anki, SQLite and the app (fopen, the audio player) keep using
 * collection.media/NAME, and none of them knows.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <switch.h>

#include "media_shards.h"

static const devoptab_t *ms_orig; /* the SD card's own functions */
static devoptab_t ms_dev;

static int ms_exists(const char *path) {
    FsFileSystem *fs = NULL;
    char fspath[FS_MAX_PATH];
    FsDirEntryType type;
    return fsdevTranslatePath(path, &fs, fspath) != -1 && R_SUCCEEDED(fsFsGetEntryType(fs, fspath, &type));
}

/* The path to use for `path`: itself, unless it names a media file, which is
 * in its shard, or else in the media folder, or else to be created in its
 * shard. `buf` (MS_PATH bytes) holds the result when it isn't `path`. */
const char *ankinx_media_path(const char *path, char *buf) {
    if (!ms_orig || !ms_shard_path(path, buf, MS_PATH)) return path;
    if (ms_exists(buf) || !ms_exists(path)) return buf;
    return path;
}

/* Create the folders of the sharded path `shard`. Returns 1 if its folder
 * was made (it was missing: worth trying again); otherwise the error of the
 * call that failed before this is left as it was. */
static int ms_make_folders(struct _reent *r, char *shard) {
    int err = r->_errno;
    char *name = strrchr(shard, '/');
    *name = 0;
    char *bucket = strrchr(shard, '/');
    *bucket = 0;
    ms_orig->mkdir_r(r, shard, 0777); /* collection.media.d: usually there */
    *bucket = '/';
    int made = ms_orig->mkdir_r(r, shard, 0777) == 0;
    *name = '/';
    if (!made) r->_errno = err;
    return made;
}

static int ms_open(struct _reent *r, void *file, const char *path, int flags, int mode) {
    char buf[MS_PATH];
    const char *real = ankinx_media_path(path, buf);
    int rc = ms_orig->open_r(r, file, real, flags, mode);
    if (rc == -1 && real == buf && (flags & O_CREAT) && ms_make_folders(r, buf))
        rc = ms_orig->open_r(r, file, real, flags, mode);
    return rc;
}

static int ms_stat(struct _reent *r, const char *path, struct stat *st) {
    char buf[MS_PATH];
    return ms_orig->stat_r(r, ankinx_media_path(path, buf), st);
}

static int ms_lstat(struct _reent *r, const char *path, struct stat *st) {
    char buf[MS_PATH];
    return ms_orig->lstat_r(r, ankinx_media_path(path, buf), st);
}

static int ms_unlink(struct _reent *r, const char *path) {
    char buf[MS_PATH];
    return ms_orig->unlink_r(r, ankinx_media_path(path, buf));
}

static int ms_utimes(struct _reent *r, const char *path, const struct timeval times[2]) {
    char buf[MS_PATH];
    return ms_orig->utimes_r(r, ankinx_media_path(path, buf), times);
}

static int ms_rename(struct _reent *r, const char *from, const char *to) {
    char from_buf[MS_PATH], to_buf[MS_PATH];
    const char *src = ankinx_media_path(from, from_buf);
    const char *dst = ankinx_media_path(to, to_buf);
    int rc = ms_orig->rename_r(r, src, dst);
    if (rc == -1 && dst == to_buf && ms_make_folders(r, to_buf)) rc = ms_orig->rename_r(r, src, dst);
    return rc;
}

/* --- listing ---------------------------------------------------------------- */

/* An open folder. A media folder is read to its end, then each shard is. */
typedef struct {
    DIR_ITER *inner;    /* the folder being read, as the SD card knows it */
    int device;
    int next_shard;     /* media folder: the shard to read next; else -1 */
    int end_rc;         /* media folder: how its own listing ended, which is */
    int end_errno;      /*   how the whole listing ends */
    char root[MS_PATH]; /* media folder: where its shards are */
} ms_dir;

static DIR_ITER *ms_inner_open(struct _reent *r, int device, const char *path) {
    DIR_ITER *inner = malloc(sizeof(DIR_ITER) + ms_orig->dirStateSize);
    if (!inner) {
        r->_errno = ENOMEM;
        return NULL;
    }
    inner->device = device;
    inner->dirStruct = inner + 1;
    if (!ms_orig->diropen_r(r, inner, path)) {
        free(inner);
        return NULL;
    }
    return inner;
}

static void ms_inner_close(struct _reent *r, ms_dir *dir) {
    if (!dir->inner) return;
    ms_orig->dirclose_r(r, dir->inner);
    free(dir->inner);
    dir->inner = NULL;
}

static DIR_ITER *ms_diropen(struct _reent *r, DIR_ITER *state, const char *path) {
    ms_dir *dir = state->dirStruct;
    dir->device = state->device;
    dir->inner = ms_inner_open(r, dir->device, path);
    if (!dir->inner) return NULL;
    dir->next_shard = ms_shard_root(path, dir->root, MS_PATH) ? 0 : -1;
    return state;
}

static int ms_dirnext(struct _reent *r, DIR_ITER *state, char *filename, struct stat *st) {
    ms_dir *dir = state->dirStruct;
    if (dir->next_shard < 0) return ms_orig->dirnext_r(r, dir->inner, filename, st);
    for (;;) {
        if (dir->inner) {
            int rc = ms_orig->dirnext_r(r, dir->inner, filename, st);
            if (rc == 0) return 0;
            if (dir->next_shard == 0) {
                dir->end_rc = rc;
                dir->end_errno = r->_errno;
            }
            ms_inner_close(r, dir);
        }
        if (dir->next_shard >= MS_SHARDS) {
            r->_errno = dir->end_errno;
            return dir->end_rc;
        }
        char path[MS_PATH + 8];
        snprintf(path, sizeof path, "%s/%02x", dir->root, dir->next_shard);
        dir->next_shard++;
        dir->inner = ms_inner_open(r, dir->device, path); /* NULL: no such shard yet */
    }
}

static int ms_dirreset(struct _reent *r, DIR_ITER *state) {
    ms_dir *dir = state->dirStruct;
    if (dir->next_shard < 0) return ms_orig->dirreset_r(r, dir->inner);
    ms_inner_close(r, dir);
    char path[MS_PATH];
    snprintf(path, sizeof path, "%.*s", (int)(strlen(dir->root) - 2), dir->root); /* without ".d" */
    dir->inner = ms_inner_open(r, dir->device, path);
    dir->next_shard = 0;
    return dir->inner ? 0 : -1;
}

static int ms_dirclose(struct _reent *r, DIR_ITER *state) {
    ms_inner_close(r, state->dirStruct);
    return 0;
}

/* Runs before main() (libnx has mounted the SD card by then, and no other
 * thread has a folder open), and again before the first API call in case it
 * was too early. Without the SD card's device (or with one missing what is
 * wrapped here) nothing changes. */
__attribute__((constructor)) void ankinx_install_media_shards(void) {
    if (ms_orig) return;
    int index = FindDevice("sdmc:");
    if (index < 0) return;
    const devoptab_t *dev = devoptab_list[index];
    if (!dev || !dev->open_r || !dev->stat_r || !dev->unlink_r || !dev->rename_r || !dev->mkdir_r ||
        !dev->diropen_r || !dev->dirreset_r || !dev->dirnext_r || !dev->dirclose_r)
        return;
    ms_dev = *dev;
    ms_dev.open_r = ms_open;
    ms_dev.stat_r = ms_stat;
    ms_dev.unlink_r = ms_unlink;
    ms_dev.rename_r = ms_rename;
    if (dev->lstat_r) ms_dev.lstat_r = ms_lstat;
    if (dev->utimes_r) ms_dev.utimes_r = ms_utimes;
    ms_dev.dirStateSize = sizeof(ms_dir);
    ms_dev.diropen_r = ms_diropen;
    ms_dev.dirreset_r = ms_dirreset;
    ms_dev.dirnext_r = ms_dirnext;
    ms_dev.dirclose_r = ms_dirclose;
    ms_orig = dev;
    devoptab_list[index] = &ms_dev;
}
