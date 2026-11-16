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
 *
 * Names are a second matter. The console's file system refuses to create
 * some names that aren't plain ASCII (seen with a Japanese one, again as "No
 * such file or directory"), and decks have such names. So a name with
 * anything but ASCII in it is written in ASCII, after a mark:
 *
 *   NAME  ->  ^u^ then NAME with  ^ as ^^,  and each other character by its
 *             Unicode number: ^hhh (below U+0800), ^uhhhh, or ^vhhhhhh
 *
 * which is at most twice as long, so Anki's 120-byte names stay within FAT's
 * 255 characters. The same is done in media.trash, where deleted media goes.
 * Files with such names that are on the card as they are stay so.
 */
#ifndef MEDIA_SHARDS_H
#define MEDIA_SHARDS_H

#include <stdio.h>
#include <string.h>

#define MS_FOLDER "/collection.media"
#define MS_TRASH "/media.trash"
#define MS_MARK "^u^"
/* A file name on the card, with its end. */
#define MS_NAME 256
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

static int ms_ends_with(const char *path, size_t len, const char *end) {
    size_t n = strlen(end);
    return len >= n && memcmp(path + len - n, end, n) == 0;
}

/* Does path[0..len] end with the media folder's name? With the trash's? */
static int ms_is_media_folder(const char *path, size_t len) { return ms_ends_with(path, len, MS_FOLDER); }
static int ms_is_trash(const char *path, size_t len) { return ms_ends_with(path, len, MS_TRASH); }

/* --- names ------------------------------------------------------------------ */

/* Is `name` one to write in ASCII? (One that starts with the mark is too, so
 * that reading a name back is never in doubt.) */
static int ms_needs_encoding(const char *name) {
    if (strncmp(name, MS_MARK, sizeof(MS_MARK) - 1) == 0) return 1;
    for (const unsigned char *c = (const unsigned char *)name; *c; c++)
        if (*c >= 0x80) return 1;
    return 0;
}

/* The fewest bytes UTF-8 takes for a character, by its number. */
static const unsigned ms_least[] = {0, 0, 0x80, 0x800, 0x10000, 0x200000};

/* Write `name` in ASCII to `out`. Returns 0 if it isn't UTF-8 or doesn't fit
 * (the name is then used as it is). */
static int ms_encode_name(const char *name, char *out, size_t cap) {
    const unsigned char *c = (const unsigned char *)name;
    size_t n = sizeof(MS_MARK) - 1;
    if (cap <= n) return 0;
    memcpy(out, MS_MARK, n);
    while (*c) {
        unsigned code;
        int len;
        if (*c < 0x80) code = *c, len = 1;
        else if ((*c & 0xE0) == 0xC0) code = *c & 0x1F, len = 2;
        else if ((*c & 0xF0) == 0xE0) code = *c & 0x0F, len = 3;
        else if ((*c & 0xF8) == 0xF0) code = *c & 0x07, len = 4;
        else return 0;
        for (int i = 1; i < len; i++) {
            if ((c[i] & 0xC0) != 0x80) return 0;
            code = code << 6 | (c[i] & 0x3F);
        }
        if (code < ms_least[len]) return 0;
        if (n + 9 > cap) return 0;
        if (len == 1) {
            if (code == '^') out[n++] = '^';
            out[n++] = (char)code;
        } else {
            n += (size_t)sprintf(out + n, len == 2 ? "^%03x" : len == 3 ? "^u%04x" : "^v%06x", code);
        }
        c += len;
    }
    out[n] = 0;
    return 1;
}

/* The reverse: the name that `stored` is the ASCII writing of, to `out`.
 * Returns 0 if it isn't one (the name is then what it reads). */
static int ms_decode_name(const char *stored, char *out, size_t cap) {
    if (strncmp(stored, MS_MARK, sizeof(MS_MARK) - 1) != 0) return 0;
    const unsigned char *c = (const unsigned char *)stored + sizeof(MS_MARK) - 1;
    size_t n = 0;
    while (*c) {
        if (n + 5 > cap || *c >= 0x80) return 0;
        if (*c != '^' || c[1] == '^') {
            if (*c == '^') c++;
            out[n++] = (char)*c++;
            continue;
        }
        c++;
        int len = *c == 'u' ? 3 : *c == 'v' ? 4 : 2;
        if (len > 2) c++;
        unsigned code = 0;
        for (int digits = len == 4 ? 6 : len + 1; digits; digits--, c++) {
            if (*c >= '0' && *c <= '9') code = code << 4 | (unsigned)(*c - '0');
            else if (*c >= 'a' && *c <= 'f') code = code << 4 | (unsigned)(*c - 'a' + 10);
            else return 0;
        }
        if (code < ms_least[len] || code >= ms_least[len + 1]) return 0;
        static const unsigned char lead[] = {0, 0, 0xC0, 0xE0, 0xF0};
        out[n++] = (char)(lead[len] | code >> (6 * (len - 1)));
        for (int i = len - 2; i >= 0; i--) out[n++] = (char)(0x80 | ((code >> (6 * i)) & 0x3F));
    }
    out[n] = 0;
    return 1;
}

/* --- paths ------------------------------------------------------------------ */

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

/* For a file directly in a media folder or the trash whose name is written in
 * ASCII, write the path of that (in its shard, for media) to `out` and return
 * 1; otherwise return 0. */
static int ms_encoded_path(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    char name[MS_NAME];
    if (!slash || !ms_needs_encoding(slash + 1) || !ms_encode_name(slash + 1, name, sizeof name)) return 0;
    int dir = (int)(slash - path), n;
    if (ms_is_media_folder(path, (size_t)dir))
        n = snprintf(out, cap, "%.*s.d/%02x/%s", dir, path, ms_shard_of(slash + 1), name);
    else if (ms_is_trash(path, (size_t)dir))
        n = snprintf(out, cap, "%.*s/%s", dir, path, name);
    else
        return 0;
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
