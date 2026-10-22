// Layout answers from the renderer: getComputedStyle, offset*/client*/
// scroll* sizes, getBoundingClientRect and getClientRects come from
// litehtml's layout of the current DOM (native.measure, which lays the HTML
// out the way the card view draws it). Reading one after the DOM changed
// lays the card out again first, as browsers do. Without a renderer (host
// tests) boxes are empty and styles come from the style attribute.
import * as linkedom from "linkedom";
import { native } from "./native.js";
import { define, document } from "./dom.js";
import { formsVersion } from "./forms.js";
import { extraCss, serializeBody, serialOf } from "./serialize.js";
import { kebab, parseDeclarations } from "./style.js";

// ---------------------------------------------------------- invalidation --

let mutations = 0;
const observer = new MutationObserver(() => { mutations++; });
observer.observe(document, { childList: true, subtree: true, attributes: true, characterData: true });

// Changes whenever the DOM or control state changes.
export function domVersion() {
    if (observer.takeRecords().length) mutations++;
    return `${mutations}:${formsVersion()}`;
}

let laidOut = "";
let boxes = null;
function layout() {
    const version = domVersion();
    if (version !== laidOut) {
        laidOut = version;
        boxes = native.measure(serializeBody(), document.body.className, extraCss()) || null;
    }
    return boxes;
}
// [x, y, width, height, clientLeft, clientTop, clientWidth, clientHeight, style], or null.
function boxOf(element) {
    if (!element || element.nodeType !== 1 || !element.isConnected) return null;
    const all = layout();
    return all ? all[serialOf(element)] || null : null;
}

function viewport() {
    const [width, height, scrollY, ratio] = native.viewport();
    return { width, height, scrollY, ratio };
}

// ---------------------------------------------------------------- DOMRect --

export class DOMRectReadOnly {
    constructor(x = 0, y = 0, width = 0, height = 0) {
        Object.assign(this, { x: +x, y: +y, width: +width, height: +height });
    }
    get top() { return Math.min(this.y, this.y + this.height); }
    get bottom() { return Math.max(this.y, this.y + this.height); }
    get left() { return Math.min(this.x, this.x + this.width); }
    get right() { return Math.max(this.x, this.x + this.width); }
    toJSON() { const { x, y, width, height, top, right, bottom, left } = this; return { x, y, width, height, top, right, bottom, left }; }
    static fromRect(r = {}) { return new this(r.x, r.y, r.width, r.height); }
}
export class DOMRect extends DOMRectReadOnly {}
define("DOMRectReadOnly", DOMRectReadOnly);
define("DOMRect", DOMRect);
define("DOMPoint", class DOMPoint {
    constructor(x = 0, y = 0, z = 0, w = 1) { Object.assign(this, { x, y, z, w }); }
});

// ---------------------------------------------------------- computed style --

const BLOCK = new Set(("address article aside blockquote body center dd details dialog dir div dl dt fieldset figcaption " +
    "figure footer form h1 h2 h3 h4 h5 h6 header hgroup hr html legend listing main menu nav ol p plaintext pre " +
    "search section summary ul xmp").split(" "));
const NONE = new Set("area base basefont datalist head link meta noembed noframes param rp script style template title".split(" "));
const DEFAULT_DISPLAY = {
    li: "list-item", table: "table", caption: "table-caption", colgroup: "table-column-group", col: "table-column",
    thead: "table-header-group", tbody: "table-row-group", tfoot: "table-footer-group", tr: "table-row",
    td: "table-cell", th: "table-cell", ruby: "ruby", rt: "ruby-text", img: "inline", button: "inline-block",
    input: "inline-block", select: "inline-block", textarea: "inline-block", meter: "inline-block", progress: "inline-block",
};
function defaultDisplay(element) {
    const tag = element.localName;
    if (element.hasAttribute("hidden") || NONE.has(tag)) return "none";
    if (BLOCK.has(tag)) return "block";
    return DEFAULT_DISPLAY[tag] || "inline";
}

const INHERITED = new Set(["color", "font-size", "font-family", "font-weight", "font-style", "line-height", "text-align",
    "visibility", "white-space", "direction", "letter-spacing", "word-spacing", "cursor", "list-style-type", "text-transform"]);
const INITIAL = {
    display: "inline", visibility: "visible", position: "static", float: "none", opacity: "1", overflow: "visible",
    "overflow-x": "visible", "overflow-y": "visible", color: "rgb(0, 0, 0)", "background-color": "rgba(0, 0, 0, 0)",
    "font-size": "16px", "font-weight": "400", "font-style": "normal", "text-align": "start", "line-height": "normal",
    "white-space": "normal", "box-sizing": "content-box", "z-index": "auto", transform: "none", "pointer-events": "auto",
    "text-decoration-line": "none", "list-style-type": "disc", cursor: "auto", direction: "ltr",
    top: "auto", left: "auto", right: "auto", bottom: "auto", "max-width": "none", "max-height": "none",
    "min-width": "0px", "min-height": "0px", "transition-duration": "0s", "animation-name": "none",
    margin: "0px", "margin-top": "0px", "margin-right": "0px", "margin-bottom": "0px", "margin-left": "0px",
    padding: "0px", "padding-top": "0px", "padding-right": "0px", "padding-bottom": "0px", "padding-left": "0px",
    "border-top-width": "0px", "border-right-width": "0px", "border-bottom-width": "0px", "border-left-width": "0px",
};

function computedValue(element, name, pseudo) {
    if (pseudo) return INITIAL[name] ?? "";
    const box = boxOf(element);
    // What the renderer computed.
    if (box && box[8] && box[8][name] !== undefined) return box[8][name];
    if (box && name === "width") return box[6] + "px";
    if (box && name === "height") return box[7] + "px";
    // Otherwise the cascade we can see: the style attribute, inheritance, defaults.
    for (let el = element; el && el.nodeType === 1; el = el.parentNode) {
        const d = parseDeclarations(el.getAttribute("style")).find(([n]) => n === name);
        if (d) return d[1];
        if (el !== element && !INHERITED.has(name)) break;
        if (el === element && name === "display") return defaultDisplay(el);
    }
    if (name === "width" || name === "height") return "auto";
    return INITIAL[name] ?? "";
}

class CSSStyleDeclarationReadOnly {
    constructor(element, pseudo) {
        this.__element = element;
        this.__pseudo = pseudo;
    }
    getPropertyValue(name) { return computedValue(this.__element, kebab(name), this.__pseudo); }
    getPropertyPriority() { return ""; }
    setProperty() { throw new DOMException("Computed styles are read-only.", "NoModificationAllowedError"); }
    removeProperty() { throw new DOMException("Computed styles are read-only.", "NoModificationAllowedError"); }
    get cssText() { return ""; }
    get length() { return 0; }
    item() { return ""; }
    get cssFloat() { return this.getPropertyValue("float"); }
}
function getComputedStyle(element, pseudo = null) {
    if (!element || element.nodeType !== 1) throw new TypeError("getComputedStyle: parameter 1 is not of type 'Element'.");
    const target = new CSSStyleDeclarationReadOnly(element, pseudo);
    return new Proxy(target, {
        get(t, key) {
            if (typeof key !== "string" || key in t) {
                const v = Reflect.get(t, key, t);
                return typeof v === "function" ? v.bind(t) : v;
            }
            return /^\d+$/.test(key) ? "" : t.getPropertyValue(key);
        },
        has(t, key) { return typeof key === "string" || key in t; },
    });
}
define("getComputedStyle", getComputedStyle);

// ----------------------------------------------------------- element boxes --

const isRoot = el => el === document.documentElement || el === document.body;

function prop(name, get, set) {
    Object.defineProperty(linkedom.Element.prototype, name, { get, set, configurable: true });
    Object.defineProperty(linkedom.HTMLElement.prototype, name, { get, set, configurable: true });
}

function rect(element) {
    const box = boxOf(element);
    if (!box) return new DOMRect(0, 0, 0, 0);
    const { scrollY } = viewport();
    return new DOMRect(box[0], box[1] - scrollY, box[2], box[3]);
}
linkedom.Element.prototype.getBoundingClientRect = function () { return rect(this); };
linkedom.Element.prototype.getClientRects = function () {
    const box = boxOf(this);
    const list = box && (box[2] || box[3] || computedValue(this, "display") !== "none") && box[8]?.display !== "none" ? [rect(this)] : [];
    list.item = i => list[i] || null;
    return list;
};

function offsetParent(element) {
    if (!boxOf(element) || computedValue(element, "display") === "none" || isRoot(element)) return null;
    for (let p = element.parentNode; p && p.nodeType === 1; p = p.parentNode) {
        if (p === document.body) return p;
        if (computedValue(p, "position") !== "static" || ["td", "th", "table"].includes(p.localName)) return p;
    }
    return document.body;
}
prop("offsetParent", function () { return offsetParent(this); });
prop("offsetWidth", function () { const b = boxOf(this); return b ? Math.round(b[2]) : 0; });
prop("offsetHeight", function () { const b = boxOf(this); return b ? Math.round(b[3]) : 0; });
prop("offsetLeft", function () {
    const b = boxOf(this);
    const p = b && offsetParent(this);
    const pb = p && boxOf(p);
    return b ? Math.round(b[0] - (pb && p !== document.body ? pb[0] + pb[4] : 0)) : 0;
});
prop("offsetTop", function () {
    const b = boxOf(this);
    const p = b && offsetParent(this);
    const pb = p && boxOf(p);
    return b ? Math.round(b[1] - (pb && p !== document.body ? pb[1] + pb[5] : 0)) : 0;
});
prop("clientLeft", function () { const b = boxOf(this); return b ? Math.round(b[4]) : 0; });
prop("clientTop", function () { const b = boxOf(this); return b ? Math.round(b[5]) : 0; });
prop("clientWidth", function () {
    if (this === document.documentElement) return Math.round(viewport().width);
    const b = boxOf(this);
    return b ? Math.round(b[6]) : 0;
});
prop("clientHeight", function () {
    if (this === document.documentElement) return Math.round(viewport().height);
    const b = boxOf(this);
    return b ? Math.round(b[7]) : 0;
});
function documentHeight() {
    const all = layout();
    let bottom = 0;
    if (all) for (const key in all) bottom = Math.max(bottom, all[key][1] + all[key][3]);
    return Math.max(bottom, viewport().height);
}
prop("scrollWidth", function () { return isRoot(this) ? Math.round(viewport().width) : this.clientWidth; });
prop("scrollHeight", function () { return isRoot(this) ? Math.round(documentHeight()) : this.clientHeight; });
prop("scrollTop", function () { return isRoot(this) ? viewport().scrollY : 0; },
    function (v) { if (isRoot(this)) native.scrollTo(Number(v) || 0); });
prop("scrollLeft", () => 0, () => {});
linkedom.Element.prototype.scrollIntoView = function () {
    const b = boxOf(this);
    if (b) native.scrollTo(b[1]);
};
linkedom.Element.prototype.scrollIntoViewIfNeeded = linkedom.Element.prototype.scrollIntoView;
for (const name of ["scroll", "scrollTo", "scrollBy"]) linkedom.Element.prototype[name] = function () {};
linkedom.Element.prototype.checkVisibility = function () { return this.getClientRects().length > 0; };

// ------------------------------------------------------------------ window --

const winProp = (name, get) => Object.defineProperty(globalThis, name, { get, set() {}, configurable: true });
winProp("innerWidth", () => Math.round(viewport().width));
winProp("innerHeight", () => Math.round(viewport().height));
winProp("outerWidth", () => Math.round(viewport().width));
winProp("outerHeight", () => Math.round(viewport().height));
winProp("scrollX", () => 0);
winProp("pageXOffset", () => 0);
winProp("scrollY", () => viewport().scrollY);
winProp("pageYOffset", () => viewport().scrollY);
winProp("devicePixelRatio", () => viewport().ratio);
winProp("screenX", () => 0);
winProp("screenY", () => 0);
winProp("screenLeft", () => 0);
winProp("screenTop", () => 0);
define("screen", {
    get width() { return 1280; },
    get height() { return 720; },
    get availWidth() { return 1280; },
    get availHeight() { return 720; },
    colorDepth: 24,
    pixelDepth: 24,
    orientation: { type: "landscape-primary", angle: 0, addEventListener() {}, removeEventListener() {}, lock: () => Promise.reject(new DOMException("", "NotSupportedError")) },
});
function scrollWindow(x, y, relative) {
    const top = typeof x === "object" && x !== null ? x.top : y;
    if (top === undefined || Number.isNaN(Number(top))) return;
    native.scrollTo((relative ? viewport().scrollY : 0) + Number(top));
}
define("scrollTo", (x, y) => scrollWindow(x, y, false));
define("scroll", (x, y) => scrollWindow(x, y, false));
define("scrollBy", (x, y) => scrollWindow(x, y, true));
document.elementFromPoint = function (x, y) {
    const all = layout();
    if (!all) return null;
    const docY = y + viewport().scrollY;
    let best = null;
    let bestArea = Infinity;
    for (const el of document.body.querySelectorAll("*")) {
        const b = all[serialOf(el)];
        if (!b || !(b[2] > 0 && b[3] > 0) || x < b[0] || x > b[0] + b[2] || docY < b[1] || docY > b[1] + b[3]) continue;
        if (b[2] * b[3] <= bestArea) {
            best = el;
            bestArea = b[2] * b[3];
        }
    }
    return best;
};
document.elementsFromPoint = (x, y) => {
    const el = document.elementFromPoint(x, y);
    const out = [];
    for (let e = el; e && e.nodeType === 1; e = e.parentNode) out.push(e);
    return out;
};
