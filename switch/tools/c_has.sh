#!/usr/bin/env bash
# Report whether each named macro/type/function exists in the Switch C headers,
# with its value (macros/enums) or size (types). Usage: c_has.sh NAME...
# Prefix a name with "struct:" for struct tags, "fn:" for functions.
source "$(dirname "$0")/../../tools/env.sh"
TMP=$(mktemp -d)
HDRS='#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>'
FLAGS="-march=armv8-a+crc+crypto -mtp=soft -fPIE -D__SWITCH__ -I$DEVKITPRO/libnx/include -w -S -O0 -std=gnu11"
# Keep only headers that compile on their own (devkitA64 lacks some, e.g. sys/uio.h).
HDRS=$(while read -r line; do
    printf '%s\n' "$line" > "$TMP/h.c"
    aarch64-none-elf-gcc $FLAGS "$TMP/h.c" -o /dev/null 2>/dev/null && printf '%s\n' "$line"
done <<< "$HDRS")
for n in "$@"; do
    case "$n" in
        struct:*) e="sizeof(struct ${n#struct:})" ;;
        type:*)   e="sizeof(${n#type:})" ;;
        fn:*)     e="(long long)(void*)&${n#fn:}" ;;
        *)        e="(long long)($n)" ;;
    esac
    printf '%s\n__attribute__((used)) const long long v = %s;\n' "$HDRS" "$e" > "$TMP/p.c"
    if aarch64-none-elf-gcc $FLAGS "$TMP/p.c" -o "$TMP/p.s" 2>/dev/null; then
        val=$(awk '/^v:/{getline; print $2}' "$TMP/p.s")
        [[ -z "$val" ]] && val=0
        [[ "$n" == fn:* ]] && val="declared"
        printf '  %-28s %s\n' "$n" "$val"
    else
        printf '  %-28s MISSING\n' "$n"
    fi
done
rm -rf "$TMP"
