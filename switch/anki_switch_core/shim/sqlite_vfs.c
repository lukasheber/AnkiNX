// "ankinx" SQLite VFS: SQLite's lock-free "unix-none" VFS, plus a fix-up that
// lets this build open collections saved in WAL mode.
//
// SQLite is built with SQLITE_OMIT_WAL (Horizon has no shared memory for the
// WAL index), and such a build rejects any database whose header says WAL
// (bytes 18 and 19 = 2) as "file is not a database" (SQLITE_NOTADB). Anki
// keeps collections in WAL mode, so that is the state of the collection
// AnkiWeb sends on a full download and of collection.anki2 copied from Anki
// desktop. When no -wal file is left beside it (Anki checkpoints on close;
// downloads never have one), a WAL-mode database is an ordinary database,
// and the only difference is those two bytes. So reads of the main database
// file report them as 1 (rollback journal), which is exactly what
// "PRAGMA journal_mode=delete" would write, and the next write of page 1
// stores that.
#include <sqlite3.h>
#include <string.h>

typedef struct
{
    sqlite3_file base; // must be first
    sqlite3_file* real; // the unix-none file, allocated right after this struct
    int mainDb;
} AnkiFile;

static sqlite3_vfs* realVfs;
static sqlite3_vfs ankiVfs;

#define REAL(f) (((AnkiFile*)(f))->real)

static int xClose(sqlite3_file* f)
{
    int rc = REAL(f)->pMethods->xClose(REAL(f));
    f->pMethods = NULL;
    return rc;
}

static int xRead(sqlite3_file* f, void* buf, int amount, sqlite3_int64 offset)
{
    int rc = REAL(f)->pMethods->xRead(REAL(f), buf, amount, offset);
    if (rc == SQLITE_OK && ((AnkiFile*)f)->mainDb && offset <= 18 && offset + amount >= 20)
    {
        unsigned char* header = (unsigned char*)buf - offset;
        if (header[18] == 2 && header[19] == 2)
            header[18] = header[19] = 1;
    }
    return rc;
}

static int xWrite(sqlite3_file* f, const void* buf, int amount, sqlite3_int64 offset)
{
    return REAL(f)->pMethods->xWrite(REAL(f), buf, amount, offset);
}

static int xTruncate(sqlite3_file* f, sqlite3_int64 size)
{
    return REAL(f)->pMethods->xTruncate(REAL(f), size);
}

static int xSync(sqlite3_file* f, int flags)
{
    return REAL(f)->pMethods->xSync(REAL(f), flags);
}

static int xFileSize(sqlite3_file* f, sqlite3_int64* size)
{
    return REAL(f)->pMethods->xFileSize(REAL(f), size);
}

static int xLock(sqlite3_file* f, int level)
{
    return REAL(f)->pMethods->xLock(REAL(f), level);
}

static int xUnlock(sqlite3_file* f, int level)
{
    return REAL(f)->pMethods->xUnlock(REAL(f), level);
}

static int xCheckReservedLock(sqlite3_file* f, int* out)
{
    return REAL(f)->pMethods->xCheckReservedLock(REAL(f), out);
}

static int xFileControl(sqlite3_file* f, int op, void* arg)
{
    return REAL(f)->pMethods->xFileControl(REAL(f), op, arg);
}

static int xSectorSize(sqlite3_file* f)
{
    return REAL(f)->pMethods->xSectorSize(REAL(f));
}

static int xDeviceCharacteristics(sqlite3_file* f)
{
    return REAL(f)->pMethods->xDeviceCharacteristics(REAL(f));
}

// Version 1: no shared memory (WAL is compiled out) and no memory mapping
// (SQLITE_MAX_MMAP_SIZE=0).
static const sqlite3_io_methods ankiMethods = {
    1,
    xClose,
    xRead,
    xWrite,
    xTruncate,
    xSync,
    xFileSize,
    xLock,
    xUnlock,
    xCheckReservedLock,
    xFileControl,
    xSectorSize,
    xDeviceCharacteristics,
};

static int xOpen(sqlite3_vfs* vfs, const char* name, sqlite3_file* f, int flags, int* outFlags)
{
    AnkiFile* file = (AnkiFile*)f;
    memset(file, 0, sizeof(*file));
    file->real   = (sqlite3_file*)(file + 1);
    file->mainDb = (flags & SQLITE_OPEN_MAIN_DB) != 0;
    int rc       = realVfs->xOpen(realVfs, name, file->real, flags, outFlags);
    // SQLite only calls xClose when pMethods is set.
    f->pMethods = (rc == SQLITE_OK && file->real->pMethods) ? &ankiMethods : NULL;
    return rc;
}

static int xDelete(sqlite3_vfs* vfs, const char* name, int syncDir)
{
    return realVfs->xDelete(realVfs, name, syncDir);
}

static int xAccess(sqlite3_vfs* vfs, const char* name, int flags, int* out)
{
    return realVfs->xAccess(realVfs, name, flags, out);
}

static int xFullPathname(sqlite3_vfs* vfs, const char* name, int n, char* out)
{
    return realVfs->xFullPathname(realVfs, name, n, out);
}

static void* xDlOpen(sqlite3_vfs* vfs, const char* name)
{
    return realVfs->xDlOpen(realVfs, name);
}

static void xDlError(sqlite3_vfs* vfs, int n, char* out)
{
    realVfs->xDlError(realVfs, n, out);
}

static void (*xDlSym(sqlite3_vfs* vfs, void* handle, const char* symbol))(void)
{
    return realVfs->xDlSym(realVfs, handle, symbol);
}

static void xDlClose(sqlite3_vfs* vfs, void* handle)
{
    realVfs->xDlClose(realVfs, handle);
}

static int xRandomness(sqlite3_vfs* vfs, int n, char* out)
{
    return realVfs->xRandomness(realVfs, n, out);
}

static int xSleep(sqlite3_vfs* vfs, int micros)
{
    return realVfs->xSleep(realVfs, micros);
}

static int xCurrentTime(sqlite3_vfs* vfs, double* out)
{
    return realVfs->xCurrentTime(realVfs, out);
}

static int xGetLastError(sqlite3_vfs* vfs, int n, char* out)
{
    return realVfs->xGetLastError ? realVfs->xGetLastError(realVfs, n, out) : 0;
}

static int xCurrentTimeInt64(sqlite3_vfs* vfs, sqlite3_int64* out)
{
    return realVfs->xCurrentTimeInt64(realVfs, out);
}

// Register "ankinx" as the default VFS. Call once, before opening databases.
int ankinx_register_sqlite_vfs(void)
{
    int rc = sqlite3_initialize();
    if (rc != SQLITE_OK)
        return rc;
    realVfs = sqlite3_vfs_find("unix-none");
    if (!realVfs || realVfs->iVersion < 2)
        return SQLITE_ERROR;
    ankiVfs = (sqlite3_vfs) {
        .iVersion          = 2,
        .szOsFile          = (int)sizeof(AnkiFile) + realVfs->szOsFile,
        .mxPathname        = realVfs->mxPathname,
        .zName             = "ankinx",
        .xOpen             = xOpen,
        .xDelete           = xDelete,
        .xAccess           = xAccess,
        .xFullPathname     = xFullPathname,
        .xDlOpen           = xDlOpen,
        .xDlError          = xDlError,
        .xDlSym            = xDlSym,
        .xDlClose          = xDlClose,
        .xRandomness       = xRandomness,
        .xSleep            = xSleep,
        .xCurrentTime      = xCurrentTime,
        .xGetLastError     = xGetLastError,
        .xCurrentTimeInt64 = xCurrentTimeInt64,
    };
    return sqlite3_vfs_register(&ankiVfs, 1);
}
