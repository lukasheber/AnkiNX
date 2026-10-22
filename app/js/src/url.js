// URL and URLSearchParams (QuickJS has neither): the WHATWG URL Standard's
// parsing for what pages use (absolute and relative URLs, special schemes,
// dot segments, opaque URLs such as data: or playsound:), without IDNA.

const SPECIAL = { "http:": "80", "https:": "443", "ws:": "80", "wss:": "443", "ftp:": "21", "file:": "" };

// Percent-encode what the URL Standard encodes in each part (approximately:
// controls, space, and the characters each set adds).
function encode(s, extra) {
    let out = "";
    for (const ch of s) {
        const c = ch.codePointAt(0);
        if (c <= 0x20 || c === 0x7f || extra.includes(ch)) out += encodeURIComponent(ch) === ch ? "%" + c.toString(16).toUpperCase().padStart(2, "0") : encodeURIComponent(ch);
        else if (c > 0x7e) out += encodeURIComponent(ch);
        else out += ch;
    }
    return out;
}
const encodePath = s => encode(s, "\"#<>?`{}");
const encodeQuery = (s, special) => encode(s, special ? "\"#<>'" : "\"#<>");
const encodeFragment = s => encode(s, "\"<>`");
const encodeUserinfo = s => encode(s, "\"#<>?`{}/:;=@[\\]^|");

function normalizePath(path, special) {
    const segments = path.split(special ? /[/\\]/ : "/");
    const out = [];
    for (let i = 0; i < segments.length; i++) {
        const seg = segments[i];
        const dot = seg === "." || seg.toLowerCase() === "%2e";
        const dotdot = /^(\.|%2e)(\.|%2e)$/i.test(seg);
        const last = i === segments.length - 1;
        if (dotdot) {
            if (out.length > 1) out.pop();
            if (last) out.push("");
        } else if (dot) {
            if (last) out.push("");
        } else out.push(seg);
    }
    if (out.length === 0 || out[0] !== "") out.unshift("");
    return out.length === 1 ? "/" : out.join("/");
}

function parseAuthority(url, authority) {
    const at = authority.lastIndexOf("@");
    if (at >= 0) {
        const userinfo = authority.slice(0, at);
        authority = authority.slice(at + 1);
        const colon = userinfo.indexOf(":");
        url.username = encodeUserinfo(colon < 0 ? userinfo : userinfo.slice(0, colon));
        url.password = colon < 0 ? "" : encodeUserinfo(userinfo.slice(colon + 1));
    }
    const m = authority.match(/^(\[[^\]]*\]|[^:]*)(?::(\d*))?$/);
    if (!m) throw new TypeError("Invalid URL");
    url.hostname = m[1].toLowerCase();
    url.port = m[2] && m[2] !== SPECIAL[url.protocol] ? String(Number(m[2])) : "";
    if (url.protocol in SPECIAL && url.protocol !== "file:" && !url.hostname) throw new TypeError("Invalid URL");
}

// Fill `url` (a plain record) from `input` against `base`.
function parse(url, input, base) {
    input = input.replace(/^[\u0000- ]+|[\u0000- ]+$/g, "").replace(/[\t\n\r]/g, "");
    let rest = input;
    const scheme = rest.match(/^([a-zA-Z][a-zA-Z0-9+.-]*):/);
    let hashAt = rest.indexOf("#");
    let fragment = null;
    if (hashAt >= 0) {
        fragment = rest.slice(hashAt + 1);
        rest = rest.slice(0, hashAt);
    }
    let query = null;
    const setQueryFrom = s => {
        const q = s.indexOf("?");
        if (q < 0) return s;
        query = s.slice(q + 1);
        return s.slice(0, q);
    };

    if (scheme) {
        url.protocol = scheme[1].toLowerCase() + ":";
        rest = rest.slice(scheme[0].length);
        const special = url.protocol in SPECIAL;
        if (special || rest.startsWith("//")) {
            rest = special ? rest.replace(/^[/\\]*/, "") : rest.slice(2);
            rest = setQueryFrom(rest);
            const slash = rest.search(special ? /[/\\]/ : /\//);
            parseAuthority(url, slash < 0 ? rest : rest.slice(0, slash));
            url.pathname = encodePath(normalizePath(slash < 0 ? "" : rest.slice(slash), special));
            url.opaque = false;
        } else {
            rest = setQueryFrom(rest);
            url.hostname = url.port = url.username = url.password = "";
            url.opaque = !rest.startsWith("/");
            url.pathname = url.opaque ? encode(rest, "") : encodePath(normalizePath(rest, false));
        }
    } else {
        if (!base) throw new TypeError("Invalid URL");
        const special = base.protocol in SPECIAL;
        url.protocol = base.protocol;
        if (base.opaque && !input.startsWith("#")) throw new TypeError("Invalid URL");
        if (/^[/\\]{2}/.test(rest) && (special || rest.startsWith("//"))) {
            rest = setQueryFrom(rest.slice(2));
            const slash = rest.search(/[/\\]/);
            parseAuthority(url, slash < 0 ? rest : rest.slice(0, slash));
            url.pathname = encodePath(normalizePath(slash < 0 ? "" : rest.slice(slash), special));
            url.opaque = false;
        } else {
            Object.assign(url, { username: base.username, password: base.password, hostname: base.hostname, port: base.port, opaque: base.opaque });
            rest = setQueryFrom(rest);
            if (rest === "") {
                url.pathname = base.pathname;
                if (query === null) query = base.query;
            } else if (rest.startsWith("/") || (special && rest.startsWith("\\"))) url.pathname = encodePath(normalizePath(rest, special));
            else url.pathname = encodePath(normalizePath(base.pathname.replace(/[^/]*$/, "") + rest, special));
        }
    }
    url.query = query === null ? null : encodeQuery(query, url.protocol in SPECIAL);
    url.fragment = fragment === null ? null : encodeFragment(fragment);
}

const RECORD = Symbol("url");

export class URL {
    constructor(input, base) {
        const record = { protocol: "", username: "", password: "", hostname: "", port: "", pathname: "", query: null, fragment: null, opaque: false };
        const baseRecord = base === undefined ? undefined : (base instanceof URL ? base : new URL(String(base)))[RECORD];
        parse(record, String(input), baseRecord);
        this[RECORD] = record;
        this.searchParams = new URLSearchParams(record.query || "");
        this.searchParams.__url = this;
    }
    static canParse(input, base) {
        try {
            new URL(input, base);
            return true;
        } catch (e) {
            return false;
        }
    }
    static parse(input, base) {
        try {
            return new URL(input, base);
        } catch (e) {
            return null;
        }
    }
    static createObjectURL(blob) { return "blob:" + globalThis.location.origin + "/" + (blob && blob.__id ? blob.__id : Math.random().toString(36).slice(2)); }
    static revokeObjectURL() {}

    get href() {
        const r = this[RECORD];
        let out = r.protocol;
        if (!r.opaque && (r.hostname || r.protocol in SPECIAL)) {
            out += "//";
            if (r.username || r.password) out += r.username + (r.password ? ":" + r.password : "") + "@";
            out += this.host;
        }
        out += r.pathname;
        if (r.query !== null) out += "?" + r.query;
        if (r.fragment !== null) out += "#" + r.fragment;
        return out;
    }
    set href(value) {
        const record = { protocol: "", username: "", password: "", hostname: "", port: "", pathname: "", query: null, fragment: null, opaque: false };
        parse(record, String(value));
        this[RECORD] = record;
        this.searchParams.__replace(record.query || "");
    }
    get origin() {
        const r = this[RECORD];
        return r.protocol in SPECIAL && r.protocol !== "file:" ? `${r.protocol}//${this.host}` : "null";
    }
    get protocol() { return this[RECORD].protocol; }
    set protocol(v) { v = String(v).replace(/:.*$/, "").toLowerCase(); if (/^[a-z][a-z0-9+.-]*$/.test(v)) this[RECORD].protocol = v + ":"; }
    get username() { return this[RECORD].username; }
    set username(v) { this[RECORD].username = encodeUserinfo(String(v)); }
    get password() { return this[RECORD].password; }
    set password(v) { this[RECORD].password = encodeUserinfo(String(v)); }
    get host() { const r = this[RECORD]; return r.hostname + (r.port ? ":" + r.port : ""); }
    set host(v) { parseAuthority(this[RECORD], String(v)); }
    get hostname() { return this[RECORD].hostname; }
    set hostname(v) { this[RECORD].hostname = String(v).toLowerCase(); }
    get port() { return this[RECORD].port; }
    set port(v) { v = String(v); this[RECORD].port = v === "" || v === SPECIAL[this[RECORD].protocol] ? "" : String(parseInt(v, 10) || ""); }
    get pathname() { return this[RECORD].pathname; }
    set pathname(v) {
        const r = this[RECORD];
        if (!r.opaque) r.pathname = encodePath(normalizePath(String(v).startsWith("/") ? String(v) : "/" + v, r.protocol in SPECIAL));
    }
    get search() { const q = this[RECORD].query; return q ? "?" + q : ""; }
    set search(v) {
        v = String(v).replace(/^\?/, "");
        this[RECORD].query = v === "" ? null : encodeQuery(v, this[RECORD].protocol in SPECIAL);
        this.searchParams.__replace(v);
    }
    get hash() { const f = this[RECORD].fragment; return f ? "#" + f : ""; }
    set hash(v) { v = String(v).replace(/^#/, ""); this[RECORD].fragment = v === "" ? null : encodeFragment(v); }
    toString() { return this.href; }
    toJSON() { return this.href; }
    // Used by URLSearchParams when it changes.
    __setQuery(q) { this[RECORD].query = q === "" ? null : q; }
}

const formEncode = s => encodeURIComponent(s).replace(/%20/g, "+").replace(/[!'()~]/g, c => "%" + c.charCodeAt(0).toString(16).toUpperCase());
const formDecode = s => {
    try {
        return decodeURIComponent(s.replace(/\+/g, " "));
    } catch (e) {
        return s;
    }
};

export class URLSearchParams {
    constructor(init = "") {
        this.__list = [];
        this.__url = null;
        if (init instanceof URLSearchParams) this.__list = init.__list.map(p => p.slice());
        else if (typeof init === "object" && init !== null) {
            if (typeof init[Symbol.iterator] === "function")
                for (const pair of init) this.__list.push([String(pair[0]), String(pair[1])]);
            else for (const key of Object.keys(init)) this.__list.push([key, String(init[key])]);
        } else this.__replace(String(init));
    }
    __replace(query) {
        this.__list = String(query).replace(/^\?/, "").split("&").filter(Boolean).map(part => {
            const eq = part.indexOf("=");
            return eq < 0 ? [formDecode(part), ""] : [formDecode(part.slice(0, eq)), formDecode(part.slice(eq + 1))];
        });
    }
    __update() { if (this.__url) this.__url.__setQuery(this.toString()); }
    get size() { return this.__list.length; }
    append(name, value) { this.__list.push([String(name), String(value)]); this.__update(); }
    delete(name, value) {
        this.__list = this.__list.filter(([n, v]) => n !== String(name) || (value !== undefined && v !== String(value)));
        this.__update();
    }
    get(name) { const p = this.__list.find(([n]) => n === String(name)); return p ? p[1] : null; }
    getAll(name) { return this.__list.filter(([n]) => n === String(name)).map(([, v]) => v); }
    has(name, value) { return this.__list.some(([n, v]) => n === String(name) && (value === undefined || v === String(value))); }
    set(name, value) {
        name = String(name);
        const i = this.__list.findIndex(([n]) => n === name);
        if (i < 0) this.__list.push([name, String(value)]);
        else {
            this.__list[i][1] = String(value);
            this.__list = this.__list.filter(([n], j) => n !== name || j === i);
        }
        this.__update();
    }
    sort() { this.__list.sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0)); this.__update(); }
    forEach(fn, thisArg) { for (const [n, v] of this.__list) fn.call(thisArg, v, n, this); }
    keys() { return this.__list.map(([n]) => n)[Symbol.iterator](); }
    values() { return this.__list.map(([, v]) => v)[Symbol.iterator](); }
    entries() { return this.__list.map(p => p.slice())[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
    toString() { return this.__list.map(([n, v]) => formEncode(n) + "=" + formEncode(v)).join("&"); }
}
