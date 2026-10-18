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

// HTML attribute names are case-insensitive; SVG ones (viewBox) are not.
function attrName(el, name) {
    name = String(name);
    for (let x = el; x && x.nodeType === 1; x = n.parent(x)) if (n.tagName(x) === "svg") return name;
    return name.toLowerCase();
}
method("getAttribute", function (name) { return n.getAttr(this, attrName(this, name)); });
method("setAttribute", function (name, value) { n.setAttr(this, attrName(this, name), String(value)); });
method("removeAttribute", function (name) { n.removeAttr(this, attrName(this, name)); });
method("hasAttribute", function (name) { return n.getAttr(this, attrName(this, name)) !== null; });
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
    const undo = type === "click" && target !== doc && target !== globalThis ? toggleFor(target) : null;
    let handled = !!undo;
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
    if (undo) {
        if (event.defaultPrevented) undo();
        else { dispatch(target, "input"); dispatch(target, "change"); }
    }
    if (type === "click" && !event.defaultPrevented) {
        const control = labelControl(target);
        if (control) { dispatch(control, "click"); handled = true; }
    }
    return handled;
}

// Click activation as in browsers: a checkbox or radio toggles before the
// handlers run (undone if one cancels the click), and a label passes the
// click on to its control.
function toggleFor(target) {
    if (target.nodeType !== 1 || n.tagName(target) !== "input") return null;
    const type = (n.getAttr(target, "type") || "").toLowerCase();
    if (type === "checkbox") {
        const was = target.checked;
        target.checked = !was;
        return () => { target.checked = was; };
    }
    if (type === "radio" && !target.checked) {
        const name = n.getAttr(target, "name");
        const group = name ? [...doc.querySelectorAll("input[type=radio]")].filter(r => n.getAttr(r, "name") === name) : [];
        const before = group.filter(r => r.checked);
        before.forEach(r => { r.checked = false; });
        target.checked = true;
        return () => { target.checked = false; before.forEach(r => { r.checked = true; }); };
    }
    return null;
}
function labelControl(target) {
    for (let x = target; x && x.nodeType === 1; x = n.parent(x)) {
        const tag = n.tagName(x);
        if (["input", "button", "select", "textarea", "a"].includes(tag)) return null;
        if (tag === "label") {
            const id = n.getAttr(x, "for");
            return id ? doc.getElementById(id) : n.query(x, "input, select, textarea, button");
        }
    }
    return null;
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
// Anki desktop's bridge: "ans" shows the answer, "ease1".."ease4" grade
// (templates with auto-submit use it). Other commands are ignored.
globalThis.pycmd = cmd => { n.command(String(cmd)); return false; };
// No text selection on a console; enough for templates that look at it.
globalThis.getSelection = () => ({
    toString() { return ""; }, rangeCount: 0, removeAllRanges() {}, addRange() {},
    getRangeAt() { return { surroundContents() {}, toString() { return ""; } }; },
});
// Anki's Image Occlusion template calls this; the app draws the masks itself
// (without it, the template's catch block shows "Error loading image occlusion").
globalThis.anki = { imageOcclusion: { setup() {}, drawShape() {} } };
// Maths is typeset by the backend before scripts run.
globalThis.MathJax = {
    typeset() {}, typesetPromise() { return Promise.resolve(); }, typesetClear() {},
    startup: { promise: Promise.resolve(), document: { state() {} } },
    config: {},
};

// --- jQuery ------------------------------------------------------------------
// Anki desktop and AnkiDroid load jQuery into every card page, and many
// templates use $ without loading it. This is a compatible subset of what
// templates use, on this DOM (full jQuery expects a real browser). A card that
// loads its own jQuery replaces it.
(function () {
    const BOOLEAN_ATTRS = ["checked", "selected", "disabled", "readonly", "multiple", "hidden", "required"];
    const PX_PROPS = /^(width|height|min-width|max-width|min-height|max-height|top|left|right|bottom|font-size|line-height|margin.*|padding.*|border.*-width)$/;
    const readyQueue = [];
    let loaded = false;

    function nodesFrom(sel, ctx) {
        if (sel == null || sel === "") return [];
        if (sel instanceof JQ) return sel.toArray();
        if (typeof sel === "string") {
            const s = sel.trim();
            if (s.startsWith("<")) return n.parse(s).filter(x => x.nodeType === 1);
            const roots = ctx ? nodesFrom(ctx).map(r => (r === document || r === window ? n.body() : r)) : [n.body()];
            const out = [];
            for (const root of roots) for (const el of query(root, s)) if (!out.includes(el)) out.push(el);
            return out;
        }
        if (sel === document || sel === window || sel.nodeType) return [sel];
        if (typeof sel.length === "number") return Array.from(sel);
        return [];
    }
    // jQuery-only selector suffixes: :visible, :hidden, :first, :last.
    function query(root, s) {
        const m = s.match(/^(.*?):(visible|hidden|first|last)$/);
        if (!m) return root === document ? [] : n.queryAll(root, s);
        let list = n.queryAll(root, m[1] || "*");
        if (m[2] === "visible") list = list.filter(visible);
        if (m[2] === "hidden") list = list.filter(e => !visible(e));
        if (m[2] === "first") list = list.slice(0, 1);
        if (m[2] === "last") list = list.slice(-1);
        return list;
    }
    function visible(el) {
        for (let x = el; x && x.nodeType === 1; x = n.parent(x))
            if (x.style.display === "none" || x.hasAttribute("hidden")) return false;
        return true;
    }
    function content(value) {
        if (value instanceof JQ) return value.toArray();
        if (typeof value === "string") return value.trim().startsWith("<") ? n.parse(value) : [document.createTextNode(value)];
        if (value && value.nodeType) return [value];
        if (Array.isArray(value)) return value.flatMap(content);
        return value == null ? [] : [document.createTextNode(String(value))];
    }

    class JQ {
        constructor(sel, ctx) {
            if (typeof sel === "function") {
                if (loaded) setTimeout(sel, 0); else readyQueue.push(sel);
                sel = document;
            }
            const list = nodesFrom(sel, ctx);
            list.forEach((x, i) => { this[i] = x; });
            this.length = list.length;
        }
        toArray() { return Array.prototype.slice.call(this, 0, this.length); }
        get(i) { return i === undefined ? this.toArray() : this[i < 0 ? this.length + i : i]; }
        each(fn) { this.toArray().forEach((el, i) => fn.call(el, i, el)); return this; }
        map(fn) { return $(this.toArray().map((el, i) => fn.call(el, i, el))); }
        eq(i) { return $(this.get(i)); }
        first() { return this.eq(0); }
        last() { return this.eq(-1); }
        index(el) { if (el === undefined) { const p = this[0] && n.parent(this[0]); return p ? n.children(p).filter(x => x.nodeType === 1).indexOf(this[0]) : -1; } return this.toArray().indexOf(el instanceof JQ ? el[0] : el); }
        find(s) { return $(this.toArray().flatMap(el => el === document || el === window ? query(n.body(), s) : el.nodeType === 1 ? query(el, s) : [])); }
        filter(s) { return $(this.toArray().filter((el, i) => typeof s === "function" ? s.call(el, i, el) : $(el).is(s))); }
        not(s) { return $(this.toArray().filter(el => !$(el).is(s))); }
        is(s) {
            if (typeof s !== "string") return this.toArray().some(el => nodesFrom(s).includes(el));
            return this.toArray().some(el => {
                if (s === ":checked") return el.hasAttribute && el.hasAttribute("checked");
                if (s === ":visible") return visible(el);
                if (s === ":hidden") return !visible(el);
                return el.nodeType === 1 && n.matches(el, s);
            });
        }
        has(s) { return this.filter(function () { return $(this).find(s).length > 0; }); }
        parent(s) { return $(this.toArray().map(el => n.parent(el)).filter(p => p && p.nodeType === 1 && (!s || n.matches(p, s)))); }
        parents(s) { const out = []; this.each(function () { for (let p = n.parent(this); p && p.nodeType === 1; p = n.parent(p)) if (!s || n.matches(p, s)) out.push(p); }); return $(out); }
        closest(s) { return $(this.toArray().map(el => el.closest ? el.closest(s) : null).filter(Boolean)); }
        children(s) { return $(this.toArray().flatMap(el => el.children || []).filter(c => !s || n.matches(c, s))); }
        siblings(s) { return $(this.toArray().flatMap(el => { const p = n.parent(el); return p ? p.children.filter(c => c !== el && (!s || n.matches(c, s))) : []; })); }
        next(s) { return $(this.toArray().map(el => el.nextElementSibling).filter(x => x && (!s || n.matches(x, s)))); }
        prev(s) { return $(this.toArray().map(el => el.previousElementSibling).filter(x => x && (!s || n.matches(x, s)))); }
        add(s) { return $(this.toArray().concat(nodesFrom(s))); }

        // Events
        on(events, selector, handler) {
            if (typeof selector === "function") { handler = selector; selector = null; }
            if (!handler) return this;
            for (const type of String(events).split(/\s+/).filter(Boolean)) {
                const name = type.split(".")[0];
                this.each(function () {
                    const el = this;
                    const wrapped = selector
                        ? function (e) { const t = e.target && e.target.closest ? e.target.closest(selector) : null; if (t && el.contains(t)) return handler.call(t, e); }
                        : function (e) { return handler.call(el, e); };
                    handler.__jq = handler.__jq || [];
                    handler.__jq.push({ el, name, wrapped });
                    el.addEventListener(name, wrapped);
                });
            }
            return this;
        }
        off(events, handler) {
            for (const type of String(events || "").split(/\s+/).filter(Boolean)) {
                const name = type.split(".")[0];
                this.each(function () {
                    if (handler && handler.__jq)
                        for (const h of handler.__jq.filter(h => h.el === this && h.name === name)) this.removeEventListener(name, h.wrapped);
                });
            }
            return this;
        }
        one(events, handler) { const self = this; const once = function (e) { self.off(events, once); return handler.call(this, e); }; return this.on(events, once); }
        trigger(type) { this.each(function () { dispatch(this, String(type).split(".")[0]); }); return this; }
        ready(fn) { if (loaded) setTimeout(fn, 0); else readyQueue.push(fn); return this; }

        // Classes, attributes, properties
        addClass(c) { return this.each(function () { if (this.classList) this.classList.add(...String(c).split(/\s+/).filter(Boolean)); }); }
        removeClass(c) { return this.each(function () { if (!this.classList) return; if (c === undefined) this.className = ""; else this.classList.remove(...String(c).split(/\s+/).filter(Boolean)); }); }
        toggleClass(c, state) { return this.each(function () { if (this.classList) for (const x of String(c).split(/\s+/).filter(Boolean)) this.classList.toggle(x, state); }); }
        hasClass(c) { return this.toArray().some(el => el.classList && el.classList.contains(c)); }
        attr(name, value) {
            if (typeof name === "object") { for (const k in name) this.attr(k, name[k]); return this; }
            const key = String(name).toLowerCase();
            if (value === undefined) {
                const el = this[0];
                if (!el || el.nodeType !== 1) return undefined;
                const v = el.getAttribute(key);
                if (BOOLEAN_ATTRS.includes(key)) return v === null ? undefined : key;
                return v === null ? undefined : v;
            }
            return this.each(function (i) {
                const v = typeof value === "function" ? value.call(this, i, this.getAttribute(key)) : value;
                if (v === null || v === false) this.removeAttribute(key);
                else this.setAttribute(key, v === true ? key : v);
            });
        }
        removeAttr(name) { return this.each(function () { if (this.removeAttribute) this.removeAttribute(name); }); }
        prop(name, value) {
            if (typeof name === "object") { for (const k in name) this.prop(k, name[k]); return this; }
            if (value === undefined) {
                const el = this[0];
                if (!el) return undefined;
                return BOOLEAN_ATTRS.includes(name) ? el.hasAttribute(name) : el[name];
            }
            return this.each(function () {
                if (BOOLEAN_ATTRS.includes(name)) this.toggleAttribute(name, !!value); else this[name] = value;
            });
        }
        removeProp(name) { return this.prop(name, false); }
        data(key, value) {
            if (value === undefined) {
                const el = this[0];
                if (!el || !el.dataset) return undefined;
                if (key === undefined) return Object.assign({}, el.__data || {});
                if (el.__data && key in el.__data) return el.__data[key];
                const v = el.dataset[key];
                try { return v === undefined ? undefined : JSON.parse(v); } catch (e) { return v; }
            }
            return this.each(function () { (this.__data = this.__data || {})[key] = value; });
        }
        val(value) {
            if (value === undefined) return this[0] ? this[0].value : undefined;
            return this.each(function () { this.value = value; });
        }

        // Content
        text(value) {
            if (value === undefined) return this.toArray().map(el => el.textContent).join("");
            return this.each(function () { this.textContent = value; });
        }
        html(value) {
            if (value === undefined) return this[0] ? this[0].innerHTML : undefined;
            return this.each(function () { this.innerHTML = value instanceof JQ ? value.toArray().map(x => x.outerHTML).join("") : value; });
        }
        append(...items) { return this.each(function (i) { for (const it of items) for (const x of content(i === 0 ? it : clone(it))) this.appendChild(x); }); }
        prepend(...items) { return this.each(function (i) { const first = this.firstChild; for (const it of items) for (const x of content(i === 0 ? it : clone(it))) this.insertBefore(x, first); }); }
        after(...items) { return this.each(function (i) { const next = this.nextSibling, p = this.parentNode; if (p) for (const it of items) for (const x of content(i === 0 ? it : clone(it))) p.insertBefore(x, next); }); }
        before(...items) { return this.each(function (i) { const p = this.parentNode; if (p) for (const it of items) for (const x of content(i === 0 ? it : clone(it))) p.insertBefore(x, this); }); }
        appendTo(target) { $(target).append(this); return this; }
        prependTo(target) { $(target).prepend(this); return this; }
        insertAfter(target) { $(target).after(this); return this; }
        insertBefore(target) { $(target).before(this); return this; }
        wrap(html) { return this.each(function () { const w = n.parse(html)[0]; if (!w) return; this.before(w); w.appendChild(this); }); }
        empty() { return this.each(function () { for (const c of n.children(this)) n.remove(this, c); }); }
        remove(s) { (s ? this.filter(s) : this).each(function () { if (this.remove) this.remove(); }); return this; }
        detach() { return this.remove(); }
        replaceWith(value) { return this.each(function (i) { const p = this.parentNode; if (!p) return; const v = typeof value === "function" ? value.call(this) : value; for (const x of content(i === 0 ? v : clone(v))) p.insertBefore(x, this); p.removeChild(this); }); }
        clone() { return $(this.toArray().map(el => el.cloneNode(true))); }

        // Style and visibility (no animation: changes apply at once)
        css(name, value) {
            if (typeof name === "object") { for (const k in name) this.css(k, name[k]); return this; }
            const key = String(name).replace(/[A-Z]/g, c => "-" + c.toLowerCase());
            if (value === undefined) return this[0] && this[0].style ? this[0].style.getPropertyValue(key) : undefined;
            return this.each(function (i) {
                let v = typeof value === "function" ? value.call(this, i, this.style.getPropertyValue(key)) : value;
                if (typeof v === "number" && PX_PROPS.test(key)) v = v + "px";
                this.style.setProperty(key, v);
            });
        }
        show() { return this.each(function () { if (this.style) { this.style.removeProperty("display"); if (!visible(this)) this.style.display = "block"; } }); }
        hide() { return this.each(function () { if (this.style) this.style.display = "none"; }); }
        toggle(state) {
            return this.each(function () {
                const show = typeof state === "boolean" ? state : !visible(this);
                show ? $(this).show() : $(this).hide();
            });
        }
        width(v) { return v === undefined ? 100 : this.css("width", v); }
        height(v) { return v === undefined ? 100 : this.css("height", v); }
        innerWidth() { return 100; }
        innerHeight() { return 100; }
        outerWidth() { return 100; }
        outerHeight() { return 100; }
        offset() { return { top: 0, left: 0 }; }
        position() { return { top: 0, left: 0 }; }
        scrollTop(v) { return v === undefined ? 0 : this; }
        scrollLeft(v) { return v === undefined ? 0 : this; }
        animate(props, duration, easing, done) {
            const cb = [duration, easing, done].find(x => typeof x === "function");
            this.css(props);
            if (cb) this.each(function () { setTimeout(() => cb.call(this), 0); });
            return this;
        }
        stop() { return this; }
        delay() { return this; }
        promise() { return Promise.resolve(this); }
        focus() { return this; }
        blur() { return this; }
    }
    for (const name of ["fadeIn", "slideDown"]) JQ.prototype[name] = function () { const cb = [...arguments].find(x => typeof x === "function"); this.show(); if (cb) this.each(function () { setTimeout(() => cb.call(this), 0); }); return this; };
    for (const name of ["fadeOut", "slideUp"]) JQ.prototype[name] = function () { const cb = [...arguments].find(x => typeof x === "function"); this.hide(); if (cb) this.each(function () { setTimeout(() => cb.call(this), 0); }); return this; };
    for (const name of ["fadeToggle", "slideToggle"]) JQ.prototype[name] = function () { return this.toggle(); };
    for (const name of ["click", "change", "dblclick", "mouseup", "mousedown", "keydown", "keyup", "input", "submit", "touchstart", "touchend"])
        JQ.prototype[name] = function (fn) { return fn ? this.on(name, fn) : this.trigger(name); };
    function clone(v) { return v instanceof JQ ? v.clone() : v && v.nodeType ? v.cloneNode(true) : v; }

    function $(sel, ctx) { return new JQ(sel, ctx); }
    $.fn = JQ.prototype;
    $.fn.jquery = "3.7.1-ankinx";
    $.each = (obj, fn) => { if (Array.isArray(obj) || (obj && typeof obj.length === "number")) Array.from(obj).forEach((v, i) => fn.call(v, i, v)); else for (const k in obj) fn.call(obj[k], k, obj[k]); return obj; };
    $.map = (arr, fn) => Array.from(arr).map((v, i) => fn(v, i)).flat().filter(v => v != null);
    $.grep = (arr, fn) => Array.from(arr).filter((v, i) => fn(v, i));
    $.inArray = (v, arr) => Array.from(arr).indexOf(v);
    $.isArray = Array.isArray;
    $.isFunction = f => typeof f === "function";
    $.isNumeric = v => !isNaN(parseFloat(v)) && isFinite(v);
    $.trim = s => String(s == null ? "" : s).trim();
    $.extend = (...objs) => { const deep = objs[0] === true; if (deep) objs.shift(); return Object.assign(...objs); };
    $.noop = () => {};
    $.now = () => Date.now();
    $.parseJSON = JSON.parse;
    $.parseHTML = s => n.parse(String(s));
    $.contains = (a, b) => a !== b && a.contains(b);
    $.ajax = $.get = $.post = $.getJSON = () => ({ done() { return this; }, fail() { return this; }, always() { return this; }, then() { return this; } });
    $.Deferred = () => { let resolve; const p = new Promise(r => { resolve = r; }); return { resolve, promise: () => p, done(fn) { p.then(fn); return this; } }; };
    $.__ready = () => { loaded = true; for (const fn of readyQueue.splice(0)) try { fn.call(document, $); } catch (e) { console.error(String(e)); } };
    if (typeof globalThis.jQuery === "undefined") globalThis.jQuery = globalThis.$ = $;
})();

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
    if (globalThis.jQuery && globalThis.jQuery.__ready) globalThis.jQuery.__ready();
    dispatch(doc, "DOMContentLoaded");
    dispatch(globalThis, "load");
    for (const hooks of [globalThis.onUpdateHook, globalThis.onShownHook])
        if (Array.isArray(hooks)) for (const fn of hooks) try { fn(); } catch (e) { console.error(String(e)); }
};
})();
