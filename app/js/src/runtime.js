// JavaScript for card templates in AnkiNX: a browser page for QuickJS.
// Bundled by app/js/build.sh into app/resources/script/runtime.js, which
// app/src/script/card_script.cpp loads into each card's context. Order
// matters: each part builds on the ones before it.
import "./native.js";     // the app's bridge
import "./dom.js";        // linkedom's document and classes on the global object
import "./events.js";     // event dispatch as the DOM Standard has it
import "./errors.js";     // uncaught errors -> log and window "error"
import "./style.js";      // element.style
import "./url.js";
import "./window.js";     // navigator, location, storage, console, timers...
import "./forms.js";      // form controls, activation behaviour, focus
import "./scripts.js";    // <script> execution
import "./net.js";        // fetch and XMLHttpRequest for media files
import "./canvas.js";     // <canvas> 2D drawing, recorded as SVG
import "./serialize.js";  // the DOM as HTML for litehtml
import "./layout.js";     // geometry and computed style from litehtml
import "./jquery.js";     // $ and jQuery
import "./reviewer.js";   // Anki's reviewer page and the app's entry points
