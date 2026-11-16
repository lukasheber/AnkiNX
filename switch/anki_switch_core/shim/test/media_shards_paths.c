#include <assert.h>
#include "media_shards.h"

int main(void) {
    char out[MS_PATH], other[MS_PATH];
    /* a media file */
    assert(ms_shard_path("/switch/ankinx/profiles/main/collection.media/elements_1.opus", out, sizeof out));
    printf("%s\n", out);
    assert(strncmp(out, "/switch/ankinx/profiles/main/collection.media.d/", 48) == 0);
    assert(strcmp(out + 50, "/elements_1.opus") == 0);
    assert(ms_shard_path("sdmc:/x/collection.media/A.JPG", out, sizeof out));
    assert(ms_shard_path("sdmc:/x/collection.media/a.jpg", other, sizeof other));
    assert(strncmp(out, other, 26) == 0); /* same shard whatever the case */
    /* not media files */
    assert(!ms_shard_path("/x/collection.media", out, sizeof out));
    assert(!ms_shard_path("/x/collection.media/", out, sizeof out));
    assert(!ms_shard_path("/x/collection.media/sub/a.jpg", out, sizeof out));
    assert(!ms_shard_path("/x/collection.media.d/3f/a.jpg", out, sizeof out));
    assert(!ms_shard_path("/x/collection.mdb", out, sizeof out));
    assert(!ms_shard_path("/x/media.trash/a.jpg", out, sizeof out));
    assert(!ms_shard_path("/x/collection.media/..", out, sizeof out));
    assert(!ms_shard_path("a.jpg", out, sizeof out));
    /* too long: left alone */
    char longp[2000] = "/x/collection.media/";
    memset(longp + 20, 'a', 1500);
    assert(!ms_shard_path(longp, out, sizeof out));
    /* folder */
    assert(ms_shard_root("/x/collection.media", out, sizeof out) && !strcmp(out, "/x/collection.media.d"));
    assert(ms_shard_root("/x/collection.media//", out, sizeof out) && !strcmp(out, "/x/collection.media.d"));
    assert(!ms_shard_root("/x/collection.media.d", out, sizeof out));
    assert(!ms_shard_root("/x", out, sizeof out));
    /* names in ASCII, and back */
    static const char *const names[] = {
        "\xef\xbd\x9e\xe5\x86\x86_g.opus",      /* the one the console refused */
        "caf\xc3\xa9.mp3", "^u^plain.jpg", "a^b\xc3\xa3^^.png", "\xf0\x9f\x98\x80.gif",
        "\xc2\x80\xdf\xbf\xe0\xa0\x80\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf",
    };
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        char stored[MS_NAME], back[MS_NAME];
        assert(ms_needs_encoding(names[i]) && ms_encode_name(names[i], stored, sizeof stored));
        printf("%s\n", stored);
        assert(strncmp(stored, MS_MARK, 3) == 0);
        for (char *c = stored; *c; c++) assert(*c > 0x20 && *c < 0x7f);
        assert(ms_decode_name(stored, back, sizeof back) && strcmp(back, names[i]) == 0);
    }
    assert(ms_encode_name("\xef\xbd\x9e\xe5\x86\x86_g.opus", out, sizeof out) && !strcmp(out, "^u^^uff5e^u5186_g.opus"));
    assert(!ms_needs_encoding("plain.jpg") && !ms_needs_encoding("a^b.jpg") && !ms_needs_encoding("^u.jpg"));
    assert(!ms_encode_name("bad\xff.jpg", out, sizeof out) && !ms_encode_name("cut\xe5\x86", out, sizeof out));
    assert(!ms_encode_name("long\xc0\xaf", out, sizeof out)); /* not the shortest form */
    /* not written by this: read as they are */
    static const char *const plain[] = {"a.jpg", "^u.jpg", "^u^^12", "^u^^uzzzz", "^u^^07f", "^u^^u07ff", "^u^^v00ffff",
                                        "^u^^E9", "^u^caf\xc3\xa9", "^u^^"};
    for (size_t i = 0; i < sizeof plain / sizeof *plain; i++) assert(!ms_decode_name(plain[i], out, sizeof out));
    /* Anki's longest names (120 bytes) fit a FAT name, whatever they are made of */
    char longest[121], stored[MS_NAME], back[MS_NAME];
    for (int i = 0; i < 60; i++) memcpy(longest + 2 * i, "\xc3\xa9", 2);
    longest[120] = 0;
    assert(ms_encode_name(longest, stored, sizeof stored) && strlen(stored) == 243);
    assert(ms_decode_name(stored, back, sizeof back) && strcmp(back, longest) == 0);
    for (int i = 0; i < 40; i++) memcpy(longest + 3 * i, "a\xc3\xa9", 3);
    assert(ms_encode_name(longest, stored, sizeof stored) && strlen(stored) < 256);
    /* what doesn't fit is left alone */
    assert(ms_encode_name("\xc3\xa9", back, 12) && !ms_encode_name("\xc3\xa9\xc3\xa9", back, 12));
    /* where they go */
    assert(ms_encoded_path("/x/collection.media/caf\xc3\xa9.mp3", out, sizeof out));
    assert(ms_shard_path("/x/collection.media/caf\xc3\xa9.mp3", other, sizeof other));
    assert(strncmp(out, other, 25) == 0 && strcmp(out + 25, "^u^caf^0e9.mp3") == 0);
    assert(ms_encoded_path("/x/media.trash/caf\xc3\xa9.mp3", out, sizeof out) && !strcmp(out, "/x/media.trash/^u^caf^0e9.mp3"));
    assert(!ms_encoded_path("/x/collection.media/cafe.mp3", out, sizeof out));
    assert(!ms_encoded_path("/x/elsewhere/caf\xc3\xa9.mp3", out, sizeof out));
    assert(!ms_encoded_path("/x/collection.media/sub/caf\xc3\xa9.mp3", out, sizeof out));
    /* spread */
    int count[MS_SHARDS] = {0}, max = 0;
    for (int i = 0; i < 40000; i++) {
        char name[64];
        snprintf(name, sizeof name, "elements_1_28_18_18_%06d.opus", i * 7);
        count[ms_shard_of(name)]++;
    }
    for (int i = 0; i < MS_SHARDS; i++) if (count[i] > max) max = count[i];
    printf("40000 files: fullest shard holds %d\n", max);
    assert(max < 400);
    puts("ok");
    return 0;
}
