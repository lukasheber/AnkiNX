// Anki's reviewer page, ported from ts/reviewer/index.ts (Anki 26.09): a side
// goes into #qa with its scripts re-created so they run, then the update and
// shown hooks run; the globals templates use (pycmd, onShownHook,
// ankiPlatform, anki.*) are the same. At the end, the entry points the app
// calls (globalThis.__ankinx, see app/src/script/card_script.cpp).
//
// Differences from Anki desktop: MathJax is typeset by the app before the
// side gets here, resources are local (no preloading), scrolling to the
// answer is the card view's, and each side fires DOMContentLoaded and load
// once (as AnkiDroid, which loads each side as a page) while globals stay
// from question to answer (as Anki desktop).
import { native, stats } from "./native.js";
import { document, setReadyState } from "./dom.js";
import { activity, clearListeners, dispatchEvent, Event, InputEvent, KeyboardEvent, MouseEvent, PointerEvent, Touch, TouchEvent, touchList } from "./events.js";
import { autofocus, focusFromPointer, formOwner, inputType } from "./forms.js";
import { domVersion } from "./layout.js";
import { mutateNextCardStates } from "./scheduling.js";
import { extraCss, nodeBySerial, serialOf, serializeBody } from "./serialize.js";
import { nextTimer, runTimers } from "./timers.js";

// ------------------------------------------------- ts/reviewer/index.ts --

const onUpdateHook = [];
const onShownHook = [];
const ankiPlatform = "desktop";
let typeans;

function getTypedAnswer() {
    return typeans?.value ?? null;
}

function _runHook(hooks) {
    const promises = [];
    for (const hook of hooks) {
        try {
            promises.push(hook());
        } catch (error) {
            console.log("Hook failed: ", error);
        }
    }
    return Promise.allSettled(promises);
}

let _updatingQueue = Promise.resolve();

function _queueAction(action) {
    _updatingQueue = _updatingQueue.then(action);
}

// Setting innerHTML does not evaluate the contents of script tags, so we need
// to add them again in order to trigger the download/evaluation. Promise
// resolves when download/evaluation has completed.
function replaceScript(oldScript) {
    return new Promise(resolve => {
        const newScript = document.createElement("script");
        let mustWaitForNetwork = true;
        if (oldScript.src) {
            newScript.addEventListener("load", () => resolve());
            newScript.addEventListener("error", () => resolve());
        } else {
            mustWaitForNetwork = false;
        }

        for (const attribute of oldScript.attributes) {
            newScript.setAttribute(attribute.name, attribute.value);
        }

        newScript.appendChild(document.createTextNode(oldScript.innerHTML));
        oldScript.replaceWith(newScript);

        if (!mustWaitForNetwork) {
            resolve();
        }
    });
}

async function setInnerHTML(element, html) {
    element.innerHTML = html;

    for (const oldScript of element.getElementsByTagName("script")) {
        // (A browser's live collection drops scripts an earlier one removed.)
        if (oldScript.isConnected) await replaceScript(oldScript);
    }
}

const renderError = type => error => {
    const errorMessage = String(error).substring(0, 2000);
    const errorStack = error instanceof Error ? String(error.stack).substring(0, 2000) : "";
    return `<div>Invalid ${type} on card: ${errorMessage}\n${errorStack}</div>`.replace(/\n/g, "<br>");
};

async function _updateQA(html, _unused, onupdate, onshown) {
    onUpdateHook.length = 0;
    onUpdateHook.push(onupdate);

    onShownHook.length = 0;
    onShownHook.push(onshown);

    const qa = document.getElementById("qa");

    try {
        await setInnerHTML(qa, html);
    } catch (error) {
        await setInnerHTML(qa, renderError("html")(error));
    }
    pageLoad.parsed();

    await _runHook(onUpdateHook);

    // dynamic toolbar background
    bridgeCommand("updateToolbar");

    await _runHook(onShownHook);
    pageLoad.loaded();
}

function _showQuestion(q, a, bodyclass) {
    _queueAction(() =>
        _updateQA(
            q,
            null,
            function () {
                // return to top of window
                window.scrollTo(0, 0);

                document.body.className = bodyclass;
            },
            function () {
                // focus typing area if visible
                typeans = document.getElementById("typeans");
                if (typeans) {
                    typeans.focus();
                }
            },
        ),
    );
}

function _showAnswer(a, bodyclass) {
    _queueAction(() =>
        _updateQA(
            a,
            null,
            function () {
                if (bodyclass) {
                    //  when previewing
                    document.body.className = bodyclass;
                }
            },
            function () {
                /* noop */
            },
        ),
    );
}

// The header shows the flag and the mark (the reviewer draws them).
function _drawFlag() {}
function _drawMark() {}

function _typeAnsPress() {
    const key = globalThis.event?.key;
    if (key === "Enter") {
        bridgeCommand("ans");
    }
}

function _emulateMobile(enabled) {
    document.documentElement.classList.toggle("mobile", enabled);
}

function _blockDefaultDragDropBehavior() {}

function addBrowserClasses() {
    const ua = navigator.userAgent.toLowerCase();
    const addClass = className => document.documentElement.classList.add(className);
    const test = regex => regex.test(ua);
    if (test(/ipad/)) addClass("ipad");
    else if (test(/iphone/)) addClass("iphone");
    else if (test(/android/)) addClass("android");
    if (test(/ipad|iphone|ipod/)) addClass("ios");
    if (test(/ipad|iphone|ipod|android/)) addClass("mobile");
    else if (test(/linux/)) addClass("linux");
    else if (test(/windows/)) addClass("win");
    else if (test(/mac/)) addClass("mac");
    if (test(/firefox\//)) addClass("firefox");
    else if (test(/chrome\//)) addClass("chrome");
    else if (test(/safari\//)) addClass("safari");
}
addBrowserClasses();

// Anki's web view bridge. Commands the reviewer acts on: "ans" (show the
// answer) and "ease1".."ease4" (answer); the rest are ignored.
function pycmd(cmd) {
    native.command(String(cmd));
    return false;
}
function bridgeCommand(cmd, callback) {
    return pycmd(cmd, callback);
}

// Custom scheduling as in Anki (scheduling.js). Anki's own image occlusion is
// drawn by the app (CardView); templates still call these. MathJax is typeset
// before scripts run.
const anki = globalThis.anki || {};
anki.mutateNextCardStates = mutateNextCardStates;
anki.imageOcclusion = { setup() {}, drawShape() {} };
anki.setupImageCloze = anki.imageOcclusion.setup; // deprecated
const MathJax = {
    typeset() {},
    typesetPromise: () => Promise.resolve(),
    typesetClear() {},
    startup: { promise: Promise.resolve(), document: { state() {} } },
    config: {},
};

// Anki's runtime-require (ts/lib/tslib/runtime-require.ts).
const packages = { "anki/reviewer": { onUpdateHook, onShownHook } };
function require(name) {
    if (!(name in packages)) throw new Error(`Cannot require "${name}" at runtime.`);
    return packages[name];
}

Object.assign(globalThis, {
    onUpdateHook, onShownHook, ankiPlatform, getTypedAnswer, _queueAction, _updateQA, _showQuestion, _showAnswer,
    _drawFlag, _drawMark, _typeAnsPress, _emulateMobile, _blockDefaultDragDropBehavior, pycmd, bridgeCommand, anki,
    MathJax, require,
});

// --------------------------------------------------------- entry points --

// A page load per side, as AnkiDroid loads each side: DOMContentLoaded once
// the side's scripts ran, load after the hooks. Listeners from an earlier
// side have had theirs.
const pageLoad = {
    parsed() {
        setReadyState("interactive");
        autofocus(document.getElementById("qa"));
        dispatchEvent(document, new Event("DOMContentLoaded", { bubbles: true }));
        clearListeners(document, "DOMContentLoaded");
    },
    loaded() {
        setReadyState("complete");
        dispatchEvent(globalThis, new Event("load"));
        clearListeners(globalThis, "load");
    },
};

function show(answer, html, otherSide, bodyClass) {
    // Each side starts afresh: a side whose script was stopped never finished.
    _updatingQueue = Promise.resolve();
    // The page had the card's classes before (Anki desktop keeps the page).
    document.body.className = bodyClass;
    setReadyState("loading");
    if (answer) _showAnswer(html, bodyClass);
    else _showQuestion(html, otherSide, bodyClass);
}

// A side put in #qa as it is, nothing in it run (as markup set with
// innerHTML): the page the collection's custom scheduling code runs on,
// where it may read the question.
function place(html, bodyClass) {
    document.body.className = bodyClass;
    document.getElementById("qa").innerHTML = html;
}

function bodyClass() {
    return document.body.className;
}

// A tap at (x, y) (CSS pixels, page coordinates) on the element with this
// serial: the events a touchscreen tap fires in a browser, then the click.
// True if the card did something with it.
function tap(serial, x, y) {
    const target = nodeBySerial(serial);
    if (!target || !target.isConnected) return false;
    const listenersBefore = activity.elementListeners;
    const commandsBefore = stats.commands;
    const versionBefore = domVersion();

    const clientX = x;
    const clientY = y - (globalThis.scrollY || 0);
    const common = { bubbles: true, cancelable: true, composed: true, view: globalThis, clientX, clientY, screenX: clientX, screenY: clientY };
    const pointer = { ...common, pointerId: 1, pointerType: "touch", isPrimary: true };
    const touch = new Touch({ identifier: 0, target, clientX, clientY, screenX: clientX, screenY: clientY, pageX: x, pageY: y });
    const touches = list => ({ touches: touchList(list), targetTouches: touchList(list), changedTouches: touchList([touch]) });

    const down = dispatchEvent(target, new PointerEvent("pointerdown", { ...pointer, buttons: 1, pressure: 0.5 }), true);
    const started = dispatchEvent(target, new TouchEvent("touchstart", { ...common, ...touches([touch]) }), true);
    dispatchEvent(target, new PointerEvent("pointerup", { ...pointer, pressure: 0 }), true);
    const ended = dispatchEvent(target, new TouchEvent("touchend", { ...common, ...touches([]) }), true);
    let clicked = true;
    // A cancelled touch has no mouse events and no click.
    if (started && ended) {
        if (down) dispatchEvent(target, new MouseEvent("mousedown", { ...common, buttons: 1, detail: 1 }), true);
        focusFromPointer(target);
        globalThis.getSelection().collapse(target, 0);
        if (down) dispatchEvent(target, new MouseEvent("mouseup", { ...common, detail: 1 }), true);
        clicked = dispatchEvent(target, new PointerEvent("click", { ...pointer, detail: 1 }), true);
    }
    // A text field the tap focused: the app opens the keyboard for it.
    const field = clicked && textField(document.activeElement);
    if (field) {
        const maxLength = parseInt(field.getAttribute("maxlength"), 10);
        native.command("keyboard:" + JSON.stringify({
            serial: serialOf(field), value: field.value, multiline: field.localName === "textarea",
            prompt: field.getAttribute("placeholder") || field.getAttribute("aria-label") || field.getAttribute("title") || "",
            maxLength: maxLength > 0 ? maxLength : 0,
        }));
    }
    return !started || !ended || !clicked || activity.elementListeners !== listenersBefore ||
        stats.commands !== commandsBefore || domVersion() !== versionBefore;
}

// Controls that take text from the keyboard: single-line text inputs and textareas.
const TEXT_TYPES = new Set(["text", "search", "email", "url", "tel", "number", "password"]);
function textField(el) {
    if (!el || el.nodeType !== 1 || !el.isConnected || el.hasAttribute("disabled") || el.hasAttribute("readonly")) return null;
    if (el.localName === "textarea" || (el.localName === "input" && TEXT_TYPES.has(inputType(el)))) return el;
    return null;
}

// Text from the keyboard for the field with this serial: what typing it
// does (the value, input and change), then Enter, which in a single-line
// field of a form submits it.
function typed(serial, text) {
    const field = textField(nodeBySerial(serial));
    if (!field) return;
    field.focus();
    field.value = String(text);
    dispatchEvent(field, new InputEvent("input", { bubbles: true, composed: true, inputType: "insertText", data: String(text) }), true);
    dispatchEvent(field, new Event("change", { bubbles: true }), true);
    if (field.localName === "textarea") return;
    const enter = { bubbles: true, cancelable: true, composed: true, view: globalThis, key: "Enter", code: "Enter", keyCode: 13, which: 13 };
    const notCancelled = dispatchEvent(field, new KeyboardEvent("keydown", enter), true);
    if (notCancelled) dispatchEvent(field, new KeyboardEvent("keypress", { ...enter, charCode: 13 }), true);
    dispatchEvent(field, new KeyboardEvent("keyup", enter), true);
    const form = notCancelled && field.isConnected ? formOwner(field) : null;
    if (form) {
        const button = form.querySelector("button:not([type]), button[type=submit], input[type=submit], input[type=image]");
        if (button && !button.hasAttribute("disabled")) button.click();
        else form.requestSubmit();
    }
}

// A key press, to the focused element (or the body).
function key(type, keyName, code, keyCode) {
    const event = new KeyboardEvent(type, { bubbles: true, cancelable: true, composed: true, view: globalThis, key: keyName, code, keyCode });
    return dispatchEvent(document.activeElement || document.body, event, true);
}

Object.defineProperty(globalThis, "__ankinx", {
    value: { show, place, html: serializeBody, bodyClass, extraCss, tap, typed, key, nextTimer, runTimers },
    enumerable: false,
});
