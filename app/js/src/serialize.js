// The body as HTML for litehtml: the DOM with control state written into
// attributes (checked boxes, typed values, selected options), each element
// tagged data-ankinx-node="<serial>" (taps, controller focus and layout
// answers come back by serial) and, when it has click handlers,
// data-ankinx-click (the reviewer offers it to the controller).
import { native } from "./native.js";
import { document } from "./dom.js";
import { mediaPath } from "./media.js";
import { hasListener } from "./events.js";
import { checkedness, inputType, inputValue, inputValueDirty, selectedness, textareaDirty, textareaValue } from "./forms.js";
import { canvasImage } from "./canvas.js";

const serials = new WeakMap();
let nextSerial = 1;
let bySerial = new Map();

export function serialOf(node) {
    let serial = serials.get(node);
    if (!serial) serials.set(node, (serial = nextSerial++));
    return serial;
}
// The element with this serial in the last serialisation.
export function nodeBySerial(serial) { return bySerial.get(serial) || null; }

const VOID = new Set("area base basefont bgsound br col embed frame hr img input keygen link meta param source track wbr".split(" "));
const RAW = new Set("script style xmp iframe noembed noframes plaintext noscript".split(" "));
const CLICK = ["click", "mousedown", "mouseup", "pointerdown", "pointerup", "touchstart", "touchend"];
// The HTML parser lowercases tag names; SVG's are case-sensitive.
const SVG_TAGS = Object.fromEntries(("altGlyph altGlyphDef altGlyphItem animateColor animateMotion animateTransform " +
    "clipPath feBlend feColorMatrix feComponentTransfer feComposite feConvolveMatrix feDiffuseLighting " +
    "feDisplacementMap feDistantLight feDropShadow feFlood feFuncA feFuncB feFuncG feFuncR feGaussianBlur " +
    "feImage feMerge feMergeNode feMorphology feOffset fePointLight feSpecularLighting feSpotLight feTile " +
    "feTurbulence foreignObject glyphRef linearGradient radialGradient textPath").split(" ").map(n => [n.toLowerCase(), n]));

const TEXT_ESCAPES = { "&": "&amp;", "<": "&lt;", ">": "&gt;", " ": "&nbsp;" };
const ATTR_ESCAPES = { "&": "&amp;", "\"": "&quot;", " ": "&nbsp;" };
const escapeText = s => s.replace(/[&<> ]/g, c => TEXT_ESCAPES[c]);
const escapeAttribute = s => s.replace(/[&" ]/g, c => ATTR_ESCAPES[c]);

function attributesOf(el) {
    const attrs = [];
    for (const a of el.attributes) attrs.push([a.name, a.value]);
    const set = (name, value) => {
        const i = attrs.findIndex(([n]) => n === name);
        if (value === null) {
            if (i >= 0) attrs.splice(i, 1);
        } else if (i >= 0) attrs[i][1] = value;
        else attrs.push([name, value]);
    };
    if (el.localName === "input") {
        const type = inputType(el);
        if (type === "checkbox" || type === "radio") set("checked", checkedness(el) ? "" : null);
        else if (inputValueDirty(el)) set("value", inputValue(el));
    } else if (el.localName === "option") set("selected", selectedness(el) ? "" : null);
    return attrs;
}

function write(out, node, svg) {
    switch (node.nodeType) {
        case 3:
            out.push(node.parentNode && node.parentNode.nodeType === 1 && RAW.has(node.parentNode.localName) ? node.data : escapeText(node.data));
            return;
        case 4:
            out.push(`<![CDATA[${node.data}]]>`);
            return;
        case 8:
            out.push(`<!--${node.data}-->`);
            return;
        case 11:
            for (const child of node.childNodes) write(out, child, svg);
            return;
        case 1:
            break;
        default:
            return;
    }
    let tag = node.localName;
    if (tag === "template") return; // inert
    const inSvg = svg || tag === "svg";
    if (inSvg) tag = SVG_TAGS[tag] || tag;
    const serial = serialOf(node);
    bySerial.set(serial, node);
    if (tag === "canvas" && !inSvg) {
        // An image of what its context drew (canvas.js), the canvas's size.
        const image = canvasImage(node);
        out.push("<img");
        for (const [name, value] of attributesOf(node))
            if (name !== "width" && name !== "height" && name !== "src") out.push(" ", name, "=\"", escapeAttribute(value), "\"");
        out.push(" width=\"", String(image.width), "\" height=\"", String(image.height), "\" src=\"", escapeAttribute(image.src), "\"");
        out.push(" data-ankinx-node=\"", String(serial), "\"");
        if (hasListener(node, CLICK)) out.push(" data-ankinx-click=\"\"");
        out.push(">");
        return;
    }
    out.push("<", tag);
    for (const [name, value] of attributesOf(node)) out.push(" ", name, "=\"", escapeAttribute(value), "\"");
    out.push(" data-ankinx-node=\"", String(serial), "\"");
    if (tag !== "body" && tag !== "html" && hasListener(node, CLICK)) out.push(" data-ankinx-click=\"\"");
    out.push(">");
    if (VOID.has(tag)) return;
    if (tag === "textarea" && textareaDirty(node)) out.push(escapeText(textareaValue(node)));
    else for (const child of node.childNodes) write(out, child, inSvg && tag !== "foreignObject");
    out.push("</", tag, ">");
}

// The body's content, as the card view lays it out.
export function serializeBody() {
    bySerial = new Map();
    const out = [];
    for (const child of document.body.childNodes) write(out, child, false);
    return out.join("");
}

// CSS scripts added to <head>: <style> elements and stylesheet links to
// media files (the card's own CSS is the reviewer's).
export function extraCss() {
    const parts = [];
    for (const el of document.head.querySelectorAll("style, link")) {
        if (el.localName === "style") parts.push(el.textContent);
        else if ((el.getAttribute("rel") || "").toLowerCase().split(/\s+/).includes("stylesheet")) {
            const path = mediaPath(el.getAttribute("href") || "");
            const css = path && native.readMedia(path);
            if (css) parts.push(css);
        }
    }
    return parts.join("\n");
}
