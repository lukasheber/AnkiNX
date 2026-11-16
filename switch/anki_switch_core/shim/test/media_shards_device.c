/* nx_media_shards.c on a PC: its SD card is a folder, behind a device that
 * behaves like the console's (a full media folder takes no new file, rename
 * doesn't replace, a listing ends with -1 and ENOENT). */
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "nx_media_shards.c"

#define FULL 5 /* entries the media folder holds */

static int entries(const char *dir) {
    DIR *d = opendir(dir);
    int n = 0;
    struct dirent *e;
    if (!d) return 0;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

static int fail(struct _reent *r) {
    r->_errno = errno;
    return -1;
}

static int sd_open(struct _reent *r, void *file, const char *path, int flags, int mode) {
    struct stat st;
    if ((flags & O_CREAT) && stat(path, &st) != 0) {
        char dir[1024];
        snprintf(dir, sizeof dir, "%.*s", (int)(strrchr(path, '/') - path), path);
        if (ms_is_media_folder(dir, strlen(dir)) && entries(dir) >= FULL) {
            r->_errno = ENOENT; /* as on the console */
            return -1;
        }
    }
    int fd = open(path, flags, mode);
    if (fd < 0) return fail(r);
    *(int *)file = fd;
    return 0;
}
static int sd_stat(struct _reent *r, const char *path, struct stat *st) { return stat(path, st) ? fail(r) : 0; }
static int sd_unlink(struct _reent *r, const char *path) { return unlink(path) ? fail(r) : 0; }
static int sd_mkdir(struct _reent *r, const char *path, int mode) { return mkdir(path, mode) ? fail(r) : 0; }
static int sd_rename(struct _reent *r, const char *from, const char *to) {
    struct stat st;
    if (stat(to, &st) == 0) {
        r->_errno = EEXIST;
        return -1;
    }
    return rename(from, to) ? fail(r) : 0;
}
static DIR_ITER *sd_diropen(struct _reent *r, DIR_ITER *state, const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        r->_errno = errno;
        return NULL;
    }
    *(DIR **)state->dirStruct = d;
    return state;
}
static int sd_dirreset(struct _reent *r, DIR_ITER *state) {
    (void)r;
    rewinddir(*(DIR **)state->dirStruct);
    return 0;
}
static int sd_dirnext(struct _reent *r, DIR_ITER *state, char *filename, struct stat *st) {
    struct dirent *e;
    do e = readdir(*(DIR **)state->dirStruct);
    while (e && (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")));
    if (!e) {
        r->_errno = ENOENT;
        return -1;
    }
    strcpy(filename, e->d_name);
    memset(st, 0, sizeof *st);
    st->st_mode = e->d_type == DT_DIR ? S_IFDIR : S_IFREG;
    return 0;
}
static int sd_dirclose(struct _reent *r, DIR_ITER *state) {
    (void)r;
    return closedir(*(DIR **)state->dirStruct);
}

static const devoptab_t sd = {
    .name = "sdmc", .structSize = sizeof(int), .open_r = sd_open, .stat_r = sd_stat, .lstat_r = sd_stat,
    .unlink_r = sd_unlink, .rename_r = sd_rename, .mkdir_r = sd_mkdir, .dirStateSize = sizeof(DIR *),
    .diropen_r = sd_diropen, .dirreset_r = sd_dirreset, .dirnext_r = sd_dirnext, .dirclose_r = sd_dirclose,
};
const devoptab_t *devoptab_list[] = { &sd };
int FindDevice(const char *name) { return strcmp(name, "sdmc:") ? -1 : 0; }

/* --- what newlib does with a device ----------------------------------------- */

static struct _reent re;
#define DEV devoptab_list[0]

static int touch(const char *path, int flags) {
    int fd = -1;
    if (DEV->open_r(&re, &fd, path, flags, 0644) == -1) return -1;
    close(fd);
    return 0;
}

/* Names in `path`, sorted and joined by spaces; `*end` gets how it ended. */
static const char *list(const char *path, int *end_rc, int *end_errno, int twice) {
    static char out[4096];
    char names[64][256];
    int n = 0;
    DIR_ITER *state = malloc(sizeof(DIR_ITER) + DEV->dirStateSize);
    state->device = 0;
    state->dirStruct = state + 1;
    assert(DEV->diropen_r(&re, state, path));
    struct stat st;
    for (int pass = 0; pass <= twice; pass++) {
        n = 0;
        if (pass) assert(DEV->dirreset_r(&re, state) == 0);
        re._errno = 0;
        while ((*end_rc = DEV->dirnext_r(&re, state, names[n], &st)) == 0) n++;
        *end_errno = re._errno;
    }
    assert(DEV->dirclose_r(&re, state) == 0);
    free(state);
    qsort(names, n, 256, (int (*)(const void *, const void *))strcmp);
    out[0] = 0;
    for (int i = 0; i < n; i++) sprintf(out + strlen(out), "%s%s", i ? " " : "", names[i]);
    return out;
}

int main(void) {
    char root[] = "/tmp/shards.XXXXXX", p[1024], q[1024], cmd[1100];
    struct stat st;
    int rc, err;
    assert(mkdtemp(root));
#define P(buf, rel) (snprintf(buf, sizeof buf, "%s/%.900s", root, rel), buf)
    assert(mkdir(P(p, "collection.media"), 0755) == 0);
    assert(mkdir(P(p, "media.trash"), 0755) == 0);

    /* A media folder as before: filled directly, until it takes no more. */
    for (int i = 0; i < FULL; i++) {
        int fd;
        snprintf(q, sizeof q, "collection.media/old%d.jpg", i);
        assert(sd.open_r(&re, &fd, P(p, q), O_CREAT | O_WRONLY, 0644) == 0);
        close(fd);
    }
    int none;
    assert(sd.open_r(&re, &none, P(p, "collection.media/new1.jpg"), O_CREAT | O_WRONLY, 0644) == -1 && re._errno == ENOENT);

    /* (in place since before main) */
    assert(devoptab_list[0] != &sd);
    ankinx_install_media_shards(); /* once only */
    assert(ms_orig == &sd);

    /* New files go to shards; the folder's own files are found and rewritten in place. */
    assert(touch(P(p, "collection.media/new1.jpg"), O_CREAT | O_WRONLY) == 0);
    assert(touch(P(p, "collection.media/new2.jpg"), O_CREAT | O_WRONLY) == 0);
    assert(touch(P(p, "collection.media/old0.jpg"), O_CREAT | O_WRONLY | O_TRUNC) == 0);
    assert(entries(P(p, "collection.media")) == FULL);
    assert(ms_shard_path(P(p, "collection.media/new1.jpg"), q, sizeof q) && stat(q, &st) == 0);
    assert(touch(P(p, "collection.media/new1.jpg"), O_RDONLY) == 0);
    assert(touch(P(p, "collection.media/old3.jpg"), O_RDONLY) == 0);
    assert(DEV->stat_r(&re, P(p, "collection.media/new2.jpg"), &st) == 0);
    assert(DEV->lstat_r(&re, P(p, "collection.media/old1.jpg"), &st) == 0);
    assert(DEV->stat_r(&re, P(p, "collection.media"), &st) == 0 && S_ISDIR(st.st_mode));

    /* Missing files are missing, with the error of a missing file. */
    re._errno = 0;
    assert(touch(P(p, "collection.media/none.jpg"), O_RDONLY) == -1 && re._errno == ENOENT);
    re._errno = 0;
    assert(DEV->stat_r(&re, P(p, "collection.media/none.jpg"), &st) == -1 && re._errno == ENOENT);
    assert(DEV->unlink_r(&re, P(p, "collection.media/none.jpg")) == -1);

    /* The folder lists both, and ends as the device ends a listing. */
    const char *all = "new1.jpg new2.jpg old0.jpg old1.jpg old2.jpg old3.jpg old4.jpg";
    assert(strcmp(list(P(p, "collection.media"), &rc, &err, 0), all) == 0);
    assert(rc == -1 && err == ENOENT);
    assert(strcmp(list(P(p, "collection.media/"), &rc, &err, 1), all) == 0); /* and after a reset */
    /* Other folders are as they are. */
    assert(strcmp(list(root, &rc, &err, 1), "collection.media collection.media.d media.trash") == 0);
    assert(rc == -1 && err == ENOENT);

    /* Renames: within media, onto an existing file (refused, as on the
     * console: the POSIX shim above this deletes it first), to the trash. */
    assert(DEV->rename_r(&re, P(p, "collection.media/new1.jpg"), P(q, "collection.media/moved.jpg")) == 0);
    assert(DEV->stat_r(&re, P(p, "collection.media/new1.jpg"), &st) == -1);
    assert(DEV->stat_r(&re, P(p, "collection.media/moved.jpg"), &st) == 0);
    re._errno = 0;
    assert(DEV->rename_r(&re, P(p, "collection.media/moved.jpg"), P(q, "collection.media/new2.jpg")) == -1);
    assert(re._errno == EEXIST);
    assert(ankinx_media_path(P(p, "collection.media/new2.jpg"), q) == q && stat(q, &st) == 0);
    assert(ankinx_media_path(P(p, "collection.media/old2.jpg"), q) == p);
    assert(ankinx_media_path(P(p, "collection.mdb"), q) == p);
    assert(DEV->rename_r(&re, P(p, "collection.media/moved.jpg"), P(q, "media.trash/moved.jpg")) == 0);
    assert(DEV->rename_r(&re, P(p, "collection.media/old4.jpg"), P(q, "media.trash/old4.jpg")) == 0);
    assert(stat(P(p, "media.trash/moved.jpg"), &st) == 0 && stat(P(p, "media.trash/old4.jpg"), &st) == 0);
    /* Back from the trash (Anki's "restore"): room in the folder again, but new files keep to shards. */
    assert(DEV->rename_r(&re, P(p, "media.trash/moved.jpg"), P(q, "collection.media/moved.jpg")) == 0);
    assert(entries(P(p, "collection.media")) == FULL - 1);

    assert(DEV->unlink_r(&re, P(p, "collection.media/new2.jpg")) == 0);
    assert(DEV->unlink_r(&re, P(p, "collection.media/old0.jpg")) == 0);
    assert(strcmp(list(P(p, "collection.media"), &rc, &err, 0), "moved.jpg old1.jpg old2.jpg old3.jpg") == 0);

    /* A folder that doesn't exist doesn't open. */
    DIR_ITER *state = malloc(sizeof(DIR_ITER) + DEV->dirStateSize);
    state->dirStruct = state + 1;
    assert(!DEV->diropen_r(&re, state, P(p, "nowhere/collection.media")));
    free(state);

    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    assert(system(cmd) == 0);
    puts("device test ok");
    return 0;
}
