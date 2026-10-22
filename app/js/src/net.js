// fetch() and XMLHttpRequest for the media folder (cards are offline: other
// URLs fail as a network error), with Headers, Request, Response, Blob,
// FileReader and FormData. jQuery's $.ajax and $.getJSON go through
// XMLHttpRequest.
import * as linkedom from "linkedom";
import { native } from "./native.js";
import { define } from "./dom.js";
import { dispatchEvent, Event, ProgressEvent } from "./events.js";
import { reportError } from "./errors.js";
import { mediaPath, PAGE_URL } from "./media.js";
import { queueTask } from "./timers.js";
import { URL, URLSearchParams } from "./url.js";

const TYPES = {
    json: "application/json", js: "text/javascript", mjs: "text/javascript", css: "text/css", html: "text/html",
    htm: "text/html", txt: "text/plain", csv: "text/csv", xml: "application/xml", svg: "image/svg+xml",
    png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", gif: "image/gif", webp: "image/webp",
    mp3: "audio/mpeg", ogg: "audio/ogg", wav: "audio/wav", m4a: "audio/mp4", ttf: "font/ttf", otf: "font/otf",
    woff: "font/woff", woff2: "font/woff2",
};
const contentType = path => TYPES[(path.match(/\.([a-z0-9]+)$/i) || [])[1]?.toLowerCase()] || "application/octet-stream";

const decoder = () => new TextDecoder();
const toBytes = data => {
    if (data == null) return new Uint8Array(0);
    if (data instanceof ArrayBuffer) return new Uint8Array(data);
    if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    if (data instanceof Blob) return data.__bytes;
    return new TextEncoder().encode(String(data));
};

// ------------------------------------------------------------------ blobs --

let blobIds = 0;
export class Blob {
    constructor(parts = [], options = {}) {
        const chunks = [...parts].map(toBytes);
        const bytes = new Uint8Array(chunks.reduce((n, c) => n + c.length, 0));
        let at = 0;
        for (const c of chunks) {
            bytes.set(c, at);
            at += c.length;
        }
        this.__bytes = bytes;
        this.__id = ++blobIds;
        this.type = String(options.type || "").toLowerCase();
    }
    get size() { return this.__bytes.length; }
    text() { return Promise.resolve(decoder().decode(this.__bytes)); }
    arrayBuffer() { return Promise.resolve(this.__bytes.slice().buffer); }
    bytes() { return Promise.resolve(this.__bytes.slice()); }
    slice(start = 0, end = this.size, type = "") { return new Blob([this.__bytes.slice(start, end)], { type }); }
}
export class File extends Blob {
    constructor(parts, name, options = {}) {
        super(parts, options);
        this.name = String(name);
        this.lastModified = options.lastModified ?? Date.now();
    }
}
class FileReader extends linkedom.EventTarget {
    constructor() {
        super();
        this.readyState = 0;
        this.result = null;
        this.error = null;
    }
    #read(blob, convert) {
        this.readyState = 1;
        queueTask(() => {
            this.result = convert(blob.__bytes, blob.type);
            this.readyState = 2;
            for (const type of ["load", "loadend"]) {
                const handler = this["on" + type];
                const event = new ProgressEvent(type, { lengthComputable: true, loaded: blob.size, total: blob.size });
                if (typeof handler === "function") handler.call(this, event);
                dispatchEvent(this, event);
            }
        });
    }
    readAsText(blob) { this.#read(blob, b => decoder().decode(b)); }
    readAsArrayBuffer(blob) { this.#read(blob, b => b.slice().buffer); }
    readAsBinaryString(blob) { this.#read(blob, b => String.fromCharCode(...b)); }
    readAsDataURL(blob) {
        this.#read(blob, (b, type) => {
            let s = "";
            for (let i = 0; i < b.length; i += 8192) s += String.fromCharCode(...b.subarray(i, i + 8192));
            return `data:${type || "application/octet-stream"};base64,${btoa(s)}`;
        });
    }
    abort() {}
}

class FormData {
    #entries = [];
    constructor(form) {
        if (form && form.elements)
            for (const el of form.elements) {
                const name = el.getAttribute("name");
                if (!name || el.disabled) continue;
                if ((el.type === "checkbox" || el.type === "radio") && !el.checked) continue;
                if (el.localName === "select") for (const o of el.selectedOptions) this.#entries.push([name, o.value]);
                else if (!["submit", "button", "reset", "image", "file"].includes(el.type)) this.#entries.push([name, el.value]);
            }
    }
    append(name, value) { this.#entries.push([String(name), value instanceof Blob ? value : String(value)]); }
    delete(name) { this.#entries = this.#entries.filter(([n]) => n !== String(name)); }
    get(name) { const e = this.#entries.find(([n]) => n === String(name)); return e ? e[1] : null; }
    getAll(name) { return this.#entries.filter(([n]) => n === String(name)).map(([, v]) => v); }
    has(name) { return this.#entries.some(([n]) => n === String(name)); }
    set(name, value) { this.delete(name); this.append(name, value); }
    forEach(fn, thisArg) { for (const [n, v] of this.#entries) fn.call(thisArg, v, n, this); }
    keys() { return this.#entries.map(([n]) => n)[Symbol.iterator](); }
    values() { return this.#entries.map(([, v]) => v)[Symbol.iterator](); }
    entries() { return this.#entries.map(e => e.slice())[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
}

// ---------------------------------------------------------- fetch classes --

class Headers {
    #map = new Map();
    constructor(init) {
        if (init instanceof Headers) init.forEach((v, k) => this.append(k, v));
        else if (Array.isArray(init)) for (const [k, v] of init) this.append(k, v);
        else if (init) for (const k of Object.keys(init)) this.append(k, init[k]);
    }
    append(name, value) {
        const k = String(name).toLowerCase();
        this.#map.set(k, this.#map.has(k) ? this.#map.get(k) + ", " + value : String(value));
    }
    set(name, value) { this.#map.set(String(name).toLowerCase(), String(value)); }
    get(name) { return this.#map.get(String(name).toLowerCase()) ?? null; }
    has(name) { return this.#map.has(String(name).toLowerCase()); }
    delete(name) { this.#map.delete(String(name).toLowerCase()); }
    forEach(fn, thisArg) { for (const [k, v] of [...this.#map].sort()) fn.call(thisArg, v, k, this); }
    entries() { return [...this.#map].sort()[Symbol.iterator](); }
    keys() { return [...this.#map.keys()].sort()[Symbol.iterator](); }
    values() { return [...this.#map].sort().map(([, v]) => v)[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
}

class Body {
    #bytes;
    #used = false;
    constructor(body) {
        this.#bytes = body instanceof URLSearchParams ? toBytes(body.toString()) : toBytes(body);
    }
    get bodyUsed() { return this.#used; }
    get body() { return null; }
    #take() {
        if (this.#used) return Promise.reject(new TypeError("Body has already been consumed."));
        this.#used = true;
        return Promise.resolve(this.#bytes);
    }
    arrayBuffer() { return this.#take().then(b => b.slice().buffer); }
    bytes() { return this.#take().then(b => b.slice()); }
    text() { return this.#take().then(b => decoder().decode(b)); }
    json() { return this.text().then(JSON.parse); }
    blob() { return this.#take().then(b => new Blob([b], { type: this.headers.get("content-type") || "" })); }
    formData() { return Promise.reject(new TypeError("formData() is not supported.")); }
    __bytes() { return this.#bytes; }
}

class Response extends Body {
    constructor(body = null, init = {}) {
        super(body);
        this.status = init.status ?? 200;
        this.statusText = init.statusText ?? "";
        this.headers = new Headers(init.headers);
        this.type = "basic";
        this.url = init.url || "";
        this.redirected = false;
    }
    get ok() { return this.status >= 200 && this.status < 300; }
    clone() { return new Response(this.__bytes(), this); }
    static json(data, init) { return new Response(JSON.stringify(data), { ...init, headers: { "content-type": "application/json" } }); }
    static error() { const r = new Response(null, { status: 0 }); r.type = "error"; return r; }
}

class Request extends Body {
    constructor(input, init = {}) {
        super(init.body ?? null);
        this.url = new URL(input instanceof Request ? input.url : String(input), PAGE_URL).href;
        this.method = String(init.method || (input instanceof Request ? input.method : "GET")).toUpperCase();
        this.headers = new Headers(init.headers || (input instanceof Request ? input.headers : undefined));
        this.signal = init.signal || null;
        this.credentials = init.credentials || "same-origin";
        this.mode = init.mode || "cors";
        this.cache = init.cache || "default";
    }
    clone() { return new Request(this); }
}

// A media file: [status, bytes, type]. Not found is a 404, other origins
// a network error (null).
function load(url) {
    let absolute;
    try {
        absolute = new URL(String(url), PAGE_URL);
    } catch (e) {
        return null;
    }
    if (absolute.protocol === "data:") {
        const m = absolute.href.match(/^data:([^,]*?)(;base64)?,(.*)$/s);
        if (!m) return null;
        const raw = m[2] ? atob(decodeURIComponent(m[3])) : decodeURIComponent(m[3]);
        const bytes = m[2] ? Uint8Array.from(raw, c => c.charCodeAt(0)) : new TextEncoder().encode(raw);
        return [200, bytes, m[1] || "text/plain"];
    }
    const path = mediaPath(absolute.href);
    if (path === null) return absolute.origin === "http://127.0.0.1" ? [404, new Uint8Array(0), "text/plain"] : null;
    const buffer = native.readMediaBytes(path);
    if (!buffer) return [404, new Uint8Array(0), "text/plain"];
    return [200, new Uint8Array(buffer), contentType(path)];
}

function fetch(input, init = {}) {
    const request = input instanceof Request && !Object.keys(init).length ? input : new Request(input, init);
    return new Promise((resolve, reject) => {
        if (request.signal && request.signal.aborted) return reject(request.signal.reason);
        queueTask(() => {
            const result = load(request.url);
            if (!result) {
                native.log(1, "fetch: no network in cards: " + request.url);
                reject(new TypeError("Failed to fetch"));
                return;
            }
            const [status, bytes, type] = result;
            resolve(new Response(request.method === "HEAD" ? null : bytes,
                { status, statusText: status === 200 ? "OK" : "Not Found", headers: { "content-type": type }, url: request.url }));
        });
    });
}

// ---------------------------------------------------------- XMLHttpRequest --

class XMLHttpRequestEventTarget extends linkedom.EventTarget {}

class XMLHttpRequest extends XMLHttpRequestEventTarget {
    static UNSENT = 0;
    static OPENED = 1;
    static HEADERS_RECEIVED = 2;
    static LOADING = 3;
    static DONE = 4;
    constructor() {
        super();
        Object.assign(this, {
            readyState: 0, status: 0, statusText: "", responseType: "", responseURL: "", timeout: 0,
            withCredentials: false, upload: new XMLHttpRequestEventTarget(),
        });
        this.__headers = {};
        this.__body = null;
        this.__aborted = false;
    }
    open(method, url, async = true) {
        this.__method = String(method).toUpperCase();
        this.__url = new URL(String(url), PAGE_URL).href;
        this.__async = async !== false;
        this.__aborted = false;
        this.__setState(1);
    }
    setRequestHeader() {}
    overrideMimeType(type) { this.__mime = type; }
    getResponseHeader(name) { return String(name).toLowerCase() === "content-type" ? this.__type || null : null; }
    getAllResponseHeaders() { return this.__type ? `content-type: ${this.__type}\r\n` : ""; }
    get responseText() {
        if (this.responseType !== "" && this.responseType !== "text") throw new DOMException("responseText is only for text responses.", "InvalidStateError");
        return this.__body ? decoder().decode(this.__body) : "";
    }
    get response() {
        if (this.readyState !== 4 || !this.__body) return this.responseType === "" || this.responseType === "text" ? "" : null;
        switch (this.responseType) {
            case "arraybuffer": return this.__body.slice().buffer;
            case "blob": return new Blob([this.__body], { type: this.__type });
            case "json":
                try { return JSON.parse(decoder().decode(this.__body)); } catch (e) { return null; }
            case "document": return new linkedom.DOMParser().parseFromString(decoder().decode(this.__body), "text/html");
            default: return decoder().decode(this.__body);
        }
    }
    get responseXML() { return this.responseType === "" || this.responseType === "document" ? this.response : null; }
    send() {
        if (this.readyState !== 1) throw new DOMException("The object's state must be OPENED.", "InvalidStateError");
        const finish = () => {
            if (this.__aborted) return;
            const result = load(this.__url);
            if (!result) {
                native.log(1, "XMLHttpRequest: no network in cards: " + this.__url);
                this.__setState(4);
                this.__fire("error");
                this.__fire("loadend");
                return;
            }
            [this.status, this.__body, this.__type] = result;
            this.statusText = this.status === 200 ? "OK" : "Not Found";
            this.responseURL = this.__url;
            this.__setState(2);
            this.__setState(3);
            this.__setState(4);
            this.__fire("load");
            this.__fire("loadend");
        };
        if (this.__async) {
            this.__fire("loadstart");
            queueTask(finish);
        } else finish();
    }
    abort() {
        this.__aborted = true;
        if (this.readyState !== 0 && this.readyState !== 4) {
            this.__setState(4);
            this.__fire("abort");
            this.__fire("loadend");
        }
        this.readyState = 0;
    }
    __setState(state) {
        this.readyState = state;
        this.__fire("readystatechange", false);
    }
    __fire(type, progress = true) {
        const event = progress ? new ProgressEvent(type, { lengthComputable: !!this.__body, loaded: this.__body ? this.__body.length : 0, total: this.__body ? this.__body.length : 0 }) : new Event(type);
        const handler = this["on" + type];
        try {
            if (typeof handler === "function") handler.call(this, event);
        } catch (e) {
            reportError(e);
        }
        dispatchEvent(this, event);
    }
}

for (const [name, value] of Object.entries({ Blob, File, FileReader, FormData, Headers, Request, Response, XMLHttpRequest, XMLHttpRequestEventTarget }))
    define(name, value);
define("fetch", fetch);
