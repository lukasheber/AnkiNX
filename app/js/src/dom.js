// The card page and the DOM on the global object. linkedom provides the
// tree, the HTML parser and serialiser, selectors and the element classes;
// this fills in what a page expects of a browser's document and makes the
// classes that browsers let scripts construct constructible.
import * as linkedom from "linkedom";
// linkedom's links between nodes (a relative path: not exported, and the
// same module linkedom uses).
import { NEXT, PREV } from "../node_modules/linkedom/esm/shared/symbols.js";
import { native } from "./native.js";
import { Event, createEvent, handlerAttributeChanged } from "./events.js";

// linkedom has classes for these elements but doesn't create them for their
// tags (they come out as plain HTMLElement, without form.requestSubmit,
// label.control and the like). They hold no state of their own, so an
// element can take its class's prototype when it is created.
const TAG_CLASSES = {
    area: "HTMLAreaElement", audio: "HTMLAudioElement", base: "HTMLBaseElement", body: "HTMLBodyElement",
    br: "HTMLBRElement", dl: "HTMLDListElement", data: "HTMLDataElement", datalist: "HTMLDataListElement",
    details: "HTMLDetailsElement", dir: "HTMLDirectoryElement", div: "HTMLDivElement", embed: "HTMLEmbedElement",
    fieldset: "HTMLFieldSetElement", font: "HTMLFontElement", form: "HTMLFormElement", frame: "HTMLFrameElement",
    frameset: "HTMLFrameSetElement", head: "HTMLHeadElement", hr: "HTMLHRElement", html: "HTMLHtmlElement",
    label: "HTMLLabelElement", legend: "HTMLLegendElement", li: "HTMLLIElement", map: "HTMLMapElement",
    marquee: "HTMLMarqueeElement", menu: "HTMLMenuElement", meter: "HTMLMeterElement", ins: "HTMLModElement",
    del: "HTMLModElement", ol: "HTMLOListElement", object: "HTMLObjectElement", optgroup: "HTMLOptGroupElement",
    output: "HTMLOutputElement", p: "HTMLParagraphElement", param: "HTMLParamElement", picture: "HTMLPictureElement",
    pre: "HTMLPreElement", listing: "HTMLPreElement", xmp: "HTMLPreElement", progress: "HTMLProgressElement",
    q: "HTMLQuoteElement", blockquote: "HTMLQuoteElement", span: "HTMLSpanElement", caption: "HTMLTableCaptionElement",
    td: "HTMLTableCellElement", th: "HTMLTableCellElement", table: "HTMLTableElement", tr: "HTMLTableRowElement",
    track: "HTMLTrackElement", ul: "HTMLUListElement", video: "HTMLVideoElement",
};
for (const name of ["HTMLAudioElement", "HTMLVideoElement"])
    if (linkedom[name] && linkedom.HTMLMediaElement) {
        Object.setPrototypeOf(linkedom[name].prototype, linkedom.HTMLMediaElement.prototype);
        Object.setPrototypeOf(linkedom[name], linkedom.HTMLMediaElement);
    }
const HTMLDocumentProto = Object.getPrototypeOf(linkedom.parseHTML("").document);
const createElement = HTMLDocumentProto.createElement;
HTMLDocumentProto.createElement = function (localName, options) {
    const element = createElement.call(this, localName, options);
    const Class = Object.getPrototypeOf(element) === linkedom.HTMLElement.prototype && linkedom[TAG_CLASSES[String(localName).toLowerCase()]];
    if (Class) Object.setPrototypeOf(element, Class.prototype);
    return element;
};

// A new attribute goes after the element's others, as in browsers (linkedom
// puts it first; the order shows in outerHTML and element.attributes).
function moveToEnd(attribute) {
    let last = attribute;
    while (last[NEXT] && last[NEXT].nodeType === 2) last = last[NEXT];
    if (last === attribute) return;
    const prev = attribute[PREV];
    const next = attribute[NEXT];
    prev[NEXT] = next;
    next[PREV] = prev;
    const after = last[NEXT];
    last[NEXT] = attribute;
    attribute[PREV] = last;
    attribute[NEXT] = after;
    after[PREV] = attribute;
}
for (const name of ["setAttribute", "setAttributeNode"]) {
    const original = linkedom.Element.prototype[name];
    linkedom.Element.prototype[name] = function (first, ...rest) {
        const attrName = name === "setAttribute" ? String(first) : first.name;
        const had = this.getAttributeNode(attrName);
        const result = original.call(this, first, ...rest);
        const now = this.getAttributeNode(attrName);
        if (now && now !== had) moveToEnd(now);
        if (/^on/i.test(attrName) && this.nodeType === 1) handlerAttributeChanged(this, attrName.toLowerCase());
        return result;
    };
}

// Anki desktop's reviewer page (qt/aqt/reviewer.py, revHtml): each side is
// put into #qa (reviewer.js).
const page = linkedom.parseHTML("<!doctype html><html><head></head><body><div id=\"qa\"></div></body></html>");
export const document = page.document;
const linkedomView = document.defaultView;

export function define(name, value) {
    Object.defineProperty(globalThis, name, { value, writable: true, configurable: true });
}

for (const [name, value] of Object.entries(linkedom))
    if (/^[A-Z]/.test(name) && typeof value === "function") define(name, value);

// Classes browsers let scripts construct (linkedom's facades throw).
function constructible(Class, create) {
    const C = function (...args) { return create(...args); };
    Object.defineProperty(C, "name", { value: Class.name });
    C.prototype = Class.prototype;
    Object.setPrototypeOf(C, Class);
    return C;
}
define("Text", constructible(linkedom.Text, (data = "") => document.createTextNode(String(data))));
define("Comment", constructible(linkedom.Comment, (data = "") => document.createComment(String(data))));
define("DocumentFragment", constructible(linkedom.DocumentFragment, () => document.createDocumentFragment()));
define("Range", constructible(document.createRange().constructor, () => document.createRange()));
define("HTMLDocument", linkedom.Document);
define("TreeWalker", document.createTreeWalker(document).constructor);
define("NodeIterator", document.createTreeWalker(document).constructor);
define("HTMLCollection", linkedom.NodeList);
define("DOMTokenList", document.documentElement.classList.constructor);
// linkedom records added and removed children only among elements unless
// characterData is observed too; browsers record text nodes as well.
const MutationObserver = linkedomView.MutationObserver;
const observe = MutationObserver.prototype.observe;
MutationObserver.prototype.observe = function (target, options = {}) {
    return observe.call(this, target, { ...options, characterData: !!(options.characterData || options.childList) });
};
define("MutationObserver", MutationObserver);
// The HTML parser puts markup without <html> or <body> into the body.
const parseFromString = linkedom.DOMParser.prototype.parseFromString;
linkedom.DOMParser.prototype.parseFromString = function (markup, type, ...rest) {
    markup = String(markup);
    if (String(type).toLowerCase() === "text/html" && !/<(html|body)[\s>]/i.test(markup))
        markup = `<!doctype html><html><head></head><body>${markup}</body></html>`;
    return parseFromString.call(this, markup, type, ...rest);
};
define("Image", linkedomView.Image);
define("customElements", linkedomView.customElements);
define("document", document);

// --------------------------------------------------------------- document --

function getter(name, get, set) {
    Object.defineProperty(document, name, { get, set, configurable: true, enumerable: true });
}

let readyState = "complete";
export function setReadyState(value) {
    readyState = value;
    document.dispatchEvent(new Event("readystatechange"));
}
getter("readyState", () => readyState);
getter("defaultView", () => globalThis);
getter("location", () => globalThis.location, value => { globalThis.location.href = value; });
getter("URL", () => globalThis.location.href);
getter("documentURI", () => globalThis.location.href);
getter("baseURI", () => globalThis.location.href);
getter("referrer", () => "");
getter("domain", () => globalThis.location.hostname);
getter("visibilityState", () => "visible");
getter("hidden", () => false);
getter("characterSet", () => "UTF-8");
getter("charset", () => "UTF-8");
getter("contentType", () => "text/html");
getter("compatMode", () => "CSS1Compat");
getter("scrollingElement", () => document.documentElement);
getter("designMode", () => "off");
getter("fonts", () => fonts);
const fonts = {
    status: "loaded", ready: Promise.resolve(), size: 0,
    check: () => true, load: () => Promise.resolve([]), add() {}, delete: () => false, clear() {},
    addEventListener() {}, removeEventListener() {}, forEach() {}, [Symbol.iterator]: () => [][Symbol.iterator](),
};

// Cookies: name=value pairs kept for the app session, like the storages.
getter("cookie",
    () => {
        const out = [];
        const count = Number(native.storage("cookie", "length", "")) || 0;
        for (let i = 0; i < count; i++) {
            const key = native.storage("cookie", "key", String(i));
            out.push(key + "=" + native.storage("cookie", "get", key));
        }
        return out.join("; ");
    },
    value => {
        const [pair, ...attributes] = String(value).split(";");
        const eq = pair.indexOf("=");
        const key = (eq < 0 ? "" : pair.slice(0, eq)).trim();
        const val = (eq < 0 ? pair : pair.slice(eq + 1)).trim();
        const expired = attributes.some(a => {
            const [k, v = ""] = a.split("=").map(s => s.trim());
            return (k.toLowerCase() === "max-age" && Number(v) <= 0) ||
                (k.toLowerCase() === "expires" && Date.parse(v) <= Date.now());
        });
        if (expired) native.storage("cookie", "remove", key);
        else native.storage("cookie", "set", key, val);
    });

const implementation = {
    createHTMLDocument(title) {
        const doc = linkedom.parseHTML("<!doctype html><html><head></head><body></body></html>").document;
        if (title !== undefined) doc.title = String(title);
        return doc;
    },
    createDocument(namespace, qualifiedName) {
        const doc = new linkedom.DOMParser().parseFromString("<?xml version=\"1.0\"?><root/>", "text/xml");
        doc.removeChild(doc.documentElement);
        if (qualifiedName) doc.appendChild(doc.createElementNS(namespace, qualifiedName));
        return doc;
    },
    createDocumentType: (name, publicId, systemId) => document.createDocumentType(name, publicId, systemId),
    hasFeature: () => true,
};
getter("implementation", () => implementation);
document.createEvent = createEvent;
document.elementFromPoint = () => null;
document.elementsFromPoint = () => [];
document.open = () => document;
document.close = () => {};
document.execCommand = () => false;
document.queryCommandSupported = () => false;
document.queryCommandEnabled = () => false;
document.hasStorageAccess = () => Promise.resolve(false);
document.exitFullscreen = () => Promise.resolve();
