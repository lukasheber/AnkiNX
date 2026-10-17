// Link test for libanki_switch_core.a: opens and closes an Anki backend.
// Built by build.sh into anki_link_test.nro (runs under hbmenu; exit with +).
#include <stdio.h>
#include <switch.h>

#include "anki_switch_core.h"

int main(void) {
    consoleInit(NULL);
    socketInitializeDefault(); // the Rust backend's async runtime uses sockets
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("Anki backend %s\n", anki_buildhash());

    // An empty message is a valid default anki.backend.BackendInit.
    AnkiBuffer err = {0};
    AnkiBackend *backend = anki_backend_open(NULL, 0, &err);
    if (backend) {
        printf("backend opened\n");
    } else {
        printf("backend failed: %.*s\n", (int)err.len, (const char *)err.data);
        anki_buffer_free(err);
    }
    printf("press + to exit\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }

    anki_backend_close(backend);
    socketExit();
    consoleExit(NULL);
    return 0;
}
