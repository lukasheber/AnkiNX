/* POSIX functions referenced by Rust std / rustix / ring dependencies that
 * devkitA64 newlib + libnx do not provide on the Nintendo Switch.
 *
 * Rule: implement what has a sound Horizon equivalent; otherwise fail with
 * ENOSYS rather than pretend to succeed. Keep in sync with the "MISSING" list
 * printed by switch/tools/check_core.sh.
 */
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <reent.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

#include <switch.h>

/* stat/read/write/rename below forward to newlib's _stat_r etc.; with these
 * macros those would expand back into stat() etc. and recurse forever. */
#if defined(REENTRANT_SYSCALLS_PROVIDED) && defined(MISSING_SYSCALL_NAMES)
#error "newlib maps _stat_r & co. to stat & co.: the overrides below would recurse"
#endif

/* --- randomness ----------------------------------------------------------- */

/* libnx's randomGet is a ChaCha20 CSPRNG seeded from kernel entropy
 * (svcGetInfo RandomEntropy); it never blocks, so flags are irrelevant. */
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    randomGet(buf, buflen);
    return (ssize_t)buflen;
}

/* --- system configuration ------------------------------------------------- */

static long nx_cpu_count(void) {
    u64 mask = 0;
    if (R_SUCCEEDED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)) && mask) {
        return __builtin_popcountll(mask);
    }
    return 3; /* applications get cores 0-2; core 3 belongs to the system */
}

long sysconf(int name) {
    switch (name) {
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN:
        return nx_cpu_count();
    case _SC_PAGESIZE:
        return 0x1000;
    default:
        errno = EINVAL;
        return -1;
    }
}

/* --- positional I/O (not atomic: emulated with lseek) ---------------------- */

ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = read(fd, buf, count);
    int err = errno;
    lseek(fd, saved, SEEK_SET);
    errno = err;
    return n;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
    off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = write(fd, buf, count);
    int err = errno;
    lseek(fd, saved, SEEK_SET);
    errno = err;
    return n;
}

/* --- *at() family: only the current directory or absolute paths ----------- */

/* devoptab paths look like "sdmc:/..." as well as "/..." */
static int nx_path_ok(int dirfd, const char *path) {
    if (dirfd == AT_FDCWD || (path && (path[0] == '/' || strchr(path, ':')))) return 1;
    errno = ENOSYS;
    return 0;
}

int openat(int dirfd, const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return nx_path_ok(dirfd, path) ? open(path, flags, mode) : -1;
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags) {
    if (!nx_path_ok(dirfd, path)) return -1;
    return (flags & AT_SYMLINK_NOFOLLOW) ? lstat(path, st) : stat(path, st);
}

int mkdirat(int dirfd, const char *path, mode_t mode) {
    return nx_path_ok(dirfd, path) ? mkdir(path, mode) : -1;
}

int unlinkat(int dirfd, const char *path, int flags) {
    if (!nx_path_ok(dirfd, path)) return -1;
    return (flags & AT_REMOVEDIR) ? rmdir(path) : unlink(path);
}

int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    if (!nx_path_ok(olddirfd, oldpath) || !nx_path_ok(newdirfd, newpath)) return -1;
    return rename(oldpath, newpath);
}

int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags) {
    (void)flags;
    if (!nx_path_ok(olddirfd, oldpath) || !nx_path_ok(newdirfd, newpath)) return -1;
    return link(oldpath, newpath);
}

int symlinkat(const char *target, int newdirfd, const char *linkpath) {
    return nx_path_ok(newdirfd, linkpath) ? symlink(target, linkpath) : -1;
}

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz) {
    return nx_path_ok(dirfd, path) ? readlink(path, buf, bufsiz) : -1;
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags) {
    if (flags & AT_SYMLINK_NOFOLLOW) {
        errno = ENOTSUP;
        return -1;
    }
    return nx_path_ok(dirfd, path) ? chmod(path, mode) : -1;
}

/* --- POSIX file semantics on Horizon (see horizon_fixes.h) ----------------- */

/* These definitions replace newlib's stat/read/write/rename for the whole
 * program. In libc.a each sits alone in its object (sysstat.o, sysread.o,
 * syswrite.o, rename.o) and only forwards to the _r function used below, so
 * the linker takes ours and never pulls newlib's. */
#define HZ_STAT(path, st) _stat_r(_REENT, path, st)
#define HZ_FSTAT(fd, st) _fstat_r(_REENT, fd, st)
#define HZ_RENAME(from, to) _rename_r(_REENT, from, to)
#define HZ_UNLINK(path) unlink(path)
#define HZ_READ(fd, buf, n) _read_r(_REENT, fd, buf, n)
#define HZ_WRITE(fd, buf, n) _write_r(_REENT, fd, buf, n)
#define HZ_LSEEK(fd, off, whence) _lseek_r(_REENT, fd, off, whence)
#define HZ_FTRUNCATE(fd, len) ftruncate(fd, len)
#include "horizon_fixes.h"

/* Entry type without opening the file: ask the filesystem directly. */
static int hz_entry_type(const char *path) {
    FsFileSystem *fs = NULL;
    char fspath[FS_MAX_PATH];
    FsDirEntryType type;
    if (fsdevTranslatePath(path, &fs, fspath) == -1 || R_FAILED(fsFsGetEntryType(fs, fspath, &type)))
        return 0;
    return type == FsDirEntryType_Dir ? 2 : 1;
}

int stat(const char *path, struct stat *st) { return hz_stat(path, st); }
/* newlib's lstat (libsysbase lstat.o) also goes through the file-opening
 * _stat_r; there are no symlinks on the SD card, so it is stat. */
int lstat(const char *path, struct stat *st) { return hz_stat(path, st); }
ssize_t read(int fd, void *buf, size_t count) { return hz_read(fd, buf, count); }
ssize_t write(int fd, const void *buf, size_t count) { return hz_write(fd, buf, count); }
int rename(const char *from, const char *to) { return hz_rename(from, to); }

/* --- thread-local keys for Rust -------------------------------------------- */

/* The target has no native thread-locals for rustc (devkitA64 reads the thread
 * pointer its own way), so every Rust `thread_local!` in std and the crates
 * takes a pthread key on first use. libnx backs each key with one of ~27 TLS
 * slots per thread, shared with C code (libstdc++, SDL, mesa). A large program
 * runs out mid-way, and std then aborts ("out of TLS keys"): on hardware,
 * loading a collection or syncing closed the app. So Rust's pthread_key_*
 * calls (redirected here by the libc fork, see switch/patches/libc.patch) use
 * one libnx slot holding a growable per-thread table. Keys are never reused;
 * key 0 is skipped because std treats it as "not created yet". */

#define NXSHIM_MAX_KEYS 4096
#define NXSHIM_DESTRUCTOR_ITERATIONS 4

typedef void (*nxshim_dtor)(void *);
typedef struct {
    size_t len;
    void *values[];
} nxshim_table;

static Mutex nxshim_lock;
static s32 nxshim_slot = -1;
static unsigned nxshim_next_key = 1;
static nxshim_dtor nxshim_dtors[NXSHIM_MAX_KEYS];

static nxshim_table *nxshim_get_table(void) {
    return nxshim_slot < 0 ? NULL : (nxshim_table *)threadTlsGet(nxshim_slot);
}

/* Thread exit (libnx calls it for a non-null slot): run the key destructors,
 * which may set values again, then free the table. */
static void nxshim_thread_exit(void *table) {
    threadTlsSet(nxshim_slot, table);
    for (int round = 0; round < NXSHIM_DESTRUCTOR_ITERATIONS; round++) {
        int ran = 0;
        for (size_t key = 1;; key++) {
            nxshim_table *t = nxshim_get_table(); /* destructors may grow it */
            if (!t || key >= t->len) break;
            void *value = t->values[key];
            nxshim_dtor dtor = key < NXSHIM_MAX_KEYS ? nxshim_dtors[key] : NULL;
            if (value && dtor) {
                t->values[key] = NULL;
                dtor(value);
                ran = 1;
            }
        }
        if (!ran) break;
    }
    free(nxshim_get_table());
    threadTlsSet(nxshim_slot, NULL);
}

int nxshim_pthread_key_create(unsigned *key, void (*dtor)(void *)) {
    int rc = 0;
    mutexLock(&nxshim_lock);
    if (nxshim_slot < 0) nxshim_slot = threadTlsAlloc(nxshim_thread_exit);
    if (nxshim_slot < 0 || nxshim_next_key >= NXSHIM_MAX_KEYS) {
        rc = EAGAIN;
    } else {
        nxshim_dtors[nxshim_next_key] = dtor;
        *key = nxshim_next_key++;
    }
    mutexUnlock(&nxshim_lock);
    return rc;
}

int nxshim_pthread_key_delete(unsigned key) {
    if (key == 0 || key >= nxshim_next_key) return EINVAL;
    nxshim_dtors[key] = NULL; /* values are left to their owners, as in POSIX */
    return 0;
}

void *nxshim_pthread_getspecific(unsigned key) {
    nxshim_table *t = nxshim_get_table();
    return t && key < t->len ? t->values[key] : NULL;
}

int nxshim_pthread_setspecific(unsigned key, const void *value) {
    if (key == 0 || key >= nxshim_next_key) return EINVAL;
    nxshim_table *t = nxshim_get_table();
    if (!t || key >= t->len) {
        size_t old = t ? t->len : 0;
        size_t len = old ? old * 2 : 32;
        while (len <= key) len *= 2;
        nxshim_table *grown = realloc(t, sizeof(nxshim_table) + len * sizeof(void *));
        if (!grown) return ENOMEM;
        memset(&grown->values[old], 0, (len - old) * sizeof(void *));
        grown->len = len;
        threadTlsSet(nxshim_slot, grown);
        t = grown;
    }
    t->values[key] = (void *)value;
    return 0;
}

/* --- not available on Horizon ---------------------------------------------- */

#define NX_ENOSYS(decl) \
    decl {              \
        errno = ENOSYS; \
        return -1;      \
    }

/* FAT/exFAT on the SD card has no ownership. */
NX_ENOSYS(int chown(const char *p, uid_t u, gid_t g))
NX_ENOSYS(int fchown(int fd, uid_t u, gid_t g))
NX_ENOSYS(int lchown(const char *p, uid_t u, gid_t g))
NX_ENOSYS(int fchownat(int d, const char *p, uid_t u, gid_t g, int f))
NX_ENOSYS(int chroot(const char *p))
NX_ENOSYS(int mkfifo(const char *p, mode_t m))
NX_ENOSYS(int pipe(int fds[2]))
NX_ENOSYS(int fstatvfs(int fd, struct statvfs *buf))

/* Single-user system without a process tree. */
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
pid_t getppid(void) { return 0; }

/* No user database: "not found" per POSIX (return 0, *result = NULL). */
int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen, struct passwd **result) {
    (void)uid, (void)pwd, (void)buf, (void)buflen;
    *result = NULL;
    return 0;
}
