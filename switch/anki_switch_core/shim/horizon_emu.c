/* Host-side emulation of Horizon (Switch) filesystem semantics, for tests.
 * Build as a shared library and LD_PRELOAD it into a Linux test binary:
 *
 *   Horizon layer  stat() of a file this process has open for writing fails;
 *                  read() at/after end of file fails (EIO); write() after end
 *                  of file fails; rename() onto an existing file fails.
 *                  Opening or deleting a file that is open for writing fails
 *                  ("target locked"); there is no fix for that, so it applies
 *                  in both modes and code must close files first.
 *   Fix layer      horizon_fixes.h, the same code the Switch shim uses.
 *
 * HORIZON_EMU_RAW=1 disables the fix layer, to check that the emulation
 * reproduces the errors seen on hardware (SQLITE_IOERR_FSTAT,
 * SQLITE_IOERR_CORRUPTFS). See switch/test_core_host.sh.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int (*real_stat)(const char *, struct stat *);
static int (*real_fstat)(int, struct stat *);
static int (*real_rename)(const char *, const char *);
static ssize_t (*real_read)(int, void *, size_t);
static ssize_t (*real_write)(int, const void *, size_t);
static off_t (*real_lseek)(int, off_t, int);
static int (*real_ftruncate)(int, off_t);
static int (*real_open)(const char *, int, ...);
static int (*real_openat)(int, const char *, int, ...);
static int (*real_unlink)(const char *);

static int raw_mode;

__attribute__((constructor)) static void emu_init(void) {
    real_stat = dlsym(RTLD_NEXT, "stat");
    real_fstat = dlsym(RTLD_NEXT, "fstat");
    real_rename = dlsym(RTLD_NEXT, "rename");
    real_read = dlsym(RTLD_NEXT, "read");
    real_write = dlsym(RTLD_NEXT, "write");
    real_lseek = dlsym(RTLD_NEXT, "lseek");
    real_ftruncate = dlsym(RTLD_NEXT, "ftruncate");
    real_open = dlsym(RTLD_NEXT, "open");
    real_openat = dlsym(RTLD_NEXT, "openat");
    real_unlink = dlsym(RTLD_NEXT, "unlink");
    raw_mode = getenv("HORIZON_EMU_RAW") != NULL;
}

/* Is `path` open for writing anywhere in this process? (Horizon: "target locked") */
static int open_for_writing(const char *path) {
    char want[PATH_MAX], link[300], got[PATH_MAX], info[256];
    if (!realpath(path, want)) return 0;
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return 0;
    int locked = 0;
    struct dirent *e;
    while (!locked && (e = readdir(dir))) {
        if (e->d_name[0] == '.') continue;
        snprintf(link, sizeof link, "/proc/self/fd/%s", e->d_name);
        ssize_t n = readlink(link, got, sizeof got - 1);
        if (n <= 0) continue;
        got[n] = 0;
        if (strcmp(got, want) != 0) continue;
        snprintf(link, sizeof link, "/proc/self/fdinfo/%s", e->d_name);
        int fd = real_open(link, O_RDONLY);
        if (fd < 0) continue;
        n = real_read(fd, info, sizeof info - 1);
        close(fd);
        if (n <= 0) continue;
        info[n] = 0;
        char *flags = strstr(info, "flags:");
        if (flags && (strtol(flags + 6, NULL, 8) & O_ACCMODE) != O_RDONLY) locked = 1;
    }
    closedir(dir);
    return locked;
}

static int regular_at(int fd, off_t *pos, off_t *size) {
    struct stat st;
    *pos = real_lseek(fd, 0, SEEK_CUR);
    if (*pos < 0 || real_fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    *size = st.st_size;
    return 1;
}

/* --- Horizon layer ---------------------------------------------------------- */

static int emu_stat(const char *path, struct stat *st) {
    if (open_for_writing(path)) {
        errno = EBUSY;
        return -1;
    }
    return real_stat(path, st);
}

static ssize_t emu_read(int fd, void *buf, size_t n) {
    off_t pos, size;
    if (n > 0 && regular_at(fd, &pos, &size) && pos >= size) {
        errno = EIO;
        return -1;
    }
    return real_read(fd, buf, n);
}

static ssize_t emu_write(int fd, const void *buf, size_t n) {
    off_t pos, size;
    if (n > 0 && regular_at(fd, &pos, &size) && pos > size) {
        errno = EIO;
        return -1;
    }
    return real_write(fd, buf, n);
}

static int emu_rename(const char *from, const char *to) {
    struct stat st;
    if (real_stat(to, &st) == 0) {
        errno = EEXIST;
        return -1;
    }
    return real_rename(from, to);
}

/* --- Fix layer: the Switch shim's code -------------------------------------- */

#define HZ_STAT(path, st) emu_stat(path, st)
#define HZ_FSTAT(fd, st) real_fstat(fd, st)
#define HZ_RENAME(from, to) emu_rename(from, to)
#define HZ_UNLINK(path) unlink(path)
#define HZ_READ(fd, buf, n) emu_read(fd, buf, n)
#define HZ_WRITE(fd, buf, n) emu_write(fd, buf, n)
#define HZ_LSEEK(fd, off, whence) real_lseek(fd, off, whence)
#define HZ_FTRUNCATE(fd, len) real_ftruncate(fd, len)
#include "horizon_fixes.h"

/* Horizon's GetEntryType works on open files. */
static int hz_entry_type(const char *path) {
    struct stat st;
    if (real_stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 2 : 1;
}

/* --- Exported (interposed) functions ---------------------------------------- */

int stat(const char *path, struct stat *st) { return raw_mode ? emu_stat(path, st) : hz_stat(path, st); }
/* x86-64: struct stat64 and struct stat have the same layout. */
_Static_assert(sizeof(struct stat64) == sizeof(struct stat), "stat64 layout");
int stat64(const char *path, struct stat64 *st) { return stat(path, (struct stat *)st); }
/* newlib's lstat goes through the same file-opening _stat_r on Horizon. */
int lstat(const char *path, struct stat *st) { return stat(path, st); }
int lstat64(const char *path, struct stat64 *st) { return stat(path, (struct stat *)st); }
ssize_t read(int fd, void *buf, size_t n) { return raw_mode ? emu_read(fd, buf, n) : hz_read(fd, buf, n); }
ssize_t write(int fd, const void *buf, size_t n) {
    return raw_mode ? emu_write(fd, buf, n) : hz_write(fd, buf, n);
}
int rename(const char *from, const char *to) { return raw_mode ? emu_rename(from, to) : hz_rename(from, to); }

/* newlib has no pread/pwrite: the Switch shim emulates them with lseek +
 * read/write (nx_posix.c). Do the same here, so code that uses them (glibc
 * builds of SQLite always do) takes the Switch's path through read/write. */
static ssize_t sw_pread(int fd, void *buf, size_t n, off_t off) {
    off_t saved = real_lseek(fd, 0, SEEK_CUR);
    if (saved < 0 || real_lseek(fd, off, SEEK_SET) < 0) return -1;
    ssize_t got = read(fd, buf, n);
    int err = errno;
    real_lseek(fd, saved, SEEK_SET);
    errno = err;
    return got;
}

static ssize_t sw_pwrite(int fd, const void *buf, size_t n, off_t off) {
    off_t saved = real_lseek(fd, 0, SEEK_CUR);
    if (saved < 0 || real_lseek(fd, off, SEEK_SET) < 0) return -1;
    ssize_t put = write(fd, buf, n);
    int err = errno;
    real_lseek(fd, saved, SEEK_SET);
    errno = err;
    return put;
}

/* Target locked: in both modes (see the top of the file). Relative paths are
 * resolved against the working directory, as everything here opens. */
static int locked(const char *path) {
    if (open_for_writing(path)) {
        errno = EBUSY;
        return 1;
    }
    return 0;
}

int open(const char *path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (flags & (O_CREAT | O_TMPFILE)) ? va_arg(ap, mode_t) : 0;
    va_end(ap);
    return locked(path) ? -1 : real_open(path, flags, mode);
}
int open64(const char *path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (flags & (O_CREAT | O_TMPFILE)) ? va_arg(ap, mode_t) : 0;
    va_end(ap);
    return open(path, flags, mode);
}
int openat(int dirfd, const char *path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (flags & (O_CREAT | O_TMPFILE)) ? va_arg(ap, mode_t) : 0;
    va_end(ap);
    if ((path[0] == '/' || dirfd == AT_FDCWD) && locked(path)) return -1;
    return real_openat(dirfd, path, flags, mode);
}
int openat64(int dirfd, const char *path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (flags & (O_CREAT | O_TMPFILE)) ? va_arg(ap, mode_t) : 0;
    va_end(ap);
    return openat(dirfd, path, flags, mode);
}
int unlink(const char *path) { return locked(path) ? -1 : real_unlink(path); }

ssize_t pread(int fd, void *buf, size_t n, off_t off) { return sw_pread(fd, buf, n, off); }
ssize_t pread64(int fd, void *buf, size_t n, off_t off) { return sw_pread(fd, buf, n, off); }
ssize_t pwrite(int fd, const void *buf, size_t n, off_t off) { return sw_pwrite(fd, buf, n, off); }
ssize_t pwrite64(int fd, const void *buf, size_t n, off_t off) { return sw_pwrite(fd, buf, n, off); }
