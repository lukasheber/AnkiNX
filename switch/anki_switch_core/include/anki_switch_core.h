/* C interface to Anki's Rust backend (anki_switch_core, Nintendo Switch).
 *
 * Requests and responses are serialized protobuf messages from Anki's
 * proto/ definitions (service/method indices as generated for rslib).
 * All returned buffers are owned by the library: release with anki_buffer_free.
 */
#ifndef ANKI_SWITCH_CORE_H
#define ANKI_SWITCH_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AnkiBackend AnkiBackend;

typedef struct {
    uint8_t *data;
    size_t len;
} AnkiBuffer;

enum {
    ANKI_OK = 0,
    ANKI_BACKEND_ERROR = 1,    /* out holds a serialized anki.backend.BackendError */
    ANKI_INVALID_ARGUMENT = 2, /* null backend/out pointer; out untouched */
};

/* init: serialized anki.backend.BackendInit. Returns NULL on failure and, if
 * err is non-NULL, stores a UTF-8 (not NUL-terminated) message in *err. */
AnkiBackend *anki_backend_open(const uint8_t *init, size_t init_len, AnkiBuffer *err);

int32_t anki_backend_command(const AnkiBackend *backend, uint32_t service, uint32_t method,
                             const uint8_t *data, size_t len, AnkiBuffer *out);

/* JSON-encoded database commands (Anki's db_command protocol). */
int32_t anki_backend_db_command(const AnkiBackend *backend, const uint8_t *data, size_t len,
                                AnkiBuffer *out);

void anki_backend_close(AnkiBackend *backend);

/* JSON command API used by the AnkiNX frontend (anki_switch_core/src/api.rs).
 * args: JSON object or NULL. Returns {"ok": ...} or {"error": "...", "kind": "..."}
 * as a NUL-terminated string; free it with ankinx_free. Blocking: call from a
 * worker thread for anything that touches the network. */
char *ankinx_call(const char *cmd, const char *args);
void ankinx_free(char *json);
void anki_buffer_free(AnkiBuffer buf);
const char *anki_buildhash(void);

#ifdef __cplusplus
}
#endif

#endif /* ANKI_SWITCH_CORE_H */
