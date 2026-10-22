// The rest of the global object a card page has in a browser: navigator,
// location and history, storage, console, matchMedia, selection, observers,
// media elements, and the event, URL and timer classes.
import * as linkedom from "linkedom";
import { native } from "./native.js";
import { define, document } from "./dom.js";
import * as events from "./events.js";
import { dispatchEvent, Event, HashChangeEvent } from "./events.js";
import { reportError, describe } from "./errors.js";
import { mediaPath, PAGE_URL } from "./media.js";
import * as timers from "./timers.js";
import { URL, URLSearchParams } from "./url.js";

define("window", globalThis);
define("self", globalThis);
define("top", globalThis);
define("parent", globalThis);
define("frames", globalThis);
for (const [name, value] of Object.entries(events))
    if (/^[A-Z]/.test(name) && typeof value === "function") define(name, value);
for (const name of ["setTimeout", "setInterval", "clearTimeout", "clearInterval", "requestAnimationFrame",
    "cancelAnimationFrame", "requestIdleCallback", "cancelIdleCallback"])
    define(name, timers[name]);
define("URL", URL);
define("webkitURL", URL);
define("URLSearchParams", URLSearchParams);
define("reportError", reportError);
Object.assign(globalThis, {
    name: "", status: "", closed: false, length: 0, opener: null, frameElement: null,
    origin: "http://127.0.0.1", isSecureContext: true, crossOriginIsolated: false,
});

// ---------------------------------------------------------------- console --

function format(args) {
    if (typeof args[0] === "string" && /%[sdifoOc]/.test(args[0])) {
        let i = 1;
        const first = args[0].replace(/%([sdifoOc%])/g, (m, f) => {
            if (f === "%") return "%";
            if (i >= args.length) return m;
            const v = args[i++];
            if (f === "c") return "";
            if (f === "d" || f === "i") return String(parseInt(v, 10));
            if (f === "f") return String(parseFloat(v));
            return typeof v === "string" ? v : inspect(v);
        });
        args = [first, ...args.slice(i)];
    }
    return args.map(a => (typeof a === "string" ? a : inspect(a))).join(" ");
}
// A short description, as a console shows it: elements by tag, id and
// class (never their whole tree), two levels of objects, long output cut.
function inspect(value, depth = 0, seen = new Set()) {
    if (value === null || value === undefined) return String(value);
    switch (typeof value) {
        case "string": return depth ? JSON.stringify(value.length > 200 ? value.slice(0, 200) + "…" : value) : value;
        case "function": return `[Function ${value.name || "(anonymous)"}]`;
        case "object": break;
        default: return String(value);
    }
    if (value instanceof Error) return describe(value);
    if (value.nodeType === 1) {
        const classes = String(value.getAttribute("class") || "").trim();
        return `<${value.localName}${value.id ? "#" + value.id : ""}${classes ? "." + classes.split(/\s+/).join(".") : ""}>`;
    }
    if (value.nodeType === 3) return `#text ${JSON.stringify(value.data.slice(0, 80))}`;
    if (value.nodeType) return `[${value.nodeName}]`;
    if (seen.has(value)) return "[Circular]";
    if (depth >= 2) return Array.isArray(value) ? "[Array]" : "[Object]";
    seen.add(value);
    let out;
    const list = Array.isArray(value) || (typeof value.length === "number" && value.jquery);
    if (list) {
        const items = Array.prototype.slice.call(value, 0, 20).map(v => inspect(v, depth + 1, seen));
        if (value.length > 20) items.push(`… ${value.length - 20} more`);
        out = (value.jquery ? `jQuery(${value.length}) ` : "") + `[${items.join(", ")}]`;
    } else {
        const keys = Object.keys(value);
        const items = keys.slice(0, 20).map(k => `${k}: ${inspect(value[k], depth + 1, seen)}`);
        if (keys.length > 20) items.push("…");
        out = `{${items.join(", ")}}`;
    }
    seen.delete(value);
    return out.length > 1000 ? out.slice(0, 1000) + "…" : out;
}
const counts = new Map();
const times = new Map();
const consoleObject = { group() {}, groupCollapsed() {}, groupEnd() {}, profile() {}, profileEnd() {}, timeStamp() {}, clear() {} };
for (const [name, level] of [["log", 0], ["info", 0], ["debug", 0], ["trace", 0], ["dir", 0], ["dirxml", 0], ["table", 0], ["warn", 1], ["error", 2]])
    consoleObject[name] = (...args) => native.log(level, format(args));
consoleObject.assert = (ok, ...args) => { if (!ok) native.log(2, "Assertion failed: " + format(args)); };
consoleObject.count = (label = "default") => { counts.set(label, (counts.get(label) || 0) + 1); native.log(0, `${label}: ${counts.get(label)}`); };
consoleObject.countReset = (label = "default") => counts.delete(label);
consoleObject.time = (label = "default") => times.set(label, performance.now());
consoleObject.timeLog = consoleObject.timeEnd = (label = "default") => {
    if (times.has(label)) native.log(0, `${label}: ${(performance.now() - times.get(label)).toFixed(1)} ms`);
};
define("console", consoleObject);

// ------------------------------------------------------------- navigator --

const navigatorObject = {
    userAgent: "Mozilla/5.0 (Nintendo Switch; AnkiNX) AppleWebKit/537.36 (KHTML, like Gecko)",
    appName: "Netscape", appVersion: "5.0 (Nintendo Switch; AnkiNX)", appCodeName: "Mozilla", product: "Gecko",
    productSub: "20030107", vendor: "", vendorSub: "", platform: "Nintendo Switch", language: "en-US",
    languages: ["en-US", "en"], onLine: false, cookieEnabled: true, doNotTrack: null, maxTouchPoints: 10,
    hardwareConcurrency: 4, deviceMemory: 4, pdfViewerEnabled: false, webdriver: false,
    plugins: [], mimeTypes: [],
    clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve(""), write: () => Promise.resolve(), read: () => Promise.resolve([]) },
    vibrate: () => false,
    sendBeacon: () => false,
    javaEnabled: () => false,
    share: () => Promise.reject(new DOMException("Sharing is not available.", "NotAllowedError")),
    canShare: () => false,
    getGamepads: () => [],
    storage: { estimate: () => Promise.resolve({ quota: 0, usage: 0 }), persist: () => Promise.resolve(false), persisted: () => Promise.resolve(false) },
    permissions: { query: () => Promise.resolve({ state: "denied", onchange: null }) },
};
define("navigator", navigatorObject);

// --------------------------------------------------------------- location --

class Location {
    #url = new URL(PAGE_URL);
    get href() { return this.#url.href; }
    set href(value) { this.assign(value); }
    get origin() { return this.#url.origin; }
    get protocol() { return this.#url.protocol; }
    get host() { return this.#url.host; }
    get hostname() { return this.#url.hostname; }
    get port() { return this.#url.port; }
    get pathname() { return this.#url.pathname; }
    get search() { return this.#url.search; }
    get hash() { return this.#url.hash; }
    set hash(value) {
        const next = new URL(this.#url.href);
        next.hash = String(value);
        this.assign(next.href);
    }
    get ancestorOrigins() { return []; }
    // Same page with another fragment: scroll there and fire "hashchange".
    // Other pages: cards can't navigate.
    assign(value) {
        let next;
        try {
            next = new URL(String(value), this.#url.href);
        } catch (e) {
            throw new DOMException(`'${value}' is not a valid URL.`, "SyntaxError");
        }
        const same = u => u.href.replace(/#.*$/, "");
        if (same(next) !== same(this.#url)) {
            native.log(1, "navigation is not possible in cards: " + next.href);
            return;
        }
        const oldURL = this.#url.href;
        if (next.href === oldURL) return;
        this.#url = next;
        const id = decodeURIComponent(next.hash.slice(1));
        const target = id && document.getElementById(id);
        if (target) target.scrollIntoView();
        timers.queueTask(() => dispatchEvent(globalThis, new HashChangeEvent("hashchange", { oldURL, newURL: next.href })));
    }
    replace(value) { this.assign(value); }
    reload() {}
    toString() { return this.href; }
}
define("location", new Location());

const historyState = { state: null };
define("history", {
    get length() { return 1; },
    get state() { return historyState.state; },
    scrollRestoration: "auto",
    pushState(state) { historyState.state = state; },
    replaceState(state) { historyState.state = state; },
    back() {}, forward() {}, go() {},
});

// ---------------------------------------------------------------- storage --

class Storage {
    #kind;
    constructor(kind) { this.#kind = kind; }
    get length() { return Number(native.storage(this.#kind, "length", "")) || 0; }
    key(index) { return native.storage(this.#kind, "key", String(index)); }
    getItem(key) { return native.storage(this.#kind, "get", String(key)); }
    setItem(key, value) { native.storage(this.#kind, "set", String(key), String(value)); }
    removeItem(key) { native.storage(this.#kind, "remove", String(key)); }
    clear() { native.storage(this.#kind, "clear", ""); }
}
// storage.foo and storage.foo = "x" go to the items, as in browsers.
function storageProxy(kind) {
    const target = new Storage(kind);
    return new Proxy(target, {
        get(t, key) {
            if (typeof key !== "string" || key in t) {
                const v = Reflect.get(t, key, t);
                return typeof v === "function" ? v.bind(t) : v;
            }
            return t.getItem(key) ?? undefined;
        },
        set(t, key, value) {
            if (typeof key !== "string" || key in t) return Reflect.set(t, key, value, t);
            t.setItem(key, value);
            return true;
        },
        has(t, key) { return key in t || (typeof key === "string" && t.getItem(key) !== null); },
        deleteProperty(t, key) { if (typeof key === "string") t.removeItem(key); return true; },
        ownKeys(t) {
            const keys = [];
            for (let i = 0; i < t.length; i++) keys.push(t.key(i));
            return keys;
        },
        getOwnPropertyDescriptor(t, key) {
            const v = typeof key === "string" ? t.getItem(key) : null;
            return v === null ? undefined : { value: v, writable: true, enumerable: true, configurable: true };
        },
    });
}
define("Storage", Storage);
define("localStorage", storageProxy("local"));
define("sessionStorage", storageProxy("session"));

// ------------------------------------------------------------ matchMedia --

function viewport() {
    const v = native.viewport();
    return { width: v[0], height: v[1] };
}
function matchesQuery(query) {
    const night = () => document.body.classList.contains("nightMode") || document.documentElement.classList.contains("night-mode");
    return String(query).split(",").some(part => {
        part = part.trim().toLowerCase();
        let negate = false;
        if (part.startsWith("not ")) {
            negate = true;
            part = part.slice(4);
        }
        const ok = part.split(/\s+and\s+/).every(term => {
            term = term.replace(/^only\s+/, "").trim();
            if (term === "all" || term === "screen" || term === "") return true;
            if (term === "print" || term === "speech") return false;
            const m = term.match(/^\(\s*([a-z-]+)\s*(?::\s*([^)]+?))?\s*\)$/);
            if (!m) return false;
            const [, feature, raw] = m;
            const px = v => (/em$/.test(v) ? parseFloat(v) * 16 : parseFloat(v));
            const { width, height } = viewport();
            switch (feature) {
                case "min-width": return width >= px(raw);
                case "max-width": return width <= px(raw);
                case "width": return width === px(raw);
                case "min-height": return height >= px(raw);
                case "max-height": return height <= px(raw);
                case "height": return height === px(raw);
                case "orientation": return raw === (width >= height ? "landscape" : "portrait");
                case "prefers-color-scheme": return raw === (night() ? "dark" : "light");
                case "prefers-reduced-motion": return raw === "reduce";
                case "hover":
                case "any-hover": return raw === undefined ? false : raw === "none";
                case "pointer":
                case "any-pointer": return raw === undefined ? true : raw === "coarse";
                case "color": return true;
                case "min-resolution":
                case "resolution": return true;
                default: return false;
            }
        });
        return negate ? !ok : ok;
    });
}
class MediaQueryList extends linkedom.EventTarget {
    constructor(media) {
        super();
        this.media = String(media);
        this.onchange = null;
    }
    get matches() { return matchesQuery(this.media); }
    addListener(fn) { this.addEventListener("change", fn); }
    removeListener(fn) { this.removeEventListener("change", fn); }
}
define("MediaQueryList", MediaQueryList);
define("matchMedia", query => new MediaQueryList(query));

// ----------------------------------------------------------------- dialogs --

// AnkiDroid shows these as dialogs; here alert() is a notice from the
// reviewer, and nothing can wait for an answer.
define("alert", (message = "") => native.command("alert:" + String(message)));
define("confirm", () => { native.log(1, "confirm() is not available in cards"); return false; });
define("prompt", () => { native.log(1, "prompt() is not available in cards"); return null; });
define("print", () => {});
define("open", () => null);
define("close", () => {});
define("stop", () => {});
define("focus", () => {});
define("blur", () => {});
define("moveTo", () => {});
define("resizeTo", () => {});
define("postMessage", (data, origin) => {
    timers.queueTask(() => dispatchEvent(globalThis, new events.MessageEvent("message", { data, origin: globalThis.origin, source: globalThis })));
});

// -------------------------------------------------------------- selection --

// No text can be selected on the console: the selection is at most a caret,
// put where the card was last tapped (as a tap in a browser does).
class Selection {
    #ranges = [];
    #node = null;
    get rangeCount() { return this.#ranges.length; }
    get isCollapsed() { return true; }
    get type() { return this.#ranges.length ? "Caret" : "None"; }
    get anchorNode() { return this.#node; }
    get focusNode() { return this.#node; }
    get anchorOffset() { return 0; }
    get focusOffset() { return 0; }
    toString() { return ""; }
    getRangeAt(index) {
        if (index !== 0 || !this.#ranges.length) throw new DOMException("The index is not in the allowed range.", "IndexSizeError");
        return this.#ranges[0];
    }
    addRange(range) { this.#ranges = [range]; }
    removeAllRanges() {
        this.#ranges = [];
        this.#node = null;
    }
    removeRange() { this.removeAllRanges(); }
    empty() { this.removeAllRanges(); }
    collapse(node) {
        if (!node) return this.removeAllRanges();
        this.#node = node;
        this.#ranges = [document.createRange()];
    }
    setPosition(node) { this.collapse(node); }
    collapseToStart() {}
    collapseToEnd() {}
    extend() {}
    selectAllChildren(node) { this.collapse(node); }
    setBaseAndExtent(node) { this.collapse(node); }
    containsNode() { return false; }
    modify() {}
}
const selection = new Selection();
define("Selection", Selection);
define("getSelection", () => selection);
document.getSelection = () => selection;

// --------------------------------------------------------------- utilities --

define("CSS", {
    escape(value) {
        return String(value).replace(/[\0-\x1f\x7f]|^-?\d|^-$|[^\w -￿-]/g, (c, i) => {
            if (c === "\0") return "�";
            if (/[\0-\x1f\x7f]/.test(c) || (/\d/.test(c[c.length - 1]) && (i === 0 || c.length > 1)))
                return (c.length > 1 ? c.slice(0, -1) : "") + "\\" + c.charCodeAt(c.length - 1).toString(16) + " ";
            if (c === "-") return "\\-";
            return "\\" + c;
        });
    },
    supports: () => false,
    registerProperty() {},
});

if (typeof globalThis.structuredClone !== "function")
    define("structuredClone", function structuredClone(value) {
        const seen = new Map();
        const clone = v => {
            if (v === null || typeof v !== "object") {
                if (typeof v === "function" || typeof v === "symbol") throw new DOMException("could not be cloned.", "DataCloneError");
                return v;
            }
            if (seen.has(v)) return seen.get(v);
            let out;
            if (v instanceof Date) out = new Date(v.getTime());
            else if (v instanceof RegExp) out = new RegExp(v.source, v.flags);
            else if (v instanceof Map) {
                out = new Map();
                seen.set(v, out);
                for (const [k, x] of v) out.set(clone(k), clone(x));
                return out;
            } else if (v instanceof Set) {
                out = new Set();
                seen.set(v, out);
                for (const x of v) out.add(clone(x));
                return out;
            } else if (ArrayBuffer.isView(v)) out = v.slice();
            else if (v instanceof ArrayBuffer) out = v.slice(0);
            else if (v.nodeType) throw new DOMException("could not be cloned.", "DataCloneError");
            else {
                out = Array.isArray(v) ? [] : {};
                seen.set(v, out);
                for (const k of Object.keys(v)) out[k] = clone(v[k]);
                return out;
            }
            seen.set(v, out);
            return out;
        };
        return clone(value);
    });

if (typeof globalThis.TextEncoder !== "function") {
    define("TextEncoder", class TextEncoder {
        get encoding() { return "utf-8"; }
        encode(text = "") {
            const s = unescape(encodeURIComponent(String(text)));
            const out = new Uint8Array(s.length);
            for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
            return out;
        }
    });
    define("TextDecoder", class TextDecoder {
        constructor(label = "utf-8") { this.encoding = String(label).toLowerCase(); }
        decode(bytes) {
            if (!bytes) return "";
            const view = bytes instanceof ArrayBuffer ? new Uint8Array(bytes) : new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
            let s = "";
            for (let i = 0; i < view.length; i += 8192) s += String.fromCharCode(...view.subarray(i, i + 8192));
            if (this.encoding === "latin1" || this.encoding === "iso-8859-1") return s;
            try {
                return decodeURIComponent(escape(s));
            } catch (e) {
                return s;
            }
        }
    });
}

define("crypto", {
    getRandomValues(array) {
        const bytes = new Uint8Array(array.buffer, array.byteOffset, array.byteLength);
        for (let i = 0; i < bytes.length; i++) bytes[i] = Math.floor(Math.random() * 256);
        return array;
    },
    randomUUID() {
        const b = this.getRandomValues(new Uint8Array(16));
        b[6] = (b[6] & 0x0f) | 0x40;
        b[8] = (b[8] & 0x3f) | 0x80;
        const h = [...b].map(x => x.toString(16).padStart(2, "0")).join("");
        return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`;
    },
    subtle: {},
});

define("XMLSerializer", class XMLSerializer {
    serializeToString(node) { return node.nodeType === 9 ? node.documentElement.outerHTML : node.outerHTML ?? node.textContent; }
});

// Observers. Intersection: everything is reported visible once (cards are
// small, and lazy loading should load). Resize: reported once, from layout.
define("IntersectionObserver", class IntersectionObserver {
    constructor(callback, options = {}) {
        this.callback = callback;
        this.root = options.root || null;
        this.rootMargin = options.rootMargin || "0px";
        this.thresholds = [].concat(options.threshold ?? 0);
        this.targets = new Set();
    }
    observe(target) {
        this.targets.add(target);
        timers.queueTask(() => {
            if (!this.targets.has(target)) return;
            const rect = target.getBoundingClientRect();
            this.callback([{ target, isIntersecting: true, intersectionRatio: 1, boundingClientRect: rect,
                intersectionRect: rect, rootBounds: null, time: performance.now() }], this);
        });
    }
    unobserve(target) { this.targets.delete(target); }
    disconnect() { this.targets.clear(); }
    takeRecords() { return []; }
});
define("ResizeObserver", class ResizeObserver {
    constructor(callback) {
        this.callback = callback;
        this.targets = new Set();
    }
    observe(target) {
        this.targets.add(target);
        timers.queueTask(() => {
            if (!this.targets.has(target)) return;
            const r = target.getBoundingClientRect();
            const size = [{ inlineSize: r.width, blockSize: r.height }];
            this.callback([{ target, contentRect: r, borderBoxSize: size, contentBoxSize: size, devicePixelContentBoxSize: size }], this);
        });
    }
    unobserve(target) { this.targets.delete(target); }
    disconnect() { this.targets.clear(); }
});
define("PerformanceObserver", class PerformanceObserver {
    observe() {}
    disconnect() {}
    static get supportedEntryTypes() { return []; }
});

// Speech: no voice on the console. Utterances end with an error at once.
define("SpeechSynthesisUtterance", class SpeechSynthesisUtterance extends linkedom.EventTarget {
    constructor(text = "") {
        super();
        Object.assign(this, { text: String(text), lang: "", voice: null, volume: 1, rate: 1, pitch: 1 });
    }
});
define("speechSynthesis", {
    speaking: false, pending: false, paused: false, onvoiceschanged: null,
    getVoices: () => [],
    speak(utterance) {
        timers.queueTask(() => dispatchEvent(utterance, new events.Event("error")));
    },
    cancel() {}, pause() {}, resume() {},
    addEventListener() {}, removeEventListener() {},
});

// ----------------------------------------------------------- audio, video --

// play() hands the file to the reviewer, which plays card audio.
const MP = linkedom.HTMLMediaElement ? linkedom.HTMLMediaElement.prototype : null;
if (MP) {
    const playing = new WeakMap();
    const fire = (el, type) => timers.queueTask(() => dispatchEvent(el, new Event(type)));
    const source = el => el.getAttribute("src") ?? (el.querySelector("source[src]") || { getAttribute: () => null }).getAttribute("src");
    Object.defineProperties(MP, {
        src: { get() { const v = this.getAttribute("src"); return v === null ? "" : new URL(v, PAGE_URL).href; }, set(v) { this.setAttribute("src", String(v)); }, configurable: true },
        currentSrc: { get() { const v = source(this); return v === null ? "" : new URL(v, PAGE_URL).href; }, configurable: true },
        paused: { get() { return !playing.get(this); }, configurable: true },
        ended: { get() { return false; }, configurable: true },
        readyState: { get() { return 4; }, configurable: true },
        networkState: { get() { return 1; }, configurable: true },
        duration: { get() { return NaN; }, configurable: true },
        currentTime: { get() { return 0; }, set() {}, configurable: true },
        volume: { value: 1, writable: true, configurable: true },
        muted: { value: false, writable: true, configurable: true },
        playbackRate: { value: 1, writable: true, configurable: true },
        loop: { get() { return this.hasAttribute("loop"); }, set(v) { this.toggleAttribute("loop", !!v); }, configurable: true },
        autoplay: { get() { return this.hasAttribute("autoplay"); }, set(v) { this.toggleAttribute("autoplay", !!v); }, configurable: true },
        controls: { get() { return this.hasAttribute("controls"); }, set(v) { this.toggleAttribute("controls", !!v); }, configurable: true },
    });
    MP.play = function () {
        const path = mediaPath(source(this) || "");
        if (!path) {
            fire(this, "error");
            return Promise.reject(new DOMException("The element has no supported sources.", "NotSupportedError"));
        }
        playing.set(this, true);
        native.command("play:" + path);
        fire(this, "play");
        fire(this, "playing");
        return Promise.resolve();
    };
    MP.pause = function () {
        if (!playing.get(this)) return;
        playing.set(this, false);
        native.command("stop");
        fire(this, "pause");
    };
    MP.load = function () { fire(this, "loadeddata"); };
    MP.canPlayType = type => (/^audio\/(mpeg|mp3|ogg|wav|x-wav|mp4|aac|flac|webm)/.test(type) ? "probably" : "");
    MP.fastSeek = function () {};
    define("Audio", function Audio(src) {
        const audio = document.createElement("audio");
        audio.setAttribute("preload", "auto");
        if (src !== undefined) audio.setAttribute("src", String(src));
        return audio;
    });
    globalThis.Audio.prototype = (linkedom.HTMLAudioElement || linkedom.HTMLMediaElement).prototype;
}
