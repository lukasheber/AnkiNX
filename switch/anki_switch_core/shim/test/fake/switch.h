/* Host stand-in for libnx, for testing nx_media_shards.c. */
#pragma once
#include <sys/stat.h>
#define FS_MAX_PATH 0x301
typedef struct { int unused; } FsFileSystem;
typedef int FsDirEntryType;
typedef int Result;
#define R_SUCCEEDED(rc) ((rc) == 0)
static inline int fsdevTranslatePath(const char *path, FsFileSystem **fs, char *out) {
    (void)fs;
    snprintf(out, FS_MAX_PATH, "%s", path);
    return 0;
}
static inline Result fsFsGetEntryType(FsFileSystem *fs, const char *path, FsDirEntryType *type) {
    struct stat st;
    (void)fs;
    if (stat(path, &st) != 0) return 1;
    *type = S_ISDIR(st.st_mode);
    return 0;
}
