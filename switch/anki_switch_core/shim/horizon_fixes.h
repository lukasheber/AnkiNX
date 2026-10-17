/* POSIX file semantics on top of Horizon's filesystem, shared by the Switch
 * shim (nx_posix.c) and the host-side Horizon emulator used in tests
 * (horizon_emu.c). Horizon differs from POSIX in ways Anki and SQLite rely on:
 *
 *   stat()   libnx opens the file to read its size, and Horizon refuses to
 *            open a file that is open for writing. SQLite stats its open
 *            database when creating the rollback journal (on device:
 *            SQLITE_IOERR_FSTAT, extended code 1802).
 *   read()   reading at or past the end of a file is an error, not end of file.
 *            SQLite reads the header of a new, empty database (on device:
 *            SQLITE_IOERR_CORRUPTFS, extended code 8458).
 *   write()  writing past the end may be refused instead of extending the file.
 *   rename() fails when the destination exists instead of replacing it (Anki's
 *            full download renames the new collection over the old one).
 *
 * Each fix calls the platform's own function first and only does extra work
 * when that fails, so the common path costs nothing.
 *
 * The includer defines these "inner" primitives before including:
 *   HZ_STAT(path, st)  HZ_FSTAT(fd, st)  HZ_RENAME(from, to)  HZ_UNLINK(path)
 *   HZ_READ(fd, buf, n)  HZ_WRITE(fd, buf, n)  HZ_LSEEK(fd, off, whence)
 *   HZ_FTRUNCATE(fd, len)
 *   hz_entry_type(path): 0 missing/unknown, 1 file, 2 directory, without
 *                        opening the file.
 */
#ifndef HORIZON_FIXES_H
#define HORIZON_FIXES_H

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int hz_entry_type(const char *path);

static int hz_stat(const char *path, struct stat *st) {
    if (HZ_STAT(path, st) == 0) return 0;
    int err = errno;
    if (err == ENOENT || err == ENOTDIR) return -1;
    int type = hz_entry_type(path);
    if (type == 0) {
        errno = err;
        return -1;
    }
    /* Size and times unknown (0); callers here only need the file type. */
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_mode = type == 2 ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    return 0;
}

/* Current position and size of a regular file; 0 if not a regular file. */
static int hz_file_pos(int fd, off_t *pos, off_t *size) {
    struct stat st;
    *pos = HZ_LSEEK(fd, 0, SEEK_CUR);
    if (*pos < 0 || HZ_FSTAT(fd, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    *size = st.st_size;
    return 1;
}

static ssize_t hz_read(int fd, void *buf, size_t count) {
    ssize_t got = HZ_READ(fd, buf, count);
    if (got >= 0 || count == 0) return got;
    int err = errno;
    off_t pos, size;
    if (hz_file_pos(fd, &pos, &size) && pos >= size) return 0; /* POSIX: end of file */
    errno = err;
    return -1;
}

static ssize_t hz_write(int fd, const void *buf, size_t count) {
    ssize_t put = HZ_WRITE(fd, buf, count);
    if (put >= 0 || count == 0) return put;
    int err = errno;
    off_t pos, size;
    /* POSIX: writing past the end extends the file (the gap reads as zeros). */
    if (hz_file_pos(fd, &pos, &size) && pos > size && HZ_FTRUNCATE(fd, pos) == 0 &&
        HZ_LSEEK(fd, pos, SEEK_SET) == pos) {
        return HZ_WRITE(fd, buf, count);
    }
    errno = err;
    return -1;
}

/* Not atomic like POSIX: the old destination is deleted first, so an
 * interruption in between leaves only the new file, under its old name. */
static int hz_rename(const char *from, const char *to) {
    if (HZ_RENAME(from, to) == 0) return 0;
    int err = errno;
    if (strcmp(from, to) != 0 && hz_entry_type(from) == 1 && hz_entry_type(to) == 1 &&
        HZ_UNLINK(to) == 0) {
        return HZ_RENAME(from, to);
    }
    errno = err;
    return -1;
}

#endif /* HORIZON_FIXES_H */
