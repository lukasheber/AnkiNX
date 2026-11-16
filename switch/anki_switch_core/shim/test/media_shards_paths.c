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
