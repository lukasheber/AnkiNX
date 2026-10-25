/* AnkiNX: the browser Memory Forest's scene scripts expect, as much of it as they use. Canvases
 * are native (PixelCanvas, app/src/forest/pixel_canvas.cpp, through globalThis.__pc), with
 * Path2D and ImageData over them; the document only makes canvases; the clock, the console and
 * storage go to the app. Nothing here lays out or shows a page: app/forest/src/runner.js draws
 * each frame and the app puts it on screen. */
(function () {
'use strict';
const pc = globalThis.__pc, native = globalThis.__native;
const g = globalThis;
g.window = g;
g.self = g;

// what each canvas call the scenes make but PixelCanvas can't draw says, once
const warned = new Set();
const warn = what => { if (!warned.has(what)) { warned.add(what); native.log(1, 'not drawn: ' + what); } };

/* ---------- paths ---------- */
// (the numbers PixelCanvas::PathOp reads)
const RECT = 0, MOVE = 1, LINE = 2, CLOSE = 3;
const CURVE_STEPS = 12;
class Path2D {
  constructor(from) {
    this._ops = from && from._ops ? from._ops.slice() : [];
    this._open = false; this._x = 0; this._y = 0; this._sx = 0; this._sy = 0;
    this._f64 = null;
  }
  _buf() {
    if (!this._f64 || this._f64.length !== this._ops.length) this._f64 = new Float64Array(this._ops);
    return this._f64;
  }
  rect(x, y, w, h) { this._ops.push(RECT, x, y, w, h); this._open = false; this._x = this._sx = x; this._y = this._sy = y; }
  roundRect(x, y, w, h) { this.rect(x, y, w, h); }
  moveTo(x, y) { this._ops.push(MOVE, x, y); this._open = true; this._x = this._sx = x; this._y = this._sy = y; }
  lineTo(x, y) {
    if (!this._open) { this.moveTo(x, y); return; }
    this._ops.push(LINE, x, y); this._x = x; this._y = y;
  }
  closePath() { if (this._open) { this._ops.push(CLOSE); this._x = this._sx; this._y = this._sy; } }
  arcTo(x1, y1) { this.lineTo(x1, y1); }  // (the corner, without its rounding)
  quadraticCurveTo(cx, cy, x, y) {
    const x0 = this._x, y0 = this._y;
    for (let i = 1; i <= CURVE_STEPS; i++) {
      const t = i / CURVE_STEPS, u = 1 - t;
      this.lineTo(u * u * x0 + 2 * u * t * cx + t * t * x, u * u * y0 + 2 * u * t * cy + t * t * y);
    }
  }
  bezierCurveTo(c1x, c1y, c2x, c2y, x, y) {
    const x0 = this._x, y0 = this._y;
    for (let i = 1; i <= CURVE_STEPS; i++) {
      const t = i / CURVE_STEPS, u = 1 - t;
      this.lineTo(u * u * u * x0 + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * x,
        u * u * u * y0 + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * y);
    }
  }
  ellipse(cx, cy, rx, ry, rot, a0, a1, ccw) {
    let sweep = a1 - a0;
    if (!ccw && sweep < 0) sweep = sweep % (2 * Math.PI) + 2 * Math.PI;
    if (ccw && sweep > 0) sweep = sweep % (2 * Math.PI) - 2 * Math.PI;
    if (Math.abs(a1 - a0) >= 2 * Math.PI) sweep = ccw ? -2 * Math.PI : 2 * Math.PI;
    const steps = Math.max(8, Math.ceil(Math.abs(sweep) / (Math.PI / 16))), cr = Math.cos(rot || 0), sr = Math.sin(rot || 0);
    for (let i = 0; i <= steps; i++) {
      const a = a0 + sweep * i / steps, ex = Math.cos(a) * rx, ey = Math.sin(a) * ry;
      this.lineTo(cx + ex * cr - ey * sr, cy + ex * sr + ey * cr);
    }
  }
  arc(cx, cy, r, a0, a1, ccw) { this.ellipse(cx, cy, r, r, 0, a0, a1, ccw); }
  addPath(p) { if (p && p._ops) { this._ops.push(...p._ops); this._open = false; } }
}

/* ---------- image data ---------- */
class ImageData {
  constructor(a, b, c) {
    if (a instanceof Uint8ClampedArray) { this.data = a; this.width = b; this.height = c || a.length / 4 / b; }
    else { this.width = a; this.height = b; this.data = new Uint8ClampedArray(a * b * 4); }
  }
}

/* ---------- the 2D context ---------- */
const DEFAULTS = {
  fillStyle: '#000000', strokeStyle: '#000000', globalAlpha: 1, globalCompositeOperation: 'source-over',
  imageSmoothingEnabled: true, imageSmoothingQuality: 'low', lineWidth: 1, lineCap: 'butt', lineJoin: 'miter',
  font: '10px sans-serif', textAlign: 'start', textBaseline: 'alphabetic', shadowBlur: 0, shadowColor: 'rgba(0, 0, 0, 0)',
  shadowOffsetX: 0, shadowOffsetY: 0, filter: 'none',
};
class CanvasRenderingContext2D {
  constructor(canvas) { this.canvas = canvas; this._n = canvas._n; this._reset(); }
  _reset() { this._s = Object.assign({}, DEFAULTS); this._stack = []; this._path = new Path2D(); }

  get fillStyle() { return this._s.fillStyle; }
  set fillStyle(v) {
    if (typeof v !== 'string') { warn('gradient or pattern fills'); return; }
    if (pc.fill(this._n, v)) this._s.fillStyle = v;
  }
  get globalAlpha() { return this._s.globalAlpha; }
  set globalAlpha(v) { v = +v; if (v >= 0 && v <= 1) { this._s.globalAlpha = v; pc.alpha(this._n, v); } }
  get globalCompositeOperation() { return this._s.globalCompositeOperation; }
  set globalCompositeOperation(v) {
    if (pc.op(this._n, String(v))) this._s.globalCompositeOperation = String(v); else warn('composite ' + v);
  }

  save() { this._stack.push(Object.assign({}, this._s)); pc.save(this._n); }
  restore() { if (this._stack.length) { this._s = this._stack.pop(); pc.restore(this._n); } }

  translate(x, y) { pc.transform(this._n, 1, 0, 0, 1, x, y); }
  scale(x, y) { pc.transform(this._n, x, 0, 0, y, 0, 0); }
  rotate(a) { const c = Math.cos(a), s = Math.sin(a); pc.transform(this._n, c, s, -s, c, 0, 0); }
  transform(a, b, c, d, e, f) { pc.transform(this._n, a, b, c, d, e, f); }
  setTransform(a, b, c, d, e, f) {
    if (a && typeof a === 'object') ({ a, b, c, d, e, f } = a);
    if (a === undefined) { a = 1; b = 0; c = 0; d = 1; e = 0; f = 0; }
    pc.setTransform(this._n, a, b, c, d, e, f);
  }
  resetTransform() { pc.setTransform(this._n, 1, 0, 0, 1, 0, 0); }

  fillRect(x, y, w, h) { pc.fillRect(this._n, x, y, w, h); }
  clearRect(x, y, w, h) { pc.clearRect(this._n, x, y, w, h); }
  strokeRect() { warn('strokes'); }

  beginPath() { this._path = new Path2D(); }
  moveTo(x, y) { this._path.moveTo(x, y); }
  lineTo(x, y) { this._path.lineTo(x, y); }
  closePath() { this._path.closePath(); }
  rect(x, y, w, h) { this._path.rect(x, y, w, h); }
  roundRect(x, y, w, h) { this._path.rect(x, y, w, h); }
  arc(...a) { this._path.arc(...a); }
  arcTo(...a) { this._path.arcTo(...a); }
  ellipse(...a) { this._path.ellipse(...a); }
  quadraticCurveTo(...a) { this._path.quadraticCurveTo(...a); }
  bezierCurveTo(...a) { this._path.bezierCurveTo(...a); }
  fill(path) { pc.fillPath(this._n, (path instanceof Path2D ? path : this._path)._buf()); }
  clip(path) { pc.clipPath(this._n, (path instanceof Path2D ? path : this._path)._buf()); }
  stroke() { warn('strokes'); }
  fillText() { warn('text'); }
  strokeText() { warn('text'); }
  measureText(s) { return { width: String(s).length * 6 }; }
  setLineDash() {}
  getLineDash() { return []; }
  createLinearGradient() { warn('gradients'); return { addColorStop() {} }; }
  createRadialGradient() { warn('gradients'); return { addColorStop() {} }; }
  createPattern() { warn('patterns'); return null; }

  drawImage(img, a, b, c, d, e, f, h, i) {
    if (!img || !img._n) { warn('images other than canvases'); return; }
    if (arguments.length <= 3) pc.drawImage(this._n, img._n, 0, 0, img.width, img.height, a, b, img.width, img.height);
    else if (arguments.length <= 5) pc.drawImage(this._n, img._n, 0, 0, img.width, img.height, a, b, c, d);
    else pc.drawImage(this._n, img._n, a, b, c, d, e, f, h, i);
  }
  getImageData(x, y, w, h) { return new ImageData(new Uint8ClampedArray(pc.getImageData(this._n, x | 0, y | 0, w | 0, h | 0)), w | 0, h | 0); }
  putImageData(img, dx, dy, x = 0, y = 0, w = img.width, h = img.height) {
    pc.putImageData(this._n, img.data, img.width, img.height, dx | 0, dy | 0, x | 0, y | 0, w | 0, h | 0);
  }
  createImageData(w, h) { return w instanceof ImageData ? new ImageData(w.width, w.height) : new ImageData(w | 0, h | 0); }
}
for (const key of Object.keys(DEFAULTS)) {
  if (Object.getOwnPropertyDescriptor(CanvasRenderingContext2D.prototype, key)) continue;
  Object.defineProperty(CanvasRenderingContext2D.prototype, key, {
    get() { return this._s[key]; },
    set(v) { this._s[key] = v; },
  });
}

/* ---------- canvases and the document ---------- */
class HTMLCanvasElement {
  constructor() {
    this._w = 300; this._h = 150; this._n = pc.create(300, 150); this._ctx = null;
    this.style = {}; this.dataset = {}; this.nodeType = 1; this.tagName = 'CANVAS';
  }
  get width() { return this._w; }
  set width(v) { this._w = Math.max(0, v | 0); this._resized(); }
  get height() { return this._h; }
  set height(v) { this._h = Math.max(0, v | 0); this._resized(); }
  _resized() { pc.resize(this._n, this._w, this._h); if (this._ctx) this._ctx._reset(); }
  getContext(type) { return type === '2d' ? this._ctx || (this._ctx = new CanvasRenderingContext2D(this)) : null; }
  toDataURL() { return 'data:,'; }
  addEventListener() {}
  removeEventListener() {}
  getBoundingClientRect() { return { x: 0, y: 0, left: 0, top: 0, width: this._w, height: this._h, right: this._w, bottom: this._h }; }
}

const element = tag => ({
  tagName: String(tag).toUpperCase(), nodeType: 1, style: {}, dataset: {}, children: [], clientWidth: 0, clientHeight: 0,
  classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
  appendChild(c) { return c; }, append() {}, prepend() {}, remove() {}, setAttribute() {}, getAttribute: () => null,
  addEventListener() {}, removeEventListener() {}, querySelector: () => null, querySelectorAll: () => [],
  getBoundingClientRect: () => ({ x: 0, y: 0, left: 0, top: 0, width: 0, height: 0, right: 0, bottom: 0 }),
});
g.document = {
  createElement: tag => String(tag).toLowerCase() === 'canvas' ? new HTMLCanvasElement() : element(tag),
  getElementById: () => null, querySelector: () => null, querySelectorAll: () => [],
  addEventListener() {}, removeEventListener() {}, hidden: false, scripts: [],
  head: element('head'), body: element('body'), documentElement: element('html'),
};
g.getComputedStyle = () => ({ backgroundColor: '', getPropertyValue: () => '' });
g.performance = { now: native.now };
// (the runner draws each frame itself: nothing in a scene waits on a timer or a refresh)
g.requestAnimationFrame = () => 0;
g.cancelAnimationFrame = () => {};
g.requestIdleCallback = () => 0;
g.setTimeout = () => 0;
g.clearTimeout = () => {};
g.navigator = { language: 'en-US', languages: ['en-US'] };
const store = new Map();
g.localStorage = {
  getItem: k => store.has(String(k)) ? store.get(String(k)) : null,
  setItem: (k, v) => { store.set(String(k), String(v)); },
  removeItem: k => { store.delete(String(k)); },
};
const text = a => a.map(v => v && v.stack ? v.stack : typeof v === 'object' ? JSON.stringify(v) : String(v)).join(' ');
g.console = {
  log: (...a) => native.log(0, text(a)), info: (...a) => native.log(0, text(a)), debug: (...a) => native.log(0, text(a)),
  warn: (...a) => native.log(1, text(a)), error: (...a) => native.log(2, text(a)),
};
/* ---------- Intl, in English: QuickJS has none (the tooltips' dates and numbers) ---------- */
if (typeof g.Intl === 'undefined') {
  const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October',
    'November', 'December'];
  const WEEKDAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const english = locales => [].concat(locales || []).filter(l => /^en\b/i.test(l));
  class DateTimeFormat {
    constructor(_locale, opts) { this.o = opts || {}; }
    static supportedLocalesOf(locales) { return english(locales); }
    // "Oct 17, 2026", "October 2026", "October", "Sunday"
    format(date) {
      const o = this.o, d = date instanceof Date ? date : new Date(date), utc = o.timeZone === 'UTC';
      const get = k => (utc ? d['getUTC' + k]() : d['get' + k]());
      const month = get('Month'), day = get('Date'), year = get('FullYear'), weekday = WEEKDAYS[get('Day')];
      const m = o.month === 'long' ? MONTHS[month] : o.month === 'short' ? MONTHS[month].slice(0, 3)
        : o.month ? String(month + 1) : '';
      let s = m && o.day ? `${m} ${day}` : m || (o.day ? String(day) : '');
      if (o.year) s = s ? (o.day ? `${s}, ${year}` : `${s} ${year}`) : String(year);
      if (o.weekday) { const w = o.weekday === 'long' ? weekday : weekday.slice(0, 3); s = s ? `${w}, ${s}` : w; }
      return s || d.toISOString().slice(0, 10);
    }
  }
  class NumberFormat {
    constructor(_locale, opts) { this.o = opts || {}; }
    static supportedLocalesOf(locales) { return english(locales); }
    // "1,234", "1.5"
    format(n) {
      const o = this.o, digits = o.maximumFractionDigits;
      const [whole, frac] = (digits != null ? Number(n).toFixed(Math.max(digits, o.minimumFractionDigits || 0))
        : String(Math.round(Number(n) * 1000) / 1000)).split('.');
      return whole.replace(/\B(?=(\d{3})+(?!\d))/g, ',') + (frac ? '.' + frac : '');
    }
  }
  class RelativeTimeFormat {
    constructor(_locale, opts) { this.o = opts || {}; }
    static supportedLocalesOf(locales) { return english(locales); }
    format(v, unit) {
      if (unit === 'day' && this.o.numeric === 'auto' && Math.abs(v) <= 1) return ['yesterday', 'today', 'tomorrow'][v + 1];
      const u = Math.abs(v) === 1 ? unit : unit + 's';
      return v < 0 ? `${-v} ${u} ago` : `in ${v} ${u}`;
    }
  }
  class PluralRules {
    static supportedLocalesOf(locales) { return english(locales); }
    select(n) { return n === 1 ? 'one' : 'other'; }
  }
  g.Intl = { DateTimeFormat, NumberFormat, RelativeTimeFormat, PluralRules };
}

Object.assign(g, { Path2D, ImageData, CanvasRenderingContext2D, HTMLCanvasElement });
})();
