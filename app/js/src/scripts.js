// <script> elements as browsers run them (HTML Standard, "prepare the script
// element"): a script runs when it is inserted into the document; inline
// ones at once, src ones as a task once read from the media folder, then
// "load" (or "error"). Scripts made by parsing markup (innerHTML,
// insertAdjacentHTML, DOMParser) never run, as in browsers, which is why
// Anki's reviewer re-creates the side's scripts (reviewer.js).
// document.currentScript and document.write() during a script work.
import * as linkedom from "linkedom";
import { native } from "./native.js";
import { document } from "./dom.js";
import { dispatchEvent, Event } from "./events.js";
import { reportError } from "./errors.js";
import { bundledLibrary, mediaPath } from "./media.js";
import { queueTask } from "./timers.js";

const started = new WeakSet(); // "already started"
let parserDepth = 0;
let current = null;

// Comments end as in browsers (HTML Standard, comment states): at "-->", at
// "--!>" (shared-deck templates write "<!-- ... --!>"), and "<!-->" and
// "<!--->" are empty comments. htmlparser2 only knows "-->", so it would run
// such a comment, scripts and all, to the end of the markup. The text of
// script, style, textarea and title elements is left alone.
const RAW_TEXT = /<(script|style|textarea|title)(?=[\s/>])/gi;
export function closeComments(markup) {
    if (typeof markup !== "string" || !markup.includes("<!--")) return markup;
    let out = "";
    let i = 0;
    while (i < markup.length) {
        const comment = markup.indexOf("<!--", i);
        RAW_TEXT.lastIndex = i;
        const raw = RAW_TEXT.exec(markup);
        if (raw && (comment < 0 || raw.index < comment)) {
            const close = markup.toLowerCase().indexOf("</" + raw[1].toLowerCase(), RAW_TEXT.lastIndex);
            const end = close < 0 ? markup.length : close + 2 + raw[1].length;
            out += markup.slice(i, end);
            i = end;
            continue;
        }
        if (comment < 0) break;
        out += markup.slice(i, comment);
        const body = comment + 4;
        if (markup.startsWith(">", body) || markup.startsWith("->", body)) {
            out += "<!---->";
            i = body + (markup[body] === ">" ? 1 : 2);
            continue;
        }
        const a = markup.indexOf("-->", body);
        const b = markup.indexOf("--!>", body);
        if (b >= 0 && (a < 0 || b < a)) {
            out += markup.slice(comment, b) + "-->";
            i = b + 4;
        } else {
            const end = a < 0 ? markup.length : a + 3;
            out += markup.slice(comment, end);
            i = end;
        }
    }
    return out + markup.slice(i);
}

// `markupAt`: the index of the argument holding markup, if any.
function parsing(fn, markupAt = -1) {
    return function (...args) {
        parserDepth++;
        try {
            if (markupAt >= 0) args[markupAt] = closeComments(args[markupAt]);
            return fn.apply(this, args);
        } finally {
            parserDepth--;
        }
    };
}
function wrapSetter(proto, name) {
    const d = Object.getOwnPropertyDescriptor(proto, name);
    if (d && d.set) Object.defineProperty(proto, name, { ...d, set: parsing(d.set, 0) });
}
wrapSetter(linkedom.Element.prototype, "innerHTML");
wrapSetter(linkedom.Element.prototype, "outerHTML");
wrapSetter(linkedom.ShadowRoot.prototype, "innerHTML");
linkedom.Element.prototype.insertAdjacentHTML = parsing(linkedom.Element.prototype.insertAdjacentHTML, 1);
const parseHTMLOrXML = linkedom.DOMParser.prototype.parseFromString;
linkedom.DOMParser.prototype.parseFromString = parsing(function (markup, type, ...rest) {
    if (String(type).toLowerCase() === "text/html") markup = closeComments(markup);
    return parseHTMLOrXML.call(this, markup, type, ...rest);
});

// All insertions go through ParentNode's insertBefore.
let PNP = linkedom.Element.prototype;
while (!Object.prototype.hasOwnProperty.call(PNP, "insertBefore")) PNP = Object.getPrototypeOf(PNP);
const insertBefore = PNP.insertBefore;
PNP.insertBefore = function (node, before) {
    let scripts = [];
    if (node && node.nodeType === 1) scripts = node.localName === "script" ? [node] : [...node.getElementsByTagName("script")];
    else if (node && node.nodeType === 11) scripts = [...node.querySelectorAll("script")];
    const result = insertBefore.call(this, node, before);
    if (parserDepth > 0) for (const s of scripts) started.add(s);
    else {
        for (const s of scripts) prepare(s);
        // Text added to an empty inline script that is in the document.
        if (this.localName === "script") prepare(this);
    }
    return result;
};

const CLASSIC = ["", "text/javascript", "application/javascript", "text/ecmascript", "application/ecmascript",
    "application/x-javascript", "text/x-javascript", "text/jscript", "text/livescript", "javascript"];
function scriptKind(script) {
    const type = script.getAttribute("type");
    const language = script.getAttribute("language");
    if (type === null ? language === null || language === "" : type === "") return "classic";
    if (type === null) return CLASSIC.includes("text/" + language.toLowerCase()) ? "classic" : null;
    const t = type.trim().toLowerCase();
    if (t === "module") return "module";
    return CLASSIC.includes(t) ? "classic" : null;
}

function prepare(script) {
    if (started.has(script) || !script.isConnected || script.ownerDocument !== document) return;
    const src = script.getAttribute("src");
    if (src === null && script.textContent === "") return;
    const kind = scriptKind(script);
    if (!kind) return;
    started.add(script);
    if (src !== null) queueTask(() => runExternal(script, src, kind));
    else if (kind === "module") queueTask(() => runModule(script, script.textContent, "inline module"));
    else run(script, script.textContent, "inline script");
}

function run(script, code, name) {
    const previous = current;
    current = script;
    try {
        native.evalScript(code, name);
    } catch (e) {
        reportError(e, name);
    } finally {
        current = previous;
    }
}

function runModule(script, code, name) {
    let promise;
    try {
        promise = native.evalModule(code, name);
    } catch (e) {
        reportError(e, name);
        return;
    }
    Promise.resolve(promise).catch(e => reportError(e, name));
}

function runExternal(script, src, kind) {
    const path = mediaPath(src);
    const code = path === null ? null : native.readMedia(path);
    if (code === null) {
        if (bundledLibrary(src)) {
            dispatchEvent(script, new Event("load"));
            return;
        }
        native.log(1, `script not found in the media folder: ${src}`);
        dispatchEvent(script, new Event("error"));
        return;
    }
    if (kind === "module") runModule(script, code, path);
    else run(script, code, path);
    dispatchEvent(script, new Event("load"));
}

Object.defineProperty(document, "currentScript", { get: () => current, configurable: true });

// document.write() from a running script adds markup after it, as while a
// page loads (AnkiDroid loads each side as a page). Elsewhere it would
// replace the whole page; that is ignored.
const writePoints = new WeakMap();
function write(...parts) {
    const script = current;
    if (!script || !script.isConnected) {
        native.log(1, "document.write() outside a running script is ignored");
        return;
    }
    const template = document.createElement("template");
    template.innerHTML = parts.join("");
    const nodes = [...template.childNodes];
    // Parsed into the template, but scripts in written markup run.
    for (const s of nodes.flatMap(n => (n.nodeType === 1 ? (n.localName === "script" ? [n] : [...n.getElementsByTagName("script")]) : [])))
        started.delete(s);
    const after = writePoints.get(script) || script;
    const parent = after.parentNode;
    let reference = after.nextSibling;
    for (const node of nodes) {
        parent.insertBefore(node, reference);
        writePoints.set(script, node);
    }
}
document.write = (...parts) => write(...parts);
document.writeln = (...parts) => write(...parts, "\n");

// The script elements' own properties.
const SP = linkedom.HTMLScriptElement.prototype;
Object.defineProperty(SP, "text", {
    get() { return this.textContent; },
    set(value) { this.textContent = String(value); },
    configurable: true,
});
for (const name of ["async", "defer", "noModule"])
    Object.defineProperty(SP, name, {
        get() { return this.hasAttribute(name.toLowerCase()); },
        set(value) { this.toggleAttribute(name.toLowerCase(), !!value); },
        configurable: true,
    });
for (const name of ["src", "type", "charset", "crossOrigin", "integrity", "referrerPolicy"])
    Object.defineProperty(SP, name, {
        get() { return this.getAttribute(name.toLowerCase()) ?? ""; },
        set(value) { this.setAttribute(name.toLowerCase(), String(value)); },
        configurable: true,
    });
