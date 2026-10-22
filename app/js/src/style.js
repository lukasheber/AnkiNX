// element.style as CSSOM's CSSStyleDeclaration: camelCase, vendor-prefixed
// and custom property names, priorities, cssText, and "" removing a
// property. Every write goes through the style attribute: linkedom's own
// declaration writes the attribute's value behind the DOM's back, which
// mutation observers (and so the renderer) never see.
import * as linkedom from "linkedom";

export function kebab(name) {
    name = String(name);
    if (name.startsWith("--")) return name;
    if (name.includes("-")) return name.toLowerCase();
    if (name === "cssFloat") return "float";
    return name.replace(/^(webkit|moz|ms|o)(?=[A-Z])/, "-$1").replace(/[A-Z]/g, c => "-" + c.toLowerCase());
}

// "a: b; c: url(x;y) !important" -> [[name, value, important]], keeping
// semicolons inside quotes and brackets (data: URLs).
export function parseDeclarations(text) {
    const out = [];
    let start = 0;
    let depth = 0;
    let quote = "";
    const push = part => {
        const colon = part.indexOf(":");
        if (colon <= 0) return;
        const name = part.slice(0, colon).trim();
        let value = part.slice(colon + 1).trim();
        const important = /!\s*important\s*$/i.test(value);
        if (important) value = value.replace(/!\s*important\s*$/i, "").trim();
        if (name && value) out.push([name.startsWith("--") ? name : name.toLowerCase(), value, important]);
    };
    text = String(text || "");
    for (let i = 0; i < text.length; i++) {
        const c = text[i];
        if (quote) {
            if (c === "\\") i++;
            else if (c === quote) quote = "";
        } else if (c === "\"" || c === "'") quote = c;
        else if (c === "(") depth++;
        else if (c === ")") depth = Math.max(0, depth - 1);
        else if (c === ";" && depth === 0) {
            push(text.slice(start, i));
            start = i + 1;
        }
    }
    push(text.slice(start));
    return out;
}

function serialize(declarations) {
    return declarations.map(([name, value, important]) => `${name}: ${value}${important ? " !important" : ""};`).join(" ");
}

const ELEMENT = Symbol("element");

const declarationsOf = style => parseDeclarations(style[ELEMENT].getAttribute("style"));
function store(style, declarations) {
    const text = serialize(declarations);
    const element = style[ELEMENT];
    if (text) element.setAttribute("style", text);
    else if (element.hasAttribute("style")) element.setAttribute("style", "");
}

// (Methods run with `this` as the proxy below, so state is symbol-keyed,
// not in private fields.)
export class CSSStyleDeclaration {
    constructor(element) {
        this[ELEMENT] = element;
    }
    get cssText() { return serialize(declarationsOf(this)); }
    set cssText(value) { store(this, parseDeclarations(value)); }
    get length() { return declarationsOf(this).length; }
    get parentRule() { return null; }
    get cssFloat() { return this.getPropertyValue("float"); }
    set cssFloat(value) { this.setProperty("float", value); }
    item(index) {
        const d = declarationsOf(this)[index];
        return d ? d[0] : "";
    }
    getPropertyValue(name) {
        const d = declarationsOf(this).find(([n]) => n === kebab(name));
        return d ? d[1] : "";
    }
    getPropertyPriority(name) {
        const d = declarationsOf(this).find(([n]) => n === kebab(name));
        return d && d[2] ? "important" : "";
    }
    setProperty(name, value, priority = "") {
        name = kebab(name);
        value = value == null ? "" : String(value).trim();
        const old = declarationsOf(this);
        const declarations = old.filter(([n]) => n !== name);
        if (value !== "") {
            const at = old.findIndex(([n]) => n === name);
            const entry = [name, value, String(priority).toLowerCase() === "important"];
            if (at >= 0) declarations.splice(at, 0, entry);
            else declarations.push(entry);
        }
        store(this, declarations);
    }
    removeProperty(name) {
        const old = this.getPropertyValue(name);
        this.setProperty(name, "");
        return old;
    }
    *[Symbol.iterator]() {
        for (const [name] of declarationsOf(this)) yield name;
    }
}

// Property access by name: style.backgroundColor, style["font-size"], style[0].
const proxyHandler = {
    get(target, key) {
        if (typeof key !== "string" || key in target) return Reflect.get(target, key, target);
        if (/^\d+$/.test(key)) return target.item(Number(key));
        return target.getPropertyValue(key);
    },
    set(target, key, value) {
        if (typeof key !== "string" || key in target) return Reflect.set(target, key, value, target);
        target.setProperty(key, value);
        return true;
    },
    has(target, key) { return typeof key === "string" || key in target; },
};

const styles = new WeakMap();
export function styleOf(element) {
    let style = styles.get(element);
    if (!style) styles.set(element, (style = new Proxy(new CSSStyleDeclaration(element), proxyHandler)));
    return style;
}

for (const proto of [linkedom.Element.prototype, linkedom.HTMLElement.prototype, linkedom.SVGElement.prototype])
    Object.defineProperty(proto, "style", {
        get() { return styleOf(this); },
        set(value) { styleOf(this).cssText = value; },
        configurable: true,
    });
