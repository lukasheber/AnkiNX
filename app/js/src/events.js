// DOM events as the DOM Standard dispatches them
// (https://dom.spec.whatwg.org/#concept-event-dispatch): a capture pass from
// the window down to the target and a bubble pass back up, `this` is the
// current target, listener options (capture, once, passive, signal), event
// handler properties and on… attributes (HTML's "event handlers"), and the
// activation behaviour hook used by forms.js. Replaces linkedom's simplified
// EventTarget, which has no capture phase and calls listeners on the target.
import * as linkedom from "linkedom";

const NONE = 0;
const CAPTURING_PHASE = 1;
const AT_TARGET = 2;
const BUBBLING_PHASE = 3;

// ------------------------------------------------------------------ events --

const internal = new WeakMap(); // event -> dispatch state

function state(event) {
    let s = internal.get(event);
    if (!s) {
        s = {
            type: "", init: {}, initialized: true, stop: false, stopImmediate: false, canceled: false,
            passive: false, dispatching: false, trusted: false, target: null, currentTarget: null,
            phase: NONE, path: [], timeStamp: performance.now(),
        };
        internal.set(event, s);
    }
    return s;
}

export class Event {
    constructor(type, init) {
        if (arguments.length < 1) throw new TypeError(`Failed to construct '${new.target.name}': 1 argument required.`);
        const s = state(this);
        s.type = String(type);
        const defaults = new.target.defaults || Event.defaults;
        for (const key in defaults) s.init[key] = init && init[key] !== undefined ? init[key] : defaults[key];
        s.init.bubbles = !!s.init.bubbles;
        s.init.cancelable = !!s.init.cancelable;
        s.init.composed = !!s.init.composed;
    }

    get type() { return state(this).type; }
    get target() { return state(this).target; }
    get srcElement() { return state(this).target; }
    get currentTarget() { return state(this).currentTarget; }
    get eventPhase() { return state(this).phase; }
    get bubbles() { return state(this).init.bubbles; }
    get cancelable() { return state(this).init.cancelable; }
    get composed() { return state(this).init.composed; }
    get defaultPrevented() { return state(this).canceled; }
    get isTrusted() { return state(this).trusted; }
    get timeStamp() { return state(this).timeStamp; }
    get returnValue() { return !state(this).canceled; }
    set returnValue(value) { if (!value) this.preventDefault(); }
    get cancelBubble() { return state(this).stop; }
    set cancelBubble(value) { if (value) state(this).stop = true; }

    preventDefault() {
        const s = state(this);
        if (s.init.cancelable && !s.passive) s.canceled = true;
    }
    stopPropagation() { state(this).stop = true; }
    stopImmediatePropagation() {
        const s = state(this);
        s.stop = s.stopImmediate = true;
    }
    composedPath() {
        const s = state(this);
        return s.dispatching ? s.path.slice() : [];
    }
    initEvent(type, bubbles = false, cancelable = false) {
        const s = state(this);
        if (s.dispatching) return;
        Object.assign(s, { type: String(type), initialized: true, stop: false, stopImmediate: false, canceled: false, target: null });
        s.init.bubbles = !!bubbles;
        s.init.cancelable = !!cancelable;
    }
}
Event.defaults = { bubbles: false, cancelable: false, composed: false };
for (const [name, value] of Object.entries({ NONE, CAPTURING_PHASE, AT_TARGET, BUBBLING_PHASE })) {
    Object.defineProperty(Event, name, { value, enumerable: true });
    Object.defineProperty(Event.prototype, name, { value, enumerable: true });
}

// An Event subclass whose extra fields come from its init dictionary.
function eventClass(name, Base, defaults, members = {}) {
    const C = { [name]: class extends Base {} }[name];
    C.defaults = { ...Base.defaults, ...defaults };
    for (const key of Object.keys(defaults))
        Object.defineProperty(C.prototype, key, { get() { return state(this).init[key]; }, configurable: true, enumerable: true });
    for (const [key, value] of Object.entries(Object.getOwnPropertyDescriptors(members)))
        Object.defineProperty(C.prototype, key, value);
    return C;
}

const modifiers = { ctrlKey: false, shiftKey: false, altKey: false, metaKey: false };
function getModifierState(key) {
    const map = { Control: "ctrlKey", Shift: "shiftKey", Alt: "altKey", Meta: "metaKey" };
    return !!(map[key] && this[map[key]]);
}
function legacyInit(event, type, bubbles, cancelable, fields) {
    if (state(event).dispatching) return;
    event.initEvent(type, bubbles, cancelable);
    Object.assign(state(event).init, fields);
}

export const CustomEvent = eventClass("CustomEvent", Event, { detail: null }, {
    initCustomEvent(type, bubbles, cancelable, detail) { legacyInit(this, type, bubbles, cancelable, { detail }); },
});
export const UIEvent = eventClass("UIEvent", Event, { view: null, detail: 0 }, {
    initUIEvent(type, bubbles, cancelable, view, detail) { legacyInit(this, type, bubbles, cancelable, { view, detail }); },
});
export const MouseEvent = eventClass("MouseEvent", UIEvent, {
    screenX: 0, screenY: 0, clientX: 0, clientY: 0, ...modifiers, button: 0, buttons: 0, relatedTarget: null,
    movementX: 0, movementY: 0,
}, {
    get x() { return this.clientX; },
    get y() { return this.clientY; },
    get pageX() { return this.clientX + (globalThis.scrollX || 0); },
    get pageY() { return this.clientY + (globalThis.scrollY || 0); },
    get offsetX() { const t = this.target; return t && t.getBoundingClientRect ? this.clientX - t.getBoundingClientRect().left : this.clientX; },
    get offsetY() { const t = this.target; return t && t.getBoundingClientRect ? this.clientY - t.getBoundingClientRect().top : this.clientY; },
    get which() { return this.button + 1; },
    getModifierState,
    initMouseEvent(type, bubbles, cancelable, view, detail, screenX, screenY, clientX, clientY, ctrlKey, altKey, shiftKey, metaKey, button, relatedTarget) {
        legacyInit(this, type, bubbles, cancelable, { view, detail, screenX, screenY, clientX, clientY, ctrlKey, altKey, shiftKey, metaKey, button, relatedTarget });
    },
});
export const PointerEvent = eventClass("PointerEvent", MouseEvent, {
    pointerId: 0, width: 1, height: 1, pressure: 0, tangentialPressure: 0, tiltX: 0, tiltY: 0, twist: 0,
    altitudeAngle: Math.PI / 2, azimuthAngle: 0, pointerType: "", isPrimary: false,
}, {
    getCoalescedEvents() { return []; },
    getPredictedEvents() { return []; },
});
export const WheelEvent = eventClass("WheelEvent", MouseEvent, { deltaX: 0, deltaY: 0, deltaZ: 0, deltaMode: 0 });
export const KeyboardEvent = eventClass("KeyboardEvent", UIEvent, {
    key: "", code: "", location: 0, ...modifiers, repeat: false, isComposing: false, charCode: 0, keyCode: 0,
}, {
    get which() { return this.keyCode || this.charCode; },
    getModifierState,
    initKeyboardEvent(type, bubbles, cancelable, view, key, location, ctrlKey, altKey, shiftKey, metaKey) {
        legacyInit(this, type, bubbles, cancelable, { view, key, location, ctrlKey, altKey, shiftKey, metaKey });
    },
});
export const FocusEvent = eventClass("FocusEvent", UIEvent, { relatedTarget: null });
export const InputEvent = eventClass("InputEvent", UIEvent, { data: null, inputType: "", isComposing: false, dataTransfer: null }, {
    getTargetRanges() { return []; },
});
export const CompositionEvent = eventClass("CompositionEvent", UIEvent, { data: "" });
export const TouchEvent = eventClass("TouchEvent", UIEvent, { touches: [], targetTouches: [], changedTouches: [], ...modifiers });
export const ErrorEvent = eventClass("ErrorEvent", Event, { message: "", filename: "", lineno: 0, colno: 0, error: undefined });
export const ProgressEvent = eventClass("ProgressEvent", Event, { lengthComputable: false, loaded: 0, total: 0 });
export const SubmitEvent = eventClass("SubmitEvent", Event, { submitter: null });
export const ToggleEvent = eventClass("ToggleEvent", Event, { oldState: "", newState: "" });
export const TransitionEvent = eventClass("TransitionEvent", Event, { propertyName: "", elapsedTime: 0, pseudoElement: "" });
export const AnimationEvent = eventClass("AnimationEvent", Event, { animationName: "", elapsedTime: 0, pseudoElement: "" });
export const HashChangeEvent = eventClass("HashChangeEvent", Event, { oldURL: "", newURL: "" });
export const PopStateEvent = eventClass("PopStateEvent", Event, { state: null });
export const PageTransitionEvent = eventClass("PageTransitionEvent", Event, { persisted: false });
export const MessageEvent = eventClass("MessageEvent", Event, { data: null, origin: "", lastEventId: "", source: null, ports: [] });
export const StorageEvent = eventClass("StorageEvent", Event, { key: null, oldValue: null, newValue: null, url: "", storageArea: null });
export const PromiseRejectionEvent = eventClass("PromiseRejectionEvent", Event, { promise: null, reason: undefined });

export class Touch {
    constructor(init = {}) {
        const defaults = {
            identifier: 0, target: null, clientX: 0, clientY: 0, screenX: 0, screenY: 0, pageX: 0, pageY: 0,
            radiusX: 1, radiusY: 1, rotationAngle: 0, force: 1,
        };
        Object.assign(this, defaults, init);
    }
}
export function touchList(touches) {
    const list = [...touches];
    list.item = i => list[i] || null;
    return list;
}

// document.createEvent(): an event to set up with initEvent() and the like.
const legacyNames = {
    event: Event, events: Event, htmlevents: Event, customevent: CustomEvent, uievent: UIEvent, uievents: UIEvent,
    mouseevent: MouseEvent, mouseevents: MouseEvent, keyboardevent: KeyboardEvent, focusevent: FocusEvent,
    touchevent: TouchEvent, compositionevent: CompositionEvent, errorevent: ErrorEvent, messageevent: MessageEvent,
};
export function createEvent(name) {
    const C = legacyNames[String(name).toLowerCase()];
    if (!C) throw new DOMException(`The provided event type ('${name}') is invalid.`, "NotSupportedError");
    const event = new C("");
    state(event).initialized = false;
    return event;
}

// --------------------------------------------------------------- listeners --

const listeners = new WeakMap(); // target -> Map(type -> [record])

// Counts of listeners run, for the reviewer: a tap that runs card code
// doesn't also flip the card.
export const activity = { listeners: 0, elementListeners: 0 };

function flags(options) {
    if (typeof options === "boolean") return { capture: options, once: false, passive: false, signal: null };
    const o = options || {};
    return { capture: !!o.capture, once: !!o.once, passive: !!o.passive, signal: o.signal || null };
}

function listFor(target, type, create) {
    let byType = listeners.get(target);
    if (!byType) {
        if (!create) return null;
        listeners.set(target, (byType = new Map()));
    }
    let list = byType.get(type);
    if (!list && create) byType.set(type, (list = []));
    return list || null;
}

export function addEventListener(target, type, callback, options) {
    if (callback == null) return;
    const { capture, once, passive, signal } = flags(options);
    if (signal && signal.aborted) return;
    type = String(type);
    const list = listFor(target, type, true);
    if (list.some(r => r.callback === callback && r.capture === capture)) return;
    const record = { callback, capture, once, passive, removed: false };
    list.push(record);
    if (signal) signal.addEventListener("abort", () => removeRecord(target, type, record));
}

export function removeEventListener(target, type, callback, options) {
    const capture = flags(options).capture;
    const list = listFor(target, String(type), false);
    const record = list && list.find(r => r.callback === callback && r.capture === capture);
    if (record) removeRecord(target, String(type), record);
}

function removeRecord(target, type, record) {
    record.removed = true;
    const list = listFor(target, type, false);
    const i = list ? list.indexOf(record) : -1;
    if (i >= 0) list.splice(i, 1);
}

// Forget the listeners for `type` on `target`, handlers included (they
// register again when set). For page load events, which fire once.
export function clearListeners(target, type) {
    const list = listFor(target, type, false);
    if (list) for (const record of list.splice(0)) record.removed = true;
    const byType = slots.get(target);
    if (byType && byType.has(type)) byType.get(type).registered = false;
}

// Has listeners or handlers for any of `types` (the reviewer offers such
// elements to the controller).
export function hasListener(target, types) {
    for (const type of types) {
        const list = listFor(target, type, false);
        if (list && list.length) return true;
        if (target.nodeType === 1 && target.hasAttribute("on" + type)) return true;
    }
    return false;
}

// ---------------------------------------------------------------- dispatch --

// Activation behaviour (forms.js): elements with a default action for clicks.
export const activation = {
    has: () => false,         // (element) -> boolean
    preActivate: () => null,  // (element, event) -> undo function or null
    activate: () => {},       // (element, event)
};

function parentFor(node, event) {
    if (node === globalThis) return null;
    if (node === globalThis.document) return event.type === "load" ? null : globalThis;
    if (node.nodeType === 11 && node.host) return node.host; // a shadow root
    return node.parentNode || null;
}

function invoke(node, event, s, capturing) {
    if (node.nodeType === 1 && !isRegistered(node, s.type) && node.hasAttribute("on" + s.type))
        registerHandler(node, s.type, true); // a parsed on… attribute: registered when parsed, so first
    const list = listFor(node, s.type, false);
    if (!list || !list.length) return;
    const element = node.nodeType === 1 && node.localName !== "html" && node.localName !== "body";
    for (const record of list.slice()) {
        if (record.removed || record.capture !== capturing) continue;
        if (record.once) removeRecord(node, s.type, record);
        s.passive = record.passive;
        if (!record.handler) {
            activity.listeners++;
            if (element) activity.elementListeners++;
        }
        try {
            const cb = record.callback;
            if (typeof cb === "function") cb.call(node, event);
            else if (cb && typeof cb.handleEvent === "function") cb.handleEvent(event);
        } catch (e) {
            reportListenerError(e);
        }
        s.passive = false;
        if (s.stopImmediate) return;
    }
}

let reportListenerError = e => { throw e; };
export function setErrorReporter(report) { reportListenerError = report; }

// Dispatch `event` at `target`. `trusted`: sent by the app (a tap, a key).
export function dispatchEvent(target, event, trusted = false) {
    if (!(event instanceof Event)) return dispatchForeign(target, event);
    const s = state(event);
    if (s.dispatching || !s.initialized)
        throw new DOMException("Failed to execute 'dispatchEvent': the event is already being dispatched or not initialised.", "InvalidStateError");
    s.dispatching = true;
    s.trusted = trusted;
    s.target = target;
    s.stop = s.stopImmediate = false;
    const path = [];
    for (let node = target; node; node = parentFor(node, event)) path.push(node);
    s.path = path;

    // A click runs the activation behaviour of the target or its nearest
    // ancestor that has one (checkbox, label, link...).
    let activationTarget = null;
    let undo = null;
    if (event instanceof MouseEvent && s.type === "click") {
        activationTarget = path.find((node, i) => node.nodeType === 1 && (i === 0 || s.init.bubbles) && activation.has(node)) || null;
        if (activationTarget) undo = activation.preActivate(activationTarget, event);
    }

    for (let i = path.length - 1; i >= 0 && !s.stop; i--) {
        s.phase = path[i] === target ? AT_TARGET : CAPTURING_PHASE;
        s.currentTarget = path[i];
        invoke(path[i], event, s, true);
    }
    for (let i = 0; i < path.length && !s.stop; i++) {
        if (path[i] === target) s.phase = AT_TARGET;
        else if (!s.init.bubbles) continue;
        else s.phase = BUBBLING_PHASE;
        s.currentTarget = path[i];
        invoke(path[i], event, s, false);
    }

    s.phase = NONE;
    s.currentTarget = null;
    s.dispatching = false;
    s.stop = s.stopImmediate = false;
    s.path = [];
    if (activationTarget) {
        if (!s.canceled) activation.activate(activationTarget, event);
        else if (undo) undo();
    }
    return !s.canceled;
}

// An event object made elsewhere (linkedom's own classes): dispatched as a
// CustomEvent with the same fields.
function dispatchForeign(target, foreign) {
    const event = new CustomEvent(foreign.type, { bubbles: foreign.bubbles, cancelable: foreign.cancelable, detail: foreign.detail });
    const result = dispatchEvent(target, event);
    if (!result) foreign.defaultPrevented = true;
    return result;
}

// ---------------------------------------------------------- event handlers --

// HTML's event handlers: onclick = fn, or an onclick="..." attribute, each
// a single listener that calls the current handler. The value set last wins.
const slots = new WeakMap(); // target -> Map(type -> {kind: "fn" | "attr", fn, registered})
const compiled = new WeakMap(); // element -> Map(type -> {code, fn})

function slotFor(target, type) {
    let byType = slots.get(target);
    if (!byType) slots.set(target, (byType = new Map()));
    let slot = byType.get(type);
    if (!slot) byType.set(type, (slot = { kind: "attr", fn: null, registered: false }));
    return slot;
}

function isRegistered(target, type) {
    const byType = slots.get(target);
    return !!(byType && byType.has(type) && byType.get(type).registered);
}

function registerHandler(target, type, first) {
    const slot = slotFor(target, type);
    if (slot.registered) return;
    slot.registered = true;
    const record = {
        callback(event) {
            const fn = currentHandler(target, type);
            if (typeof fn !== "function") return;
            activity.listeners++;
            if (target.nodeType === 1 && target.localName !== "html" && target.localName !== "body") activity.elementListeners++;
            if (type === "error" && target === globalThis && event instanceof ErrorEvent) {
                if (fn.call(target, event.message, event.filename, event.lineno, event.colno, event.error) === true) event.preventDefault();
            } else if (fn.call(target, event) === false) event.preventDefault();
        },
        capture: false, once: false, passive: false, removed: false, handler: true,
    };
    const list = listFor(target, type, true);
    if (first) list.unshift(record);
    else list.push(record);
}

function compile(element, type, code) {
    let byType = compiled.get(element);
    if (!byType) compiled.set(element, (byType = new Map()));
    const cached = byType.get(type);
    if (cached && cached.code === code) return cached.fn;
    let fn = null;
    try {
        // The scope chain browsers give attribute handlers: the element, its
        // form, then the document.
        fn = new Function("event",
            "with (this.ownerDocument || {}) with (this.form || {}) with (this) " +
            "return (function (event) {\n" + code + "\n}).call(this, event);");
    } catch (e) {
        reportListenerError(e);
    }
    byType.set(type, { code, fn });
    return fn;
}

function currentHandler(target, type) {
    const byType = slots.get(target);
    const slot = byType && byType.get(type);
    if (slot && slot.kind === "fn") return slot.fn;
    if (target.nodeType === 1) {
        const code = target.getAttribute("on" + type);
        if (code !== null) return compile(target, type, code);
    }
    return null;
}

// An on… attribute was set by a script (setAttribute): it is the handler now.
export function handlerAttributeChanged(element, name) {
    const type = name.slice(2);
    slotFor(element, type).kind = "attr";
    if (element.hasAttribute(name)) registerHandler(element, type, false);
}

function defineHandlers(target, types) {
    for (const type of types)
        Object.defineProperty(target, "on" + type, {
            get() { return currentHandler(this, type); },
            set(value) {
                const slot = slotFor(this, type);
                slot.kind = "fn";
                slot.fn = typeof value === "function" ? value : null;
                if (slot.fn) registerHandler(this, type, false);
            },
            configurable: true,
            enumerable: true,
        });
}

const GLOBAL_HANDLERS = (
    "abort auxclick beforeinput blur cancel canplay canplaythrough change click close contextmenu copy cut " +
    "dblclick drag dragend dragenter dragleave dragover dragstart drop durationchange emptied ended error " +
    "focus focusin focusout input invalid keydown keypress keyup load loadeddata loadedmetadata loadstart " +
    "mousedown mouseenter mouseleave mousemove mouseout mouseover mouseup paste pause play playing " +
    "pointercancel pointerdown pointerenter pointerleave pointermove pointerout pointerover pointerup " +
    "gotpointercapture lostpointercapture progress ratechange reset resize scroll scrollend seeked seeking " +
    "select selectionchange selectstart slotchange stalled submit suspend timeupdate toggle " +
    "touchstart touchend touchmove touchcancel transitionstart transitionrun transitionend transitioncancel " +
    "animationstart animationend animationiteration animationcancel volumechange waiting wheel"
).split(" ");
const WINDOW_HANDLERS = (
    "afterprint beforeprint beforeunload hashchange languagechange message messageerror offline online " +
    "pagehide pageshow popstate rejectionhandled storage unhandledrejection unload"
).split(" ");

// ------------------------------------------------------------------ install --

const ETP = linkedom.EventTarget.prototype;
ETP.addEventListener = function (type, callback, options) { addEventListener(this, type, callback, options); };
ETP.removeEventListener = function (type, callback, options) { removeEventListener(this, type, callback, options); };
ETP.dispatchEvent = function (event) { return dispatchEvent(this, event); };

defineHandlers(linkedom.Element.prototype, GLOBAL_HANDLERS);
defineHandlers(linkedom.HTMLElement.prototype, GLOBAL_HANDLERS); // over linkedom's own
defineHandlers(linkedom.Document.prototype, [...GLOBAL_HANDLERS, "readystatechange", "visibilitychange"]);
defineHandlers(globalThis, [...GLOBAL_HANDLERS, ...WINDOW_HANDLERS]);
Object.assign(globalThis, {
    addEventListener(type, callback, options) { addEventListener(globalThis, type, callback, options); },
    removeEventListener(type, callback, options) { removeEventListener(globalThis, type, callback, options); },
    dispatchEvent(event) { return dispatchEvent(globalThis, event); },
});

export class AbortSignal extends linkedom.EventTarget {
    constructor() {
        super();
        this.aborted = false;
        this.reason = undefined;
    }
    throwIfAborted() { if (this.aborted) throw this.reason; }
    static abort(reason) {
        const controller = new AbortController();
        controller.abort(reason);
        return controller.signal;
    }
    static timeout(ms) {
        const controller = new AbortController();
        globalThis.setTimeout(() => controller.abort(new DOMException("The operation timed out.", "TimeoutError")), ms);
        return controller.signal;
    }
}
defineHandlers(AbortSignal.prototype, ["abort"]);
export class AbortController {
    constructor() { this.signal = new AbortSignal(); }
    abort(reason = new DOMException("This operation was aborted.", "AbortError")) {
        if (this.signal.aborted) return;
        this.signal.aborted = true;
        this.signal.reason = reason;
        dispatchEvent(this.signal, new Event("abort"));
    }
}
