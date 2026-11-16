/* Where a collection's media files are kept on the SD card (path arithmetic
 * only; nx_media_shards.c applies it to the Switch's file system).
 *
 * Anki keeps all media in one folder, collection.media. On FAT32, the format
 * of nearly every Switch SD card, a folder holds 65,536 entries and a long
 * file name takes several: around 17,000 media files fill it, and creating
 * the next one fails ("No such file or directory"). So new files go to 256
 * folders beside it, chosen by the file's name:
 *
 *   .../collection.media/NAME  ->  .../collection.media.d/XX/NAME
 *
 * Files already in collection.media (from before, or copied from a computer)
 * stay there and are still found.
 */
#ifndef MEDIA_SHARDS_H
#define MEDIA_SHARDS_H

#include <stdio.h>
#include <string.h>

#define MS_FOLDER "/collection.media"
#define MS_SHARDS 256
/* Horizon's longest path (FS_MAX_PATH), with room for a device name. */
#define MS_PATH 0x340

/* The shard of a file name. FAT ignores the case of names, so this does too
 * (for ASCII, which is all it can know about). */
static unsigned ms_shard_of(const char *name) {
    unsigned hash = 2166136261u; /* FNV-1a */
    for (const unsigned char *c = (const unsigned char *)name; *c; c++) {
        hash ^= (*c >= 'A' && *c <= 'Z') ? *c + 32u : *c;
        hash *= 16777619u;
    }
    return (hash ^ (hash >> 8) ^ (hash >> 16) ^ (hash >> 24)) % MS_SHARDS;
}

/* Length of `path` without trailing slashes. */
static size_t ms_trimmed(const char *path) {
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') len--;
    return len;
}

/* Does path[0..len] end with the media folder's name? */
static int ms_is_media_folder(const char *path, size_t len) {
    size_t folder = sizeof(MS_FOLDER) - 1;
    return len >= folder && memcmp(path + len - folder, MS_FOLDER, folder) == 0;
}

/* For a file directly in a media folder, write its sharded path to `out` and
 * return 1; otherwise return 0 (the path is used as given). */
static int ms_shard_path(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    if (!slash || !slash[1] || !ms_is_media_folder(path, (size_t)(slash - path))) return 0;
    const char *name = slash + 1;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    int n = snprintf(out, cap, "%.*s.d/%02x/%s", (int)(slash - path), path, ms_shard_of(name), name);
    return n > 0 && (size_t)n < cap;
}

/* For a media folder, write the folder holding its shards to `out` and return
 * 1; otherwise return 0. */
static int ms_shard_root(const char *path, char *out, size_t cap) {
    size_t len = ms_trimmed(path);
    if (!ms_is_media_folder(path, len)) return 0;
    int n = snprintf(out, cap, "%.*s.d", (int)len, path);
    return n > 0 && (size_t)n < cap;
}

#endif /* MEDIA_SHARDS_H */
