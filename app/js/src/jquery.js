// jQuery, as Anki's reviewer has it (ts/reviewer/index.ts exports $ and
// jQuery from the jquery package): the real library on this DOM. It is set
// up the first time a script touches $ or jQuery, so cards that don't use it
// open faster. A card that loads its own jQuery replaces it, as in Anki.
let jQuery = null;
let loading = false;

function load() {
    if (!jQuery && !loading) {
        loading = true; // jQuery reads window.jQuery while it sets up (noConflict)
        try {
            jQuery = require("jquery");
        } finally {
            loading = false;
        }
    }
    return jQuery || undefined;
}

for (const name of ["$", "jQuery"])
    Object.defineProperty(globalThis, name, {
        get: load,
        set(value) { Object.defineProperty(globalThis, name, { value, writable: true, configurable: true, enumerable: true }); },
        configurable: true,
        enumerable: true,
    });
