// Form controls, activation behaviour and focus, as in the HTML Standard:
// checkedness, values and selectedness are state apart from the attributes
// (the attributes give the defaults); radio groups; what a click does on a
// checkbox, radio, label, submit or reset button, <summary> or link; and
// focus with document.activeElement. linkedom only reflects attributes.
import * as linkedom from "linkedom";
import { native } from "./native.js";
import { document } from "./dom.js";
import { activation, dispatchEvent, Event, FocusEvent, MouseEvent, SubmitEvent, ToggleEvent } from "./events.js";
import { reportError } from "./errors.js";
import { queueTask } from "./timers.js";
// css-select (linkedom's selector engine) matches :checked and :selected on
// attributes; they follow control state here, as in browsers. (A relative
// path: the package doesn't export these tables, and the same module is
// what linkedom uses.)
import { aliases } from "../node_modules/css-select/dist/pseudo-selectors/aliases.js";
import { pseudos } from "../node_modules/css-select/dist/pseudo-selectors/pseudos.js";

const states = new WeakMap();
let version = 0;
// Changes when control state changes (it is drawn: checked boxes, values).
export const formsVersion = () => version;

function st(element) {
    let s = states.get(element);
    if (!s) states.set(element, (s = {}));
    return s;
}

function prop(proto, name, get, set) {
    Object.defineProperty(proto, name, { get, set, configurable: true, enumerable: true });
}
function reflectString(proto, name, attribute = name.toLowerCase(), fallback = "") {
    prop(proto, name, function () { const v = this.getAttribute(attribute); return v === null ? fallback : v; },
        function (v) { this.setAttribute(attribute, String(v)); });
}
function reflectBoolean(proto, name, attribute = name.toLowerCase()) {
    prop(proto, name, function () { return this.hasAttribute(attribute); },
        function (v) { this.toggleAttribute(attribute, !!v); });
}
function reflectNumber(proto, name, attribute, fallback) {
    prop(proto, name, function () { const v = parseInt(this.getAttribute(attribute), 10); return Number.isNaN(v) ? fallback : v; },
        function (v) { this.setAttribute(attribute, String(Math.trunc(Number(v)))); });
}

const {
    HTMLElement, HTMLInputElement, HTMLTextAreaElement, HTMLSelectElement, HTMLOptionElement, HTMLButtonElement,
    HTMLFormElement, HTMLLabelElement, HTMLDetailsElement, HTMLAnchorElement, HTMLFieldSetElement, HTMLOutputElement,
} = linkedom;

// ------------------------------------------------------------- form owner --

const LISTED = ["button", "fieldset", "input", "object", "output", "select", "textarea"];

export function formOwner(element) {
    const id = element.getAttribute("form");
    if (id !== null) {
        const form = element.isConnected ? document.getElementById(id) : null;
        return form && form.localName === "form" ? form : null;
    }
    return element.parentNode ? element.parentNode.closest?.("form") || null : null;
}

function isDisabled(element) {
    if (!["button", "input", "select", "textarea", "option", "optgroup", "fieldset"].includes(element.localName)) return false;
    if (element.hasAttribute("disabled")) return true;
    for (let p = element.parentNode; p && p.nodeType === 1; p = p.parentNode)
        if (p.localName === "fieldset" && p.hasAttribute("disabled")) {
            const legend = [...p.children].find(c => c.localName === "legend");
            if (!legend || !legend.contains(element)) return true;
        }
    return false;
}

for (const C of [HTMLInputElement, HTMLTextAreaElement, HTMLSelectElement, HTMLButtonElement, HTMLFieldSetElement, HTMLOutputElement]) {
    prop(C.prototype, "form", function () { return formOwner(this); });
    reflectString(C.prototype, "name");
    reflectBoolean(C.prototype, "disabled");
    prop(C.prototype, "labels", function () {
        return [...document.querySelectorAll("label")].filter(label => label.control === this);
    });
    prop(C.prototype, "validity", () => ({
        valid: true, valueMissing: false, typeMismatch: false, patternMismatch: false, tooLong: false, tooShort: false,
        rangeUnderflow: false, rangeOverflow: false, stepMismatch: false, badInput: false, customError: false,
    }));
    prop(C.prototype, "validationMessage", () => "");
    prop(C.prototype, "willValidate", function () { return !isDisabled(this); });
    C.prototype.checkValidity = () => true;
    C.prototype.reportValidity = () => true;
    C.prototype.setCustomValidity = () => {};
}

// ----------------------------------------------------------------- inputs --

const INPUT_TYPES = new Set(("hidden text search tel url email password date month week time datetime-local " +
    "number range color checkbox radio file submit image reset button").split(" "));
export function inputType(input) {
    const t = (input.getAttribute("type") || "").toLowerCase();
    return INPUT_TYPES.has(t) ? t : "text";
}

export function checkedness(input) {
    const s = st(input);
    return s.dirtyChecked ? s.checked : input.hasAttribute("checked");
}
function setCheckedness(input, value) {
    const s = st(input);
    s.dirtyChecked = true;
    s.checked = !!value;
    version++;
    if (value && inputType(input) === "radio")
        for (const other of radioGroup(input))
            if (checkedness(other)) {
                st(other).dirtyChecked = true;
                st(other).checked = false;
            }
}

function radioGroup(input) {
    const name = input.getAttribute("name");
    if (!name) return [];
    let root = input;
    while (root.parentNode) root = root.parentNode;
    const form = formOwner(input);
    const candidates = root.querySelectorAll ? [...root.querySelectorAll("input")] : [];
    return candidates.filter(r => r !== input && inputType(r) === "radio" && r.getAttribute("name") === name && formOwner(r) === form);
}

function valueMode(type) {
    if (["hidden", "submit", "image", "reset", "button"].includes(type)) return "default";
    if (type === "checkbox" || type === "radio") return "default/on";
    if (type === "file") return "filename";
    return "value";
}

export function inputValue(input) {
    const s = st(input);
    switch (valueMode(inputType(input))) {
        case "value": {
            const v = s.dirtyValue ? s.value : input.getAttribute("value") || "";
            return ["text", "search", "tel", "url", "email", "password"].includes(inputType(input)) ? v.replace(/[\r\n]/g, "") : v;
        }
        case "default/on": return input.getAttribute("value") ?? "on";
        case "filename": return "";
        default: return input.getAttribute("value") || "";
    }
}
export function inputValueDirty(input) { return !!st(input).dirtyValue && valueMode(inputType(input)) === "value"; }

const IP = HTMLInputElement.prototype;
prop(IP, "type", function () { return inputType(this); }, function (v) { this.setAttribute("type", String(v)); });
prop(IP, "checked", function () { return checkedness(this); }, function (v) { setCheckedness(this, v); });
reflectBoolean(IP, "defaultChecked", "checked");
prop(IP, "indeterminate", function () { return !!st(this).indeterminate; }, function (v) { st(this).indeterminate = !!v; });
prop(IP, "value", function () { return inputValue(this); }, function (v) {
    const mode = valueMode(inputType(this));
    if (mode === "value") {
        const s = st(this);
        s.dirtyValue = true;
        s.value = v == null ? "" : String(v);
        version++;
    } else if (mode !== "filename") this.setAttribute("value", String(v));
});
reflectString(IP, "defaultValue", "value");
prop(IP, "valueAsNumber", function () {
    const type = inputType(this);
    if (type === "number" || type === "range") return this.value === "" ? NaN : Number(this.value);
    if (["date", "datetime-local", "month", "week", "time"].includes(type)) return Date.parse(this.value);
    return NaN;
}, function (v) { this.value = Number.isNaN(v) ? "" : String(v); });
for (const name of ["placeholder", "min", "max", "step", "pattern", "accept", "alt", "src", "autocomplete", "inputMode", "dirName"])
    reflectString(IP, name);
for (const name of ["readOnly", "required", "multiple", "autofocus"]) reflectBoolean(IP, name);
reflectNumber(IP, "size", "size", 20);
reflectNumber(IP, "maxLength", "maxlength", -1);
reflectNumber(IP, "minLength", "minlength", -1);
prop(IP, "selectionStart", function () { return this.value.length; });
prop(IP, "selectionEnd", function () { return this.value.length; });
IP.select = function () {};
IP.setSelectionRange = function () {};
IP.setRangeText = function () {};
IP.showPicker = function () {};
IP.stepUp = function (n = 1) { const step = Number(this.step) || 1; this.value = String((Number(this.value) || 0) + step * n); };
IP.stepDown = function (n = 1) { this.stepUp(-n); };

// -------------------------------------------------------------- textareas --

const TP = HTMLTextAreaElement.prototype;
export function textareaValue(textarea) {
    const s = st(textarea);
    return s.dirtyValue ? s.value : textarea.textContent;
}
export function textareaDirty(textarea) { return !!st(textarea).dirtyValue; }
prop(TP, "value", function () { return textareaValue(this); }, function (v) {
    const s = st(this);
    s.dirtyValue = true;
    s.value = v == null ? "" : String(v);
    version++;
});
prop(TP, "defaultValue", function () { return this.textContent; }, function (v) { this.textContent = String(v); });
prop(TP, "type", () => "textarea");
prop(TP, "textLength", function () { return this.value.length; });
for (const name of ["placeholder", "wrap", "autocomplete"]) reflectString(TP, name);
for (const name of ["readOnly", "required", "autofocus"]) reflectBoolean(TP, name);
reflectNumber(TP, "rows", "rows", 2);
reflectNumber(TP, "cols", "cols", 20);
reflectNumber(TP, "maxLength", "maxlength", -1);
TP.select = function () {};
TP.setSelectionRange = function () {};

// --------------------------------------------------------- selects, options --

export function selectedness(option) {
    const s = st(option);
    return s.dirtySelected ? s.selected : option.hasAttribute("selected");
}
function optionsOf(select) { return [...select.querySelectorAll("option")]; }
function selectOf(option) {
    const p = option.parentNode;
    if (p && p.localName === "select") return p;
    if (p && p.localName === "optgroup" && p.parentNode && p.parentNode.localName === "select") return p.parentNode;
    return null;
}
function setSelectedness(option, value) {
    const s = st(option);
    s.dirtySelected = true;
    s.selected = !!value;
    version++;
    const select = selectOf(option);
    if (value && select && !select.multiple)
        for (const other of optionsOf(select))
            if (other !== option) {
                st(other).dirtySelected = true;
                st(other).selected = false;
            }
}
function selectedIndex(select) {
    const options = optionsOf(select);
    let index = -1;
    options.forEach((o, i) => {
        if (selectedness(o) && (index < 0 || !select.multiple)) index = i;
    });
    // A single-choice drop-down shows its first enabled option.
    if (index < 0 && !select.multiple && !(Number(select.getAttribute("size")) > 1))
        index = options.findIndex(o => !o.hasAttribute("disabled"));
    return index;
}

const SP = HTMLSelectElement.prototype;
prop(SP, "options", function () {
    const list = optionsOf(this);
    list.item = i => list[i] || null;
    list.namedItem = name => list.find(o => o.id === name || o.getAttribute("name") === name) || null;
    return list;
});
prop(SP, "length", function () { return optionsOf(this).length; });
prop(SP, "type", function () { return this.multiple ? "select-multiple" : "select-one"; });
reflectBoolean(SP, "multiple");
reflectBoolean(SP, "required");
reflectNumber(SP, "size", "size", 0);
prop(SP, "selectedIndex", function () { return selectedIndex(this); }, function (i) {
    optionsOf(this).forEach((o, n) => setSelectedness(o, n === Number(i)));
});
prop(SP, "selectedOptions", function () {
    const options = optionsOf(this);
    const index = selectedIndex(this);
    return options.filter((o, i) => selectedness(o) || i === index);
});
prop(SP, "value", function () {
    const o = optionsOf(this)[selectedIndex(this)];
    return o ? o.value : "";
}, function (v) {
    const options = optionsOf(this);
    const match = options.find(o => o.value === String(v));
    options.forEach(o => { st(o).dirtySelected = true; st(o).selected = o === match; });
    version++;
});
SP.item = function (i) { return optionsOf(this)[i] || null; };
SP.namedItem = function (name) { return this.options.namedItem(name); };
SP.add = function (option, before = null) {
    const ref = typeof before === "number" ? optionsOf(this)[before] || null : before;
    (ref ? ref.parentNode : this).insertBefore(option, ref);
};
const removeNode = linkedom.Element.prototype.remove;
SP.remove = function (index) {
    if (arguments.length === 0) return removeNode.call(this);
    const o = optionsOf(this)[index];
    if (o) o.remove();
};

const OP = HTMLOptionElement.prototype;
const collapse = s => s.replace(/[ \t\n\f\r]+/g, " ").trim();
prop(OP, "selected", function () { return selectedness(this); }, function (v) { setSelectedness(this, v); });
reflectBoolean(OP, "defaultSelected", "selected");
reflectBoolean(OP, "disabled");
prop(OP, "value", function () { const v = this.getAttribute("value"); return v === null ? collapse(this.textContent) : v; },
    function (v) { this.setAttribute("value", String(v)); });
prop(OP, "text", function () { return collapse(this.textContent); }, function (v) { this.textContent = String(v); });
prop(OP, "label", function () { const v = this.getAttribute("label"); return v === null ? this.text : v; },
    function (v) { this.setAttribute("label", String(v)); });
prop(OP, "index", function () { const s = selectOf(this); return s ? optionsOf(s).indexOf(this) : 0; });
prop(OP, "form", function () { const s = selectOf(this); return s ? formOwner(s) : null; });
globalThis.Option = function Option(text = "", value, defaultSelected = false, selected = false) {
    const option = document.createElement("option");
    if (text !== "") option.textContent = String(text);
    if (value !== undefined) option.setAttribute("value", String(value));
    if (defaultSelected) option.setAttribute("selected", "");
    if (selected) setSelectedness(option, true);
    return option;
};
globalThis.Option.prototype = OP;

function optionShownSelected(option) {
    const select = selectOf(option);
    return select ? select.selectedOptions.includes(option) : selectedness(option);
}
delete aliases.checked;
delete aliases.selected;
pseudos.checked = element =>
    (element.localName === "input" && ["checkbox", "radio"].includes(inputType(element)) && checkedness(element)) ||
    (element.localName === "option" && optionShownSelected(element));
pseudos.selected = element => element.localName === "option" && optionShownSelected(element);
pseudos.indeterminate = element => element.localName === "input" && !!st(element).indeterminate;

// ---------------------------------------------------------------- buttons --

const BP = HTMLButtonElement.prototype;
function buttonType(button) {
    const t = (button.getAttribute("type") || "").toLowerCase();
    return t === "reset" || t === "button" ? t : "submit";
}
prop(BP, "type", function () { return buttonType(this); }, function (v) { this.setAttribute("type", String(v)); });
reflectString(BP, "value");
reflectBoolean(BP, "autofocus");

// ------------------------------------------------------------------ forms --

const FP = HTMLFormElement.prototype;
function listedElements(form) {
    return [...document.querySelectorAll(LISTED.join(","))].concat([...form.querySelectorAll(LISTED.join(","))])
        .filter((el, i, all) => all.indexOf(el) === i && formOwner(el) === form && !(el.localName === "input" && inputType(el) === "image"));
}
prop(FP, "elements", function () {
    const list = listedElements(this);
    list.item = i => list[i] || null;
    list.namedItem = name => list.find(el => el.id === name || el.getAttribute("name") === name) || null;
    for (const el of list) {
        for (const key of [el.id, el.getAttribute("name")])
            if (key && !(key in list)) list[key] = el;
    }
    return list;
});
prop(FP, "length", function () { return listedElements(this).length; });
for (const name of ["action", "method", "target", "enctype", "acceptCharset", "autocomplete"]) reflectString(FP, name);
reflectBoolean(FP, "noValidate");
FP.checkValidity = () => true;
FP.reportValidity = () => true;
// Forms can't go anywhere from a card: submitting fires "submit" (handlers
// usually check answers there) and stops.
FP.submit = function () { native.log(0, "form submitted (no navigation in cards)"); };
FP.requestSubmit = function (submitter = null) {
    if (submitter && !(submitter.form === this)) throw new DOMException("The specified element is not a submit button of this form.", "NotFoundError");
    if (dispatchEvent(this, new SubmitEvent("submit", { bubbles: true, cancelable: true, submitter }))) this.submit();
};
FP.reset = function () {
    if (!dispatchEvent(this, new Event("reset", { bubbles: true, cancelable: true }))) return;
    for (const el of listedElements(this)) {
        const s = st(el);
        s.dirtyValue = s.dirtyChecked = false;
        s.indeterminate = false;
        if (el.localName === "select") for (const o of optionsOf(el)) st(o).dirtySelected = false;
    }
    version++;
};
Object.defineProperty(FP, Symbol.iterator, { value: function () { return listedElements(this)[Symbol.iterator](); }, configurable: true });

// ----------------------------------------------------------------- labels --

const LABELABLE = ["button", "input", "meter", "output", "progress", "select", "textarea"];
function labelable(el) { return el && LABELABLE.includes(el.localName) && !(el.localName === "input" && inputType(el) === "hidden"); }
const LP = HTMLLabelElement.prototype;
prop(LP, "control", function () {
    const id = this.getAttribute("for");
    if (id !== null) {
        const el = this.isConnected ? document.getElementById(id) : null;
        return labelable(el) ? el : null;
    }
    return [...this.querySelectorAll(LABELABLE.join(","))].find(labelable) || null;
});
reflectString(LP, "htmlFor", "for");
prop(LP, "form", function () { const c = this.control; return c ? c.form : null; });

// ---------------------------------------------------------------- details --

const DP = HTMLDetailsElement.prototype;
prop(DP, "open", function () { return this.hasAttribute("open"); }, function (v) {
    const was = this.hasAttribute("open");
    this.toggleAttribute("open", !!v);
    if (was !== !!v)
        queueTask(() => dispatchEvent(this, new ToggleEvent("toggle", {
            oldState: was ? "open" : "closed", newState: v ? "open" : "closed",
        })));
});
function summaryOfDetails(summary) {
    const details = summary.parentNode;
    return details && details.localName === "details" && [...details.children].find(c => c.localName === "summary") === summary;
}

// ------------------------------------------------------------------ links --

function followLink(anchor) {
    const href = anchor.getAttribute("href");
    if (href === null) return;
    if (/^javascript:/i.test(href)) {
        try {
            native.evalScript(decodeURIComponent(href.slice(11)), "javascript: link");
        } catch (e) {
            reportError(e);
        }
        return;
    }
    if (href.startsWith("#")) {
        const id = decodeURIComponent(href.slice(1));
        const target = id && (document.getElementById(id) || document.querySelector(`a[name="${CSS.escape(id)}"]`));
        if (target) target.scrollIntoView();
        else if (id === "" || id.toLowerCase() === "top") globalThis.scrollTo(0, 0);
        return;
    }
    // playsound:, anki-hint: and the rest: what a tap on the link does.
    native.command("link:" + href);
}

// ------------------------------------------------------ activation, clicks --

activation.has = el => {
    switch (el.localName) {
        case "a":
        case "area": return el.hasAttribute("href");
        case "input": return ["checkbox", "radio", "submit", "image", "reset"].includes(inputType(el));
        case "button":
        case "label": return true;
        case "summary": return summaryOfDetails(el);
        default: return false;
    }
};

// Before the handlers run: a checkbox or radio changes at once (handlers see
// the new state), and changes back if one cancels the click.
activation.preActivate = el => {
    if (el.localName !== "input" || isDisabled(el)) return null;
    const type = inputType(el);
    if (type === "checkbox") {
        const s = st(el);
        const before = { dirtyChecked: s.dirtyChecked, checked: s.checked, indeterminate: s.indeterminate };
        setCheckedness(el, !checkedness(el));
        s.indeterminate = false;
        return () => { Object.assign(s, before); version++; };
    }
    if (type === "radio") {
        const was = checkedness(el);
        const previous = radioGroup(el).find(checkedness) || null;
        setCheckedness(el, true);
        return () => {
            setCheckedness(el, was);
            if (previous) setCheckedness(previous, true);
        };
    }
    return null;
};

function interactive(el) {
    return ["a", "button", "details", "embed", "iframe", "label", "select", "textarea"].includes(el.localName) ||
        (el.localName === "input" && inputType(el) !== "hidden");
}

activation.activate = (el, event) => {
    if (isDisabled(el)) return;
    switch (el.localName) {
        case "input": {
            const type = inputType(el);
            const form = formOwner(el);
            if (type === "checkbox" || type === "radio") {
                if (!el.isConnected) return;
                dispatchEvent(el, new Event("input", { bubbles: true, composed: true }));
                dispatchEvent(el, new Event("change", { bubbles: true }));
            } else if ((type === "submit" || type === "image") && form) form.requestSubmit(el);
            else if (type === "reset" && form) form.reset();
            return;
        }
        case "button": {
            const form = formOwner(el);
            if (!form) return;
            if (buttonType(el) === "submit") form.requestSubmit(el);
            else if (buttonType(el) === "reset") form.reset();
            return;
        }
        case "label": {
            const control = el.control;
            if (!control) return;
            // Not when the click was on the control itself, or on another
            // interactive element inside the label.
            for (let t = event.target; t && t !== el; t = t.parentNode)
                if (t === control || (t.nodeType === 1 && interactive(t))) return;
            control.focus();
            control.click();
            return;
        }
        case "summary": {
            const details = el.parentNode;
            details.open = !details.open;
            return;
        }
        default:
            followLink(el);
    }
};

const clicking = new WeakSet();
HTMLElement.prototype.click = function () {
    if (isDisabled(this) || clicking.has(this)) return;
    clicking.add(this);
    try {
        dispatchEvent(this, new MouseEvent("click", { bubbles: true, cancelable: true, composed: true, view: globalThis, detail: 1 }));
    } finally {
        clicking.delete(this);
    }
};

// ------------------------------------------------------------------ focus --

let focused = null;

function focusable(el) {
    if (!el || el.nodeType !== 1 || !el.isConnected || isDisabled(el)) return false;
    switch (el.localName) {
        case "input": return inputType(el) !== "hidden";
        case "button":
        case "select":
        case "textarea":
        case "iframe":
        case "summary": return true;
        case "a":
        case "area": return el.hasAttribute("href");
        case "audio":
        case "video": return el.hasAttribute("controls");
    }
    return el.hasAttribute("tabindex") || el.hasAttribute("contenteditable");
}

function setFocus(el) {
    if (el === focused) return;
    const old = focused;
    focused = el;
    if (old && old.isConnected) {
        dispatchEvent(old, new FocusEvent("blur", { relatedTarget: el, view: globalThis }));
        dispatchEvent(old, new FocusEvent("focusout", { bubbles: true, composed: true, relatedTarget: el, view: globalThis }));
    }
    if (el) {
        dispatchEvent(el, new FocusEvent("focus", { relatedTarget: old, view: globalThis }));
        dispatchEvent(el, new FocusEvent("focusin", { bubbles: true, composed: true, relatedTarget: old, view: globalThis }));
    }
}

// A press on `target` focuses it or its nearest focusable ancestor, or
// takes focus away (as a mousedown does).
export function focusFromPointer(target) {
    for (let el = target; el && el.nodeType === 1; el = el.parentNode)
        if (focusable(el)) {
            setFocus(el);
            return;
        }
    setFocus(null);
}

for (const proto of [HTMLElement.prototype, linkedom.SVGElement.prototype]) {
    proto.focus = function () { if (focusable(this)) setFocus(this); };
    proto.blur = function () { if (focused === this) setFocus(null); };
}
prop(HTMLElement.prototype, "tabIndex", function () {
    const v = parseInt(this.getAttribute("tabindex"), 10);
    if (!Number.isNaN(v)) return v;
    return focusable(this) && !this.hasAttribute("tabindex") ? 0 : -1;
}, function (v) { this.setAttribute("tabindex", String(Math.trunc(Number(v)))); });
Object.defineProperty(document, "activeElement", {
    get: () => (focused && focused.isConnected ? focused : document.body),
    configurable: true,
});
document.hasFocus = () => true;

// <input autofocus> and the like, when a side is shown.
export function autofocus(root) {
    const el = root.querySelector("[autofocus]");
    if (el) el.focus();
}

// Anchors: href and the URL parts, resolved against the page.
for (const C of [HTMLAnchorElement, linkedom.HTMLAreaElement]) {
    if (!C) continue;
    prop(C.prototype, "href", function () {
        const v = this.getAttribute("href");
        if (v === null) return "";
        try { return new URL(v, globalThis.location.href).href; } catch (e) { return v; }
    }, function (v) { this.setAttribute("href", String(v)); });
    for (const part of ["protocol", "host", "hostname", "port", "pathname", "search", "hash", "origin"])
        prop(C.prototype, part, function () {
            try { return new URL(this.href)[part]; } catch (e) { return ""; }
        });
    reflectString(C.prototype, "target");
    reflectString(C.prototype, "rel");
    reflectString(C.prototype, "download");
}
