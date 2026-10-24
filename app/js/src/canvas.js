// <canvas> with a 2D context that records what is drawn as SVG instead of
// pixels: the serialiser writes each canvas as an <img> of its SVG, which the
// card view draws like any SVG image (lunasvg). Covers paths (lines, arcs,
// ellipses, curves, rectangles, rounded rectangles, Path2D), fill and stroke
// with colours and linear/radial gradients, line styles and dashes,
// transforms, save/restore, globalAlpha, clearRect, fillText/strokeText and
// drawImage (media images and other canvases). Not covered: reading pixels
// back (getImageData returns a blank image), patterns, shadows, filters,
// compositing modes and clip().
import * as linkedom from "linkedom";
import { native } from "./native.js";
import { define } from "./dom.js";
import { mediaPath } from "./media.js";

const TAU = Math.PI * 2;
const num = n => String(Math.round(n * 100) / 100);
const escapeXml = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;" })[c]);

// ------------------------------------------------------------ matrices --

// [a, b, c, d, e, f]: x' = a x + c y + e, y' = b x + d y + f.
const IDENTITY = [1, 0, 0, 1, 0, 0];
const multiply = (m, n) => [
    m[0] * n[0] + m[2] * n[1], m[1] * n[0] + m[3] * n[1],
    m[0] * n[2] + m[2] * n[3], m[1] * n[2] + m[3] * n[3],
    m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5],
];
const apply = (m, x, y) => [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]];
const finite = (...values) => values.every(Number.isFinite);
const matrixAttr = m => `matrix(${m.map(num).join(" ")})`;

// ------------------------------------------------------------- colours --

// A CSS colour as SVG paint and opacity (lunasvg reads names and #hex; rgb()
// with alpha and hsl() are converted here).
function paintOf(value) {
    const s = String(value).trim().toLowerCase();
    if (s === "transparent") return { color: "#000", alpha: 0 };
    let m = /^#([0-9a-f]{4}|[0-9a-f]{8})$/.exec(s);
    if (m) {
        const hex = m[1].length === 4 ? [...m[1]].map(c => c + c).join("") : m[1];
        return { color: "#" + hex.slice(0, 6), alpha: parseInt(hex.slice(6), 16) / 255 };
    }
    m = /^(rgba?|hsla?)\((.*)\)$/.exec(s);
    if (m) {
        const parts = m[2].split(/[\s,/]+/).filter(Boolean);
        const alpha = parts.length > 3 ? (parts[3].endsWith("%") ? parseFloat(parts[3]) / 100 : parseFloat(parts[3])) : 1;
        let rgb;
        if (m[1].startsWith("rgb")) {
            rgb = parts.slice(0, 3).map(p => p.endsWith("%") ? parseFloat(p) * 2.55 : parseFloat(p));
        } else {
            const h = ((parseFloat(parts[0]) % 360) + 360) % 360 / 360;
            const sat = parseFloat(parts[1]) / 100, l = parseFloat(parts[2]) / 100;
            const q = l < 0.5 ? l * (1 + sat) : l + sat - l * sat, p = 2 * l - q;
            const hue = t => {
                t = (t + 1) % 1;
                if (t < 1 / 6) return p + (q - p) * 6 * t;
                if (t < 1 / 2) return q;
                if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
                return p;
            };
            rgb = [hue(h + 1 / 3), hue(h), hue(h - 1 / 3)].map(v => v * 255);
        }
        if (!rgb.every(Number.isFinite)) return null;
        const c = rgb.map(v => Math.max(0, Math.min(255, Math.round(v))));
        return { color: `rgb(${c.join(",")})`, alpha: Number.isFinite(alpha) ? Math.max(0, Math.min(1, alpha)) : 1 };
    }
    return /^[a-z]+$|^#[0-9a-f]{3}$|^#[0-9a-f]{6}$/.test(s) ? { color: s, alpha: 1 } : null;
}

export class CanvasGradient {
    constructor(kind, coords) {
        this._kind = kind;
        this._coords = coords;
        this._stops = [];
    }
    addColorStop(offset, color) {
        offset = +offset;
        if (!(offset >= 0 && offset <= 1)) throw new DOMException("The offset is outside the range [0, 1].", "IndexSizeError");
        const paint = paintOf(color);
        if (!paint) throw new SyntaxError(`'${color}' is not a valid colour.`);
        this._stops.push([offset, paint]);
    }
}

export class CanvasPattern {
    setTransform() {}
}

// -------------------------------------------------------------- images --

// Width and height from an image file's header.
function headerSize(bytes) {
    const b = new Uint8Array(bytes);
    const u16 = i => (b[i] << 8) | b[i + 1], u32 = i => ((b[i] << 24) >>> 0) + (b[i + 1] << 16) + (b[i + 2] << 8) + b[i + 3];
    const le16 = i => b[i] | (b[i + 1] << 8);
    if (b[0] === 0x89 && b[1] === 0x50) return [u32(16), u32(20)]; // PNG
    if (b[0] === 0x47 && b[1] === 0x49) return [le16(6), le16(8)]; // GIF
    if (b[0] === 0x42 && b[1] === 0x4d) return [le16(18) | (le16(20) << 16), Math.abs((le16(22) | (le16(24) << 16)) << 0)]; // BMP
    if (b[0] === 0xff && b[1] === 0xd8) { // JPEG: the first frame header
        for (let i = 2; i + 9 < b.length;) {
            if (b[i] !== 0xff) { i++; continue; }
            const marker = b[i + 1];
            if (marker >= 0xc0 && marker <= 0xcf && marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc) return [u16(i + 7), u16(i + 5)];
            i += 2 + u16(i + 2);
        }
        return null;
    }
    if (b[8] === 0x57 && b[9] === 0x45 && b[10] === 0x42 && b[11] === 0x50) { // WebP
        const chunk = String.fromCharCode(b[12], b[13], b[14], b[15]);
        if (chunk === "VP8 ") return [le16(26) & 0x3fff, le16(28) & 0x3fff];
        if (chunk === "VP8L") return [1 + (((b[22] & 0x3f) << 8) | b[21]), 1 + (((b[24] & 0x0f) << 10) | (b[23] << 2) | ((b[22] & 0xc0) >> 6))];
        if (chunk === "VP8X") return [1 + (b[24] | (b[25] << 8) | (b[26] << 16)), 1 + (b[27] | (b[28] << 8) | (b[29] << 16))];
        return null;
    }
    const text = String.fromCharCode(...b.subarray(0, Math.min(b.length, 2048)));
    const svg = /<svg\b[^>]*>/i.exec(text);
    if (svg) {
        const attr = name => { const m = new RegExp(`\\b${name}\\s*=\\s*["']([\\d.]+)(px)?["']`, "i").exec(svg[0]); return m ? +m[1] : 0; };
        const box = /viewBox\s*=\s*["'][\d.\s-]+?\s([\d.]+)\s+([\d.]+)["']/i.exec(svg[0]);
        return [attr("width") || (box ? +box[1] : 300), attr("height") || (box ? +box[2] : 150)];
    }
    return null;
}

function base64(bytes) {
    const b = new Uint8Array(bytes);
    let s = "";
    for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode(...b.subarray(i, i + 0x8000));
    return btoa(s);
}

const MIME = { png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", gif: "image/gif", webp: "image/webp", bmp: "image/bmp", svg: "image/svg+xml" };
const imageCache = new Map();
// A media image as { uri, width, height } (a data: URI, which lunasvg reads), or null.
function mediaImage(src) {
    if (imageCache.has(src)) return imageCache.get(src);
    let info = null;
    const path = mediaPath(src);
    const bytes = path && native.readMediaBytes(path);
    if (bytes) {
        const size = headerSize(bytes);
        const ext = (/\.([a-z0-9]+)$/i.exec(path) || [])[1];
        if (size) info = { uri: `data:${MIME[(ext || "").toLowerCase()] || "image/png"};base64,${base64(bytes)}`, width: size[0], height: size[1] };
    }
    imageCache.set(src, info);
    return info;
}

// <img>: natural size from the file, for scripts and drawImage.
const ImageProto = linkedom.HTMLImageElement.prototype;
for (const [name, index] of [["naturalWidth", 0], ["naturalHeight", 1]])
    Object.defineProperty(ImageProto, name, {
        configurable: true,
        get() {
            const src = this.getAttribute("src");
            const info = src && mediaImage(src);
            return info ? info[index ? "height" : "width"] : 0;
        },
    });
Object.defineProperty(ImageProto, "complete", { configurable: true, get() { return true; } });

// --------------------------------------------------------------- paths --

// Path segments in device space, as SVG path data. `transform` maps the user
// space points of each call (the context's current transform, or the one in
// force when a Path2D is drawn).
class PathData {
    constructor() {
        this.d = [];
        this.start = null; // subpath start (user space)
        this.pen = null;   // current point (user space)
    }
    point(m, x, y) { return apply(m, x, y).map(num).join(" "); }
    moveTo(m, x, y) {
        if (!finite(x, y)) return;
        this.d.push("M" + this.point(m, x, y));
        this.start = this.pen = [x, y];
    }
    lineTo(m, x, y) {
        if (!finite(x, y)) return;
        if (!this.pen) return this.moveTo(m, x, y);
        this.d.push("L" + this.point(m, x, y));
        this.pen = [x, y];
    }
    closePath() {
        if (!this.pen) return;
        this.d.push("Z");
        this.pen = this.start;
    }
    quadraticCurveTo(m, cx, cy, x, y) {
        if (!finite(cx, cy, x, y)) return;
        if (!this.pen) this.moveTo(m, cx, cy);
        this.d.push("Q" + this.point(m, cx, cy) + " " + this.point(m, x, y));
        this.pen = [x, y];
    }
    bezierCurveTo(m, c1x, c1y, c2x, c2y, x, y) {
        if (!finite(c1x, c1y, c2x, c2y, x, y)) return;
        if (!this.pen) this.moveTo(m, c1x, c1y);
        this.d.push("C" + this.point(m, c1x, c1y) + " " + this.point(m, c2x, c2y) + " " + this.point(m, x, y));
        this.pen = [x, y];
    }
    // An elliptical arc, as cubic Béziers of at most 90° each.
    ellipse(m, x, y, rx, ry, rotation, a0, a1, ccw) {
        if (!finite(x, y, rx, ry, rotation, a0, a1)) return;
        if (rx < 0 || ry < 0) throw new DOMException("The radius provided is negative.", "IndexSizeError");
        let sweep = a1 - a0;
        if (!ccw) {
            if (sweep >= TAU) sweep = TAU;
            else sweep = ((sweep % TAU) + TAU) % TAU;
        } else {
            if (sweep <= -TAU) sweep = -TAU;
            else sweep = -((((a0 - a1) % TAU) + TAU) % TAU);
        }
        const local = multiply(m, [Math.cos(rotation), Math.sin(rotation), -Math.sin(rotation), Math.cos(rotation), x, y]);
        const at = a => [rx * Math.cos(a), ry * Math.sin(a)];
        const [sx, sy] = at(a0);
        const startUser = apply([Math.cos(rotation), Math.sin(rotation), -Math.sin(rotation), Math.cos(rotation), x, y], sx, sy);
        if (this.pen) this.d.push("L" + this.point(local, sx, sy));
        else { this.d.push("M" + this.point(local, sx, sy)); this.start = startUser; }
        const pieces = Math.max(1, Math.ceil(Math.abs(sweep) / (Math.PI / 2) - 1e-9));
        const step = sweep / pieces;
        const k = (4 / 3) * Math.tan(step / 4);
        let a = a0;
        for (let i = 0; i < pieces; i++) {
            const b = a + step;
            const [p0x, p0y] = [Math.cos(a), Math.sin(a)];
            const [p3x, p3y] = [Math.cos(b), Math.sin(b)];
            const c1 = [rx * (p0x - k * p0y), ry * (p0y + k * p0x)];
            const c2 = [rx * (p3x + k * p3y), ry * (p3y - k * p3x)];
            this.d.push("C" + this.point(local, ...c1) + " " + this.point(local, ...c2) + " " + this.point(local, rx * p3x, ry * p3y));
            a = b;
        }
        const [ex, ey] = at(a0 + sweep);
        this.pen = apply([Math.cos(rotation), Math.sin(rotation), -Math.sin(rotation), Math.cos(rotation), x, y], ex, ey);
    }
    arcTo(m, x1, y1, x2, y2, r) {
        if (!finite(x1, y1, x2, y2, r)) return;
        if (r < 0) throw new DOMException("The radius provided is negative.", "IndexSizeError");
        if (!this.pen) this.moveTo(m, x1, y1);
        const [x0, y0] = this.pen;
        const v1 = [x0 - x1, y0 - y1], v2 = [x2 - x1, y2 - y1];
        const l1 = Math.hypot(...v1), l2 = Math.hypot(...v2);
        const cross = v1[0] * v2[1] - v1[1] * v2[0];
        if (r === 0 || l1 === 0 || l2 === 0 || Math.abs(cross) < 1e-9) return this.lineTo(m, x1, y1);
        const angle = Math.acos(Math.max(-1, Math.min(1, (v1[0] * v2[0] + v1[1] * v2[1]) / (l1 * l2))));
        const t = r / Math.tan(angle / 2);
        const p1 = [x1 + (v1[0] / l1) * t, y1 + (v1[1] / l1) * t];
        const p2 = [x1 + (v2[0] / l2) * t, y1 + (v2[1] / l2) * t];
        const bis = [v1[0] / l1 + v2[0] / l2, v1[1] / l1 + v2[1] / l2];
        const bl = Math.hypot(...bis);
        const d = r / Math.sin(angle / 2);
        const c = [x1 + (bis[0] / bl) * d, y1 + (bis[1] / bl) * d];
        this.lineTo(m, ...p1);
        this.ellipse(m, c[0], c[1], r, r, 0, Math.atan2(p1[1] - c[1], p1[0] - c[0]), Math.atan2(p2[1] - c[1], p2[0] - c[0]), cross > 0);
    }
    rect(m, x, y, w, h) {
        if (!finite(x, y, w, h)) return;
        this.moveTo(m, x, y);
        this.lineTo(m, x + w, y);
        this.lineTo(m, x + w, y + h);
        this.lineTo(m, x, y + h);
        this.closePath();
    }
    roundRect(m, x, y, w, h, radii = 0) {
        if (!finite(x, y, w, h)) return;
        const list = Array.isArray(radii) ? radii : [radii];
        const radius = v => typeof v === "object" && v ? +v.x || 0 : +v || 0;
        const [tl, tr = tl, br = tl, bl = tr] = list.map(radius);
        const fit = v => Math.max(0, Math.min(v, Math.abs(w) / 2, Math.abs(h) / 2));
        const [a, b, c, d] = [fit(tl), fit(tr), fit(br), fit(bl)];
        this.moveTo(m, x + a, y);
        this.lineTo(m, x + w - b, y);
        if (b) this.ellipse(m, x + w - b, y + b, b, b, 0, -Math.PI / 2, 0, false);
        this.lineTo(m, x + w, y + h - c);
        if (c) this.ellipse(m, x + w - c, y + h - c, c, c, 0, 0, Math.PI / 2, false);
        this.lineTo(m, x + d, y + h);
        if (d) this.ellipse(m, x + d, y + h - d, d, d, 0, Math.PI / 2, Math.PI, false);
        this.lineTo(m, x, y + a);
        if (a) this.ellipse(m, x + a, y + a, a, a, 0, Math.PI, Math.PI * 1.5, false);
        this.closePath();
    }
}

const PATH_METHODS = ["moveTo", "lineTo", "closePath", "quadraticCurveTo", "bezierCurveTo", "arc", "arcTo", "ellipse", "rect", "roundRect"];

// Replays recorded user-space calls into `path` under `matrix`.
function replay(calls, path, matrix) {
    for (const [name, args] of calls) {
        if (name === "closePath") path.closePath();
        else if (name === "arc") path.ellipse(matrix, args[0], args[1], args[2], args[2], 0, args[3], args[4], !!args[5]);
        else path[name](matrix, ...args);
    }
}

export class Path2D {
    constructor(source) {
        this._calls = [];
        if (source instanceof Path2D) this._calls = source._calls.slice();
        else if (typeof source === "string") this._svg = source; // SVG path data, used as is
    }
    addPath(path, transform) {
        if (!(path instanceof Path2D)) return;
        if (transform) {
            const m = [transform.a ?? 1, transform.b ?? 0, transform.c ?? 0, transform.d ?? 1, transform.e ?? 0, transform.f ?? 0];
            this._calls.push(["_transformed", [m, path]]);
        } else this._calls.push(...path._calls);
    }
}
for (const name of PATH_METHODS)
    Path2D.prototype[name] = function (...args) { this._calls.push([name, args]); };

// ------------------------------------------------------------- context --

const STATE = {
    fillStyle: "#000000", strokeStyle: "#000000", lineWidth: 1, lineCap: "butt", lineJoin: "miter", miterLimit: 10,
    globalAlpha: 1, font: "10px sans-serif", textAlign: "start", textBaseline: "alphabetic", direction: "inherit",
    lineDashOffset: 0, shadowBlur: 0, shadowColor: "rgba(0, 0, 0, 0)", shadowOffsetX: 0, shadowOffsetY: 0,
    globalCompositeOperation: "source-over", imageSmoothingEnabled: true, imageSmoothingQuality: "low", filter: "none",
    letterSpacing: "0px", wordSpacing: "0px", fontKerning: "auto", textRendering: "auto",
};

// A CSS font shorthand: style, weight, size (px) and a generic family lunasvg has.
function parseFont(font) {
    const m = /^\s*((?:(?:normal|italic|oblique|small-caps|bold|bolder|lighter|[1-9]00)\s+)*)([\d.]+)(px|pt|em|rem|%)(?:\s*\/\s*\S+)?\s+(.+)$/i.exec(font);
    if (!m) return null;
    const words = m[1].toLowerCase().split(/\s+/);
    let size = +m[2];
    const unit = m[3].toLowerCase();
    if (unit === "pt") size *= 4 / 3;
    else if (unit === "em" || unit === "rem") size *= 16;
    else if (unit === "%") size *= 0.16;
    const families = m[4].split(",").map(f => f.trim().replace(/^["']|["']$/g, "").toLowerCase());
    let family = "sans-serif";
    for (const f of families) {
        if (/^(monospace|courier|courier new|consolas|menlo|monaco|lucida console)$/.test(f)) { family = "monospace"; break; }
        if (/^(serif|times|times new roman|georgia|garamond|cambria|palatino)$/.test(f)) { family = "serif"; break; }
        if (/^(sans-serif|system-ui|arial|helvetica|verdana|tahoma|segoe ui|roboto)$/.test(f)) break;
    }
    const weight = words.find(w => /^(bold|bolder|[6-9]00)$/.test(w)) ? "bold" : "normal";
    const style = words.includes("italic") || words.includes("oblique") ? "italic" : "normal";
    return { size, family, weight, style };
}

export class CanvasRenderingContext2D {
    constructor(canvas) {
        Object.defineProperty(this, "canvas", { value: canvas, enumerable: true });
        this._clear();
    }
    _clear() {
        this._items = [];
        this._defs = [];
        this._ids = 0;
        this._stack = [];
        this._state = { ...STATE, matrix: IDENTITY.slice(), dash: [] };
        this._path = new PathData();
        this._version = (this._version || 0) + 1;
        this._uri = null;
    }
    _changed() {
        this._version++;
        this._uri = null;
    }
    _emit(markup) {
        this._items.push(markup);
        this._changed();
    }
    // SVG paint for a style, defining gradients as needed: [paint, opacity].
    _paint(style, matrix) {
        if (style instanceof CanvasGradient) {
            if (!style._stops.length) return ["none", 0];
            const id = `g${++this._ids}`;
            const stops = style._stops.slice().sort((a, b) => a[0] - b[0])
                .map(([o, p]) => `<stop offset="${num(o)}" stop-color="${p.color}" stop-opacity="${num(p.alpha)}"/>`).join("");
            const c = style._coords.map(num);
            const attrs = style._kind === "linear"
                ? `x1="${c[0]}" y1="${c[1]}" x2="${c[2]}" y2="${c[3]}"`
                : `cx="${c[3]}" cy="${c[4]}" r="${c[5]}" fx="${c[0]}" fy="${c[1]}"`;
            this._defs.push(`<${style._kind}Gradient id="${id}" gradientUnits="userSpaceOnUse" gradientTransform="${matrixAttr(matrix)}" ${attrs}>${stops}</${style._kind}Gradient>`);
            return [`url(#${id})`, 1];
        }
        if (style instanceof CanvasPattern) return ["none", 0];
        const p = paintOf(style) || { color: "#000", alpha: 1 };
        return [p.color, p.alpha];
    }
    _fill(d, rule) {
        if (!d) return;
        const s = this._state;
        const [paint, alpha] = this._paint(s.fillStyle, s.matrix);
        const opacity = alpha * s.globalAlpha;
        if (paint === "none" || opacity <= 0) return;
        this._emit(`<path d="${d}" fill="${paint}"${opacity < 1 ? ` fill-opacity="${num(opacity)}"` : ""}${rule === "evenodd" ? ` fill-rule="evenodd"` : ""}/>`);
    }
    _stroke(d) {
        if (!d) return;
        const s = this._state;
        const [paint, alpha] = this._paint(s.strokeStyle, s.matrix);
        const opacity = alpha * s.globalAlpha;
        if (paint === "none" || opacity <= 0 || !(s.lineWidth > 0)) return;
        const scale = Math.sqrt(Math.abs(s.matrix[0] * s.matrix[3] - s.matrix[1] * s.matrix[2])) || 1;
        let attrs = `fill="none" stroke="${paint}" stroke-width="${num(s.lineWidth * scale)}"`;
        if (s.lineCap !== "butt") attrs += ` stroke-linecap="${s.lineCap}"`;
        if (s.lineJoin !== "miter") attrs += ` stroke-linejoin="${s.lineJoin}"`;
        if (s.miterLimit !== 4) attrs += ` stroke-miterlimit="${num(s.miterLimit)}"`;
        if (s.dash.length) attrs += ` stroke-dasharray="${s.dash.map(v => num(v * scale)).join(" ")}" stroke-dashoffset="${num(s.lineDashOffset * scale)}"`;
        if (opacity < 1) attrs += ` stroke-opacity="${num(opacity)}"`;
        this._emit(`<path d="${d}" ${attrs}/>`);
    }
    // The path data of a Path2D (or the current path) in device space.
    _pathData(path) {
        if (!(path instanceof Path2D)) return this._path.d.join("");
        if (path._svg !== undefined) return null; // drawn through a transform below
        const data = new PathData();
        const run = (calls, matrix) => {
            for (const call of calls) {
                if (call[0] === "_transformed") run(call[1][1]._calls, multiply(matrix, call[1][0]));
                else replay([call], data, matrix);
            }
        };
        run(path._calls, this._state.matrix);
        return data.d.join("");
    }
    _svgPath(path, markup) {
        // A Path2D from SVG path data: the data in user space, under the transform.
        this._emit(`<g transform="${matrixAttr(this._state.matrix)}">${markup(escapeXml(path._svg))}</g>`);
    }

    // State
    save() { this._stack.push({ ...this._state, matrix: this._state.matrix.slice(), dash: this._state.dash.slice() }); }
    restore() { if (this._stack.length) this._state = this._stack.pop(); }
    reset() { this._clear(); }
    isContextLost() { return false; }
    getContextAttributes() { return { alpha: true, colorSpace: "srgb", desynchronized: false, willReadFrequently: false }; }

    // Transforms
    scale(x, y) { if (finite(x, y)) this._state.matrix = multiply(this._state.matrix, [x, 0, 0, y, 0, 0]); }
    rotate(a) { if (finite(a)) this._state.matrix = multiply(this._state.matrix, [Math.cos(a), Math.sin(a), -Math.sin(a), Math.cos(a), 0, 0]); }
    translate(x, y) { if (finite(x, y)) this._state.matrix = multiply(this._state.matrix, [1, 0, 0, 1, x, y]); }
    transform(a, b, c, d, e, f) { if (finite(a, b, c, d, e, f)) this._state.matrix = multiply(this._state.matrix, [a, b, c, d, e, f]); }
    setTransform(a = 1, b = 0, c = 0, d = 1, e = 0, f = 0) {
        if (typeof a === "object" && a) ({ a = 1, b = 0, c = 0, d = 1, e = 0, f = 0 } = a);
        if (finite(a, b, c, d, e, f)) this._state.matrix = [a, b, c, d, e, f];
    }
    resetTransform() { this._state.matrix = IDENTITY.slice(); }
    getTransform() {
        const [a, b, c, d, e, f] = this._state.matrix;
        return typeof DOMMatrix === "function" ? new DOMMatrix([a, b, c, d, e, f]) : { a, b, c, d, e, f, is2D: true };
    }

    // Line styles
    setLineDash(segments) {
        const list = Array.from(segments || [], Number);
        if (list.some(v => !Number.isFinite(v) || v < 0)) return;
        this._state.dash = list.length % 2 ? list.concat(list) : list;
    }
    getLineDash() { return this._state.dash.slice(); }

    // Gradients and patterns
    createLinearGradient(x0, y0, x1, y1) { return new CanvasGradient("linear", [x0, y0, x1, y1]); }
    createRadialGradient(x0, y0, r0, x1, y1, r1) { return new CanvasGradient("radial", [x0, y0, r0, x1, y1, r1]); }
    createConicGradient(angle, x, y) { return new CanvasGradient("linear", [x, y, x + 1, y]); }
    createPattern() { return new CanvasPattern(); }

    // Rectangles
    clearRect(x, y, w, h) {
        if (!finite(x, y, w, h)) return;
        const m = this._state.matrix;
        const { width, height } = this.canvas;
        const corners = [[x, y], [x + w, y], [x + w, y + h], [x, y + h]].map(([px, py]) => apply(m, px, py));
        const xs = corners.map(c => c[0]), ys = corners.map(c => c[1]);
        const axisAligned = m[1] === 0 && m[2] === 0;
        if (axisAligned && Math.min(...xs) <= 0 && Math.min(...ys) <= 0 && Math.max(...xs) >= width && Math.max(...ys) >= height) {
            this._items = [];
            this._defs = [];
            this._changed();
            return;
        }
        if (!this._items.length) return;
        // Erase by masking what was drawn so far.
        const id = `m${++this._ids}`;
        const hole = "M" + corners.map(c => c.map(num).join(" ")).join("L") + "Z";
        this._defs.push(`<mask id="${id}" maskUnits="userSpaceOnUse" x="0" y="0" width="${width}" height="${height}">` +
            `<rect width="${width}" height="${height}" fill="#fff"/><path d="${hole}" fill="#000"/></mask>`);
        this._items = [`<g mask="url(#${id})">${this._items.join("")}</g>`];
        this._changed();
    }
    fillRect(x, y, w, h) {
        const p = new PathData();
        p.rect(this._state.matrix, x, y, w, h);
        this._fill(p.d.join(""));
    }
    strokeRect(x, y, w, h) {
        const p = new PathData();
        p.rect(this._state.matrix, x, y, w, h);
        this._stroke(p.d.join(""));
    }

    // Paths
    beginPath() { this._path = new PathData(); }
    arc(x, y, r, a0, a1, ccw = false) { this._path.ellipse(this._state.matrix, x, y, r, r, 0, a0, a1, !!ccw); }
    fill(path, rule) {
        if (typeof path === "string") [path, rule] = [undefined, path];
        if (path instanceof Path2D && path._svg !== undefined) {
            const s = this._state;
            const [paint, alpha] = this._paint(s.fillStyle, IDENTITY);
            if (paint !== "none") this._svgPath(path, d => `<path d="${d}" fill="${paint}" fill-opacity="${num(alpha * s.globalAlpha)}"${rule === "evenodd" ? ` fill-rule="evenodd"` : ""}/>`);
            return;
        }
        this._fill(this._pathData(path), rule);
    }
    stroke(path) {
        if (path instanceof Path2D && path._svg !== undefined) {
            const s = this._state;
            const [paint, alpha] = this._paint(s.strokeStyle, IDENTITY);
            if (paint !== "none") this._svgPath(path, d => `<path d="${d}" fill="none" stroke="${paint}" stroke-width="${num(s.lineWidth)}" stroke-opacity="${num(alpha * s.globalAlpha)}"/>`);
            return;
        }
        this._stroke(this._pathData(path));
    }
    clip() {}
    isPointInPath() { return false; }
    isPointInStroke() { return false; }
    drawFocusIfNeeded() {}

    // Text
    _text(text, x, y, maxWidth, mode) {
        const s = this._state;
        const font = parseFont(s.font) || parseFont(STATE.font);
        if (!finite(x, y) || !font) return;
        const shift = { top: 0.8, hanging: 0.8, middle: 0.35, bottom: -0.2, ideographic: -0.2 }[s.textBaseline] || 0;
        let m = multiply(s.matrix, [1, 0, 0, 1, x, y + shift * font.size]);
        if (Number.isFinite(maxWidth) && maxWidth > 0) {
            const width = this.measureText(text).width;
            if (width > maxWidth) m = multiply(m, [maxWidth / width, 0, 0, 1, 0, 0]);
        }
        const anchor = { center: "middle", end: "end", right: "end" }[s.textAlign] || "start";
        const [paint, alpha] = this._paint(mode === "fill" ? s.fillStyle : s.strokeStyle, IDENTITY);
        const opacity = alpha * s.globalAlpha;
        if (paint === "none" || opacity <= 0) return;
        const paintAttrs = mode === "fill"
            ? `fill="${paint}"${opacity < 1 ? ` fill-opacity="${num(opacity)}"` : ""}`
            : `fill="none" stroke="${paint}" stroke-width="${num(s.lineWidth)}"${opacity < 1 ? ` stroke-opacity="${num(opacity)}"` : ""}`;
        this._emit(`<text transform="${matrixAttr(m)}" font-family="${font.family}" font-size="${num(font.size)}"` +
            `${font.weight !== "normal" ? ` font-weight="${font.weight}"` : ""}${font.style !== "normal" ? ` font-style="${font.style}"` : ""}` +
            `${anchor !== "start" ? ` text-anchor="${anchor}"` : ""} ${paintAttrs} xml:space="preserve">${escapeXml(text)}</text>`);
    }
    fillText(text, x, y, maxWidth) { this._text(String(text), x, y, maxWidth, "fill"); }
    strokeText(text, x, y, maxWidth) { this._text(String(text), x, y, maxWidth, "stroke"); }
    // Estimated from the font size (no font measurement in the page).
    measureText(text) {
        const font = parseFont(this._state.font) || parseFont(STATE.font);
        let width = 0;
        for (const ch of String(text)) {
            const code = ch.codePointAt(0);
            width += font.size * (code >= 0x2e80 ? 1 : font.family === "monospace" ? 0.6 : /[ilIj.,:;'|!]/.test(ch) ? 0.3 : /[mwMW]/.test(ch) ? 0.85 : 0.55);
        }
        const ascent = font.size * 0.76, descent = font.size * 0.24;
        return {
            width, actualBoundingBoxLeft: 0, actualBoundingBoxRight: width, actualBoundingBoxAscent: ascent,
            actualBoundingBoxDescent: descent, fontBoundingBoxAscent: font.size * 0.93, fontBoundingBoxDescent: font.size * 0.24,
            emHeightAscent: ascent, emHeightDescent: descent, alphabeticBaseline: 0, hangingBaseline: ascent, ideographicBaseline: -descent,
        };
    }

    // Images
    drawImage(image, ...args) {
        let source = null;
        if (image instanceof linkedom.HTMLCanvasElement) {
            const ctx = contexts.get(image);
            source = { uri: canvasUri(image, ctx), width: image.width, height: image.height };
        } else if (image && typeof image.getAttribute === "function") {
            const src = image.getAttribute("src") || "";
            source = src.startsWith("data:") ? { uri: src, width: +image.getAttribute("width") || 0, height: +image.getAttribute("height") || 0 } : mediaImage(src);
        }
        if (!source || !source.width || !source.height) return;
        let [sx, sy, sw, sh] = [0, 0, source.width, source.height];
        let dx, dy, dw, dh;
        if (args.length >= 8) [sx, sy, sw, sh, dx, dy, dw, dh] = args;
        else if (args.length >= 4) [dx, dy, dw, dh] = args;
        else [dx, dy, dw, dh] = [args[0], args[1], source.width, source.height];
        if (!finite(sx, sy, sw, sh, dx, dy, dw, dh) || !sw || !sh) return;
        const s = this._state;
        const m = multiply(s.matrix, [dw / sw, 0, 0, dh / sh, dx - sx * (dw / sw), dy - sy * (dh / sh)]);
        const id = `c${++this._ids}`;
        this._defs.push(`<clipPath id="${id}"><rect x="${num(sx)}" y="${num(sy)}" width="${num(sw)}" height="${num(sh)}"/></clipPath>`);
        this._emit(`<g transform="${matrixAttr(m)}"${s.globalAlpha < 1 ? ` opacity="${num(s.globalAlpha)}"` : ""}>` +
            `<image width="${num(source.width)}" height="${num(source.height)}" preserveAspectRatio="none" clip-path="url(#${id})" href="${escapeXml(source.uri)}"/></g>`);
    }

    // Pixels: not kept (the drawing is vector); a blank image of the size.
    createImageData(w, h) {
        if (typeof w === "object" && w) [w, h] = [w.width, w.height];
        return new ImageData(Math.max(1, Math.abs(w | 0)), Math.max(1, Math.abs(h | 0)));
    }
    getImageData(x, y, w, h) { return this.createImageData(w, h); }
    putImageData() {}
}
for (const name of Object.keys(STATE))
    Object.defineProperty(CanvasRenderingContext2D.prototype, name, {
        configurable: true,
        enumerable: true,
        get() { return this._state[name]; },
        set(value) {
            if (name === "fillStyle" || name === "strokeStyle") {
                if (value instanceof CanvasGradient || value instanceof CanvasPattern) {
                    this._state[name] = value;
                    return;
                }
                if (!paintOf(value)) return;
            } else if (typeof STATE[name] === "number") {
                value = +value;
                if (!Number.isFinite(value) || (name !== "lineDashOffset" && name !== "shadowOffsetX" && name !== "shadowOffsetY" && value < 0)) return;
                if (name === "globalAlpha" && value > 1) return;
            } else if (name === "font" && !parseFont(String(value))) return;
            this._state[name] = typeof STATE[name] === "string" ? String(value) : value;
        },
    });
for (const name of PATH_METHODS)
    if (!CanvasRenderingContext2D.prototype[name])
        CanvasRenderingContext2D.prototype[name] = function (...args) {
            if (name === "closePath") this._path.closePath();
            else this._path[name](this._state.matrix, ...args);
        };

export class ImageData {
    constructor(a, b, c) {
        if (a instanceof Uint8ClampedArray) {
            this.data = a;
            this.width = b;
            this.height = c ?? a.length / 4 / b;
        } else {
            this.width = a;
            this.height = b;
            this.data = new Uint8ClampedArray(a * b * 4);
        }
        this.colorSpace = "srgb";
    }
}

// ------------------------------------------------------------- element --

const contexts = new WeakMap();
const size = (el, name, fallback) => {
    const v = parseInt(el.getAttribute(name), 10);
    return Number.isFinite(v) && v >= 0 ? v : fallback;
};
const CanvasProto = linkedom.HTMLCanvasElement.prototype;
for (const [name, fallback] of [["width", 300], ["height", 150]])
    Object.defineProperty(CanvasProto, name, {
        configurable: true,
        get() { return size(this, name, fallback); },
        set(value) {
            this.setAttribute(name, String(Math.max(0, value >>> 0)));
            const ctx = contexts.get(this);
            if (ctx) ctx._clear(); // setting the size clears the canvas
        },
    });
CanvasProto.getContext = function (type) {
    if (type !== "2d") return null;
    let ctx = contexts.get(this);
    if (!ctx) contexts.set(this, (ctx = new CanvasRenderingContext2D(this)));
    return ctx;
};
CanvasProto.toDataURL = function () { return canvasUri(this, contexts.get(this)); };
CanvasProto.toBlob = function (callback) {
    const svg = canvasSvg(this, contexts.get(this));
    setTimeout(() => callback(typeof Blob === "function" ? new Blob([svg], { type: "image/svg+xml" }) : null), 0);
};
CanvasProto.transferControlToOffscreen = function () { throw new DOMException("Not supported.", "NotSupportedError"); };

function canvasSvg(canvas, ctx) {
    const { width, height } = canvas;
    const defs = ctx && ctx._defs.length ? `<defs>${ctx._defs.join("")}</defs>` : "";
    return `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">${defs}${ctx ? ctx._items.join("") : ""}</svg>`;
}
function canvasUri(canvas, ctx) {
    if (ctx && ctx._uri && ctx._uriSize === `${canvas.width}x${canvas.height}`) return ctx._uri;
    const uri = "data:image/svg+xml," + encodeURIComponent(canvasSvg(canvas, ctx));
    if (ctx) {
        ctx._uri = uri;
        ctx._uriSize = `${canvas.width}x${canvas.height}`;
    }
    return uri;
}

// How the serialiser writes a canvas: an <img> of its drawing, the same size.
export function canvasImage(canvas) {
    return { src: canvasUri(canvas, contexts.get(canvas)), width: canvas.width, height: canvas.height };
}

define("CanvasRenderingContext2D", CanvasRenderingContext2D);
define("CanvasGradient", CanvasGradient);
define("CanvasPattern", CanvasPattern);
define("Path2D", Path2D);
define("ImageData", ImageData);
