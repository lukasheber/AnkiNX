// Browser surface for card scripts, on top of the native tree in __n
// (script/card_script.cpp). Loaded once per card, before the card's own
// scripts. Covers what card templates commonly use; not a full DOM.
(function () {
"use strict";
const n = __n;
const P = n.proto; // prototype of every node object

function def(name, get, set) {
    Object.defineProperty(P, name, { get, set, configurable: true });
}
function method(name, fn) {
    Object.defineProperty(P, name, { value: fn, writable: true, configurable: true });
}
function camelToKebab(s) { return s.replace(/[A-Z]/g, c => "-" + c.toLowerCase()); }

// --- tree and content --------------------------------------------------
def("nodeType", function () { return n.nodeType(this); });
def("nodeName", function () { return this.nodeType === 1 ? this.tagName : this.nodeType === 3 ? "#text" : "#comment"; });
def("tagName", function () { return n.tagName(this).toUpperCase(); });
def("localName", function () { return n.tagName(this); });
def("parentNode", function () { return n.parent(this); });
def("parentElement", function () { const p = n.parent(this); return p && p.nodeType === 1 ? p : null; });
def("childNodes", function () { return n.children(this); });
def("children", function () { return n.children(this).filter(c => c.nodeType === 1); });
def("childElementCount", function () { return this.children.length; });
def("firstChild", function () { const c = n.children(this); return c.length ? c[0] : null; });
def("lastChild", function () { const c = n.children(this); return c.length ? c[c.length - 1] : null; });
def("firstElementChild", function () { return this.children[0] || null; });
def("lastElementChild", function () { const c = this.children; return c[c.length - 1] || null; });
function sibling(node, step, elementsOnly) {
    const p = n.parent(node);
    if (!p) return null;
    const list = n.children(p);
    for (let i = list.indexOf(node) + step; i >= 0 && i < list.length; i += step)
        if (!elementsOnly || list[i].nodeType === 1) return list[i];
    return null;
}
def("nextSibling", function () { return sibling(this, 1, false); });
def("previousSibling", function () { return sibling(this, -1, false); });
def("nextElementSibling", function () { return sibling(this, 1, true); });
def("previousElementSibling", function () { return sibling(this, -1, true); });
def("textContent", function () { return n.text(this); }, function (v) { n.setText(this, String(v == null ? "" : v)); });
def("innerText", function () { return n.text(this); }, function (v) { n.setText(this, String(v == null ? "" : v)); });
def("nodeValue", function () { return this.nodeType === 1 ? null : n.text(this); }, function (v) { if (this.nodeType !== 1) n.setText(this, String(v)); });
def("data", function () { return n.text(this); }, function (v) { n.setText(this, String(v)); });
def("innerHTML", function () { return n.html(this); }, function (v) { n.setHtml(this, String(v == null ? "" : v)); });
def("outerHTML", function () { return n.outer(this); });
def("isConnected", function () { for (let x = this; x; x = n.parent(x)) if (x === n.body()) return true; return false; });

method("getAttribute", function (name) { return n.getAttr(this, String(name).toLowerCase()); });
method("setAttribute", function (name, value) { n.setAttr(this, String(name).toLowerCase(), String(value)); });
method("removeAttribute", function (name) { n.removeAttr(this, String(name).toLowerCase()); });
method("hasAttribute", function (name) { return n.getAttr(this, String(name).toLowerCase()) !== null; });
method("toggleAttribute", function (name, force) {
    const has = this.hasAttribute(name);
    const want = force === undefined ? !has : !!force;
    if (want && !has) this.setAttribute(name, "");
    if (!want && has) this.removeAttribute(name);
    return want;
});
def("attributes", function () { return n.attrNames(this).map(name => ({ name, value: n.getAttr(this, name) })); });

method("appendChild", function (child) { n.append(this, child); return child; });
method("append", function (...items) { for (const i of items) n.append(this, typeof i === "string" ? document.createTextNode(i) : i); });
method("prepend", function (...items) { const first = this.firstChild; for (const i of items) n.insertBefore(this, typeof i === "string" ? document.createTextNode(i) : i, first); });
method("insertBefore", function (child, ref) { n.insertBefore(this, child, ref || null); return child; });
method("removeChild", function (child) { n.remove(this, child); return child; });
method("replaceChild", function (child, old) { n.insertBefore(this, child, old); n.remove(this, old); return old; });
method("remove", function () { const p = n.parent(this); if (p) n.remove(p, this); });
method("before", function (...items) { const p = n.parent(this); if (p) for (const i of items) n.insertBefore(p, typeof i === "string" ? document.createTextNode(i) : i, this); });
method("after", function (...items) { const p = n.parent(this); if (!p) return; const next = this.nextSibling; for (const i of items) n.insertBefore(p, typeof i === "string" ? document.createTextNode(i) : i, next); });
method("replaceWith", function (...items) { this.before(...items); this.remove(); });
method("cloneNode", function (deep) { return n.clone(this, !!deep); });
method("contains", function (other) { for (let x = other; x; x = n.parent(x)) if (x === this) return true; return false; });
method("hasChildNodes", function () { return n.children(this).length > 0; });
method("insertAdjacentHTML", function (where, html) {
    const nodes = n.parse(String(html));
    const p = n.parent(this);
    switch (String(where).toLowerCase()) {
        case "beforebegin": if (p) for (const x of nodes) n.insertBefore(p, x, this); break;
        case "afterend": if (p) { const next = this.nextSibling; for (const x of nodes) n.insertBefore(p, x, next); } break;
        case "afterbegin": { const first = this.firstChild; for (const x of nodes) n.insertBefore(this, x, first); break; }
        default: for (const x of nodes) n.append(this, x);
    }
});
method("insertAdjacentElement", function (where, el) {
    const p = n.parent(this);
    switch (String(where).toLowerCase()) {
        case "beforebegin": if (p) n.insertBefore(p, el, this); break;
        case "afterend": if (p) n.insertBefore(p, el, this.nextSibling); break;
        case "afterbegin": n.insertBefore(this, el, this.firstChild); break;
        default: n.append(this, el);
    }
    return el;
});

method("querySelector", function (s) { return n.query(this, String(s)); });
method("querySelectorAll", function (s) { const list = n.queryAll(this, String(s)); list.item = i => list[i] || null; return list; });
method("getElementsByClassName", function (c) { return n.queryAll(this, String(c).trim().split(/\s+/).map(x => "." + x).join("")); });
method("getElementsByTagName", function (t) { return n.queryAll(this, t === "*" ? "*" : String(t)); });
method("matches", function (s) { return n.matches(this, String(s)); });
method("closest", function (s) { for (let x = this; x && x.nodeType === 1; x = n.parent(x)) if (n.matches(x, String(s))) return x; return null; });
method("getElementById", function (id) { return n.query(this, "#" + CSS.escape(String(id))); });

// --- attributes as properties ------------------------------------------
for (const name of ["id", "title", "lang", "dir", "href", "src", "alt", "name", "type", "placeholder", "rel", "target"])
    def(name, function () { const v = n.getAttr(this, name); return v === null ? "" : v; }, function (v) { n.setAttr(this, name, String(v)); });
def("className", function () { return n.getAttr(this, "class") || ""; }, function (v) { n.setAttr(this, "class", String(v)); });
def("hidden", function () { return this.hasAttribute("hidden"); }, function (v) { this.toggleAttribute("hidden", !!v); });
def("value", function () { const v = n.getAttr(this, "value"); return v === null ? (this.tagName === "TEXTAREA" ? n.text(this) : "") : v; },
    function (v) { if (this.tagName === "TEXTAREA") n.setText(this, String(v)); else n.setAttr(this, "value", String(v)); });
def("checked", function () { return this.hasAttribute("checked"); }, function (v) { this.toggleAttribute("checked", !!v); });
def("disabled", function () { return this.hasAttribute("disabled"); }, function (v) { this.toggleAttribute("disabled", !!v); });

def("classList", function () {
    const el = this;
    const words = () => (n.getAttr(el, "class") || "").split(/\s+/).filter(Boolean);
    const store = list => n.setAttr(el, "class", list.join(" "));
    return {
        add(...c) { const w = words(); for (const x of c) if (!w.includes(x)) w.push(x); store(w); },
        remove(...c) { store(words().filter(x => !c.includes(x))); },
        toggle(c, force) {
            const has = words().includes(c);
            const want = force === undefined ? !has : !!force;
            if (want !== has) want ? this.add(c) : this.remove(c);
            return want;
        },
        contains(c) { return words().includes(c); },
        replace(a, b) { const w = words(); const i = w.indexOf(a); if (i < 0) return false; w[i] = b; store(w); return true; },
        item(i) { return words()[i] || null; },
        get length() { return words().length; },
        get value() { return words().join(" "); },
        forEach(fn) { words().forEach(fn); },
        [Symbol.iterator]() { return words()[Symbol.iterator](); },
        toString() { return words().join(" "); },
    };
});

function parseStyle(text) {
    const map = new Map();
    for (const part of (text || "").split(";")) {
        const i = part.indexOf(":");
        if (i > 0) map.set(part.slice(0, i).trim().toLowerCase(), part.slice(i + 1).trim());
    }
    return map;
}
function writeStyle(el, map) {
    const text = [...map].filter(([, v]) => v !== "").map(([k, v]) => k + ": " + v).join("; ");
    if (text) n.setAttr(el, "style", text); else n.removeAttr(el, "style");
}
def("style", function () {
    const el = this;
    const api = {
        getPropertyValue(k) { return parseStyle(n.getAttr(el, "style")).get(String(k).toLowerCase()) || ""; },
        setProperty(k, v) { const m = parseStyle(n.getAttr(el, "style")); m.set(String(k).toLowerCase(), v == null ? "" : String(v)); writeStyle(el, m); },
        removeProperty(k) { const m = parseStyle(n.getAttr(el, "style")); const old = m.get(k) || ""; m.delete(String(k).toLowerCase()); writeStyle(el, m); return old; },
    };
    return new Proxy(api, {
        get(t, k) {
            if (k in t) return t[k];
            if (k === "cssText") return n.getAttr(el, "style") || "";
            if (typeof k !== "string") return undefined;
            return t.getPropertyValue(k.startsWith("--") ? k : camelToKebab(k));
        },
        set(t, k, v) {
            if (k === "cssText") { if (v) n.setAttr(el, "style", String(v)); else n.removeAttr(el, "style"); return true; }
            t.setProperty(String(k).startsWith("--") ? k : camelToKebab(String(k)), v);
            return true;
        },
    });
});
def("dataset", function () {
    const el = this;
    const attr = k => "data-" + camelToKebab(String(k));
    return new Proxy({}, {
        get(t, k) { if (typeof k !== "string") return undefined; const v = n.getAttr(el, attr(k)); return v === null ? undefined : v; },
        set(t, k, v) { n.setAttr(el, attr(k), String(v)); return true; },
        deleteProperty(t, k) { n.removeAttr(el, attr(k)); return true; },
        has(t, k) { return n.getAttr(el, attr(k)) !== null; },
    });
});
// Layout isn't known to scripts: report a visible, sized element.
for (const name of ["offsetWidth", "offsetHeight", "clientWidth", "clientHeight", "scrollWidth", "scrollHeight"])
    def(name, function () { return 100; });
method("getBoundingClientRect", function () { return { x: 0, y: 0, top: 0, left: 0, right: 100, bottom: 100, width: 100, height: 100 }; });
method("focus", function () {});
method("blur", function () {});
method("scrollIntoView", function () {});
method("click", function () { dispatch(this, "click"); });

// --- events --------------------------------------------------------------
const listeners = new WeakMap();
function addListener(target, type, fn, options) {
    if (!fn) return;
    let byType = listeners.get(target);
    if (!byType) listeners.set(target, (byType = new Map()));
    if (!byType.has(type)) byType.set(type, []);
    const list = byType.get(type);
    if (!list.some(l => l.fn === fn)) list.push({ fn, once: !!(options && options.once) });
}
function removeListener(target, type, fn) {
    const byType = listeners.get(target);
    if (byType && byType.has(type)) byType.set(type, byType.get(type).filter(l => l.fn !== fn));
}
method("addEventListener", function (t, fn, o) { addListener(this, t, fn, o); });
method("removeEventListener", function (t, fn) { removeListener(this, t, fn); });
method("dispatchEvent", function (e) { return dispatch(this, e.type, e); });

const inlineHandlers = new WeakMap();
function inlineHandler(el, type) {
    const code = el.nodeType === 1 ? n.getAttr(el, "on" + type) : null;
    if (code === null) return null;
    let cache = inlineHandlers.get(el);
    if (!cache) inlineHandlers.set(el, (cache = {}));
    if (!cache[type] || cache[type].code !== code)
        cache[type] = { code, fn: new Function("event", code) };
    return cache[type].fn;
}

// Bubble `type` from `target` to the document and window. Returns whether
// any handler ran (a tap that runs one doesn't also flip the card).
function dispatch(target, type, init) {
    const event = init || {};
    event.type = type;
    event.target = target;
    event.bubbles = true;
    event.defaultPrevented = false;
    let stopped = false;
    event.preventDefault = () => { event.defaultPrevented = true; };
    event.stopPropagation = () => { stopped = true; };
    event.stopImmediatePropagation = () => { stopped = true; };
    let handled = false;
    // Element ancestors, then document and window.
    const path = [];
    if (target === doc || target === globalThis) path.push(target);
    else for (let x = target; x; x = n.parent(x)) path.push(x);
    if (target !== globalThis) {
        if (target !== doc) path.push(doc);
        path.push(globalThis);
    }
    for (const node of path) {
        if (stopped) break;
        event.currentTarget = node;
        const inline = inlineHandler(node, type);
        if (inline) { handled = true; if (inline.call(node, event) === false) event.preventDefault(); }
        const prop = node["on" + type];
        if (typeof prop === "function" && prop !== inline) { handled = true; prop.call(node, event); }
        const byType = listeners.get(node);
        if (byType && byType.has(type)) {
            for (const l of [...byType.get(type)]) {
                handled = true;
                l.fn.call(node, event);
                if (l.once) removeListener(node, type, l.fn);
            }
        }
    }
    return handled;
}

// --- document and window -------------------------------------------------
const doc = {
    get body() { return n.body(); },
    get documentElement() { return n.body(); },
    get head() { return n.body(); },
    readyState: "complete",
    getElementById(id) { return n.byId(String(id)); },
    querySelector(s) { return n.query(n.body(), String(s)); },
    querySelectorAll(s) { const l = n.queryAll(n.body(), String(s)); l.item = i => l[i] || null; return l; },
    getElementsByClassName(c) { return n.body().getElementsByClassName(c); },
    getElementsByTagName(t) { return n.body().getElementsByTagName(t); },
    getElementsByName(name) { return n.queryAll(n.body(), "[name=\"" + name + "\"]"); },
    createElement(t) { return n.createElement(String(t)); },
    createElementNS(ns, t) { return n.createElement(String(t)); },
    createTextNode(t) { return n.createText(String(t)); },
    createDocumentFragment() { return n.createFragment(); },
    createComment() { return n.createText(""); },
    addEventListener(t, fn, o) { addListener(doc, t, fn, o); },
    removeEventListener(t, fn) { removeListener(doc, t, fn); },
    hasFocus() { return true; },
    activeElement: null,
    cookie: "",
};
globalThis.document = doc;
globalThis.window = globalThis;
globalThis.self = globalThis;
globalThis.addEventListener = (t, fn, o) => addListener(globalThis, t, fn, o);
globalThis.removeEventListener = (t, fn) => removeListener(globalThis, t, fn);
globalThis.navigator = { userAgent: "AnkiNX (Nintendo Switch)", platform: "Nintendo Switch", language: "en", languages: ["en"] };
globalThis.location = { href: "", hash: "", search: "", pathname: "", reload() {} };
globalThis.innerWidth = 1280;
globalThis.innerHeight = 720;
globalThis.devicePixelRatio = 1;
globalThis.getComputedStyle = el => el.style;
globalThis.matchMedia = q => ({ matches: false, media: q, addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} });
globalThis.alert = msg => n.log(1, "alert: " + msg);
globalThis.confirm = () => false;
globalThis.prompt = () => null;
globalThis.scrollTo = () => {};
globalThis.CSS = { escape: s => String(s).replace(/([^\w-])/g, "\\$1"), supports: () => false };
globalThis.MutationObserver = class { observe() {} disconnect() {} takeRecords() { return []; } };
globalThis.ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };
globalThis.IntersectionObserver = class { observe() {} unobserve() {} disconnect() {} };
globalThis.Event = class { constructor(type, init) { this.type = type; Object.assign(this, init || {}); } };
globalThis.CustomEvent = class extends globalThis.Event {};

const console = {};
for (const [name, level] of [["log", 0], ["info", 0], ["debug", 0], ["warn", 1], ["error", 2]])
    console[name] = (...args) => n.log(level, args.map(a => (typeof a === "string" ? a : JSON.stringify(a))).join(" "));
globalThis.console = console;

// Timers: the app fires them (CardScript::fireTimer).
const timers = new Map();
let timerSeq = 0;
function addTimer(fn, ms, args, repeat) {
    const id = ++timerSeq;
    timers.set(id, { fn, args, ms: Math.max(0, ms | 0), repeat });
    n.timer(id, Math.max(0, ms | 0));
    return id;
}
globalThis.setTimeout = (fn, ms, ...args) => addTimer(fn, ms, args, false);
globalThis.setInterval = (fn, ms, ...args) => addTimer(fn, Math.max(10, ms | 0), args, true);
globalThis.clearTimeout = globalThis.clearInterval = id => { timers.delete(id); };
globalThis.requestAnimationFrame = fn => addTimer(() => fn(Date.now()), 16, [], false);
globalThis.cancelAnimationFrame = id => { timers.delete(id); };
globalThis.queueMicrotask = fn => Promise.resolve().then(fn);

function storage(kind) {
    return new Proxy({
        getItem(k) { return n.storage(kind, "get", String(k)); },
        setItem(k, v) { n.storage(kind, "set", String(k), String(v)); },
        removeItem(k) { n.storage(kind, "remove", String(k)); },
        clear() { n.storage(kind, "clear", ""); },
        key(i) { return n.storage(kind, "key", String(i)); },
        get length() { return Number(n.storage(kind, "length", "")); },
    }, {
        get(t, k) { if (k in t) return t[k]; return typeof k === "string" ? t.getItem(k) : undefined; },
        set(t, k, v) { t.setItem(k, v); return true; },
    });
}
globalThis.localStorage = storage("local");
globalThis.sessionStorage = storage("session");

// --- Anki's reviewer environment ------------------------------------------
// Hooks templates register to run when the card is shown.
globalThis.onUpdateHook = [];
globalThis.onShownHook = [];
globalThis.pycmd = () => {};
// Anki's Image Occlusion template calls this; the app draws the masks itself
// (without it, the template's catch block shows "Error loading image occlusion").
globalThis.anki = { imageOcclusion: { setup() {}, drawShape() {} } };
// Maths is typeset by the backend before scripts run.
globalThis.MathJax = {
    typeset() {}, typesetPromise() { return Promise.resolve(); }, typesetClear() {},
    startup: { promise: Promise.resolve(), document: { state() {} } },
    config: {},
};

// Native entry points.
n.dispatch = (target, type) => dispatch(target, type);
n.fireTimer = id => {
    const t = timers.get(id);
    if (!t) return;
    if (!t.repeat) timers.delete(id);
    else n.timer(id, t.ms);
    t.fn(...t.args);
};
n.shown = () => {
    dispatch(doc, "DOMContentLoaded");
    dispatch(globalThis, "load");
    for (const hooks of [globalThis.onUpdateHook, globalThis.onShownHook])
        if (Array.isArray(hooks)) for (const fn of hooks) try { fn(); } catch (e) { console.error(String(e)); }
};
})();
