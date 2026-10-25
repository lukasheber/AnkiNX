/* AnkiNX: Memory Forest's scene runner without a page. It builds and draws a scene as the
 * add-on's AF.mount does (web/core.js: build() and frame()), onto a canvas the app asks for by
 * size, and leaves out what belongs to the add-on's page: the caption, the heatmap, the
 * pointer and its tooltips, zooming in, the strike played on mounting. The app calls
 * globalThis.__forest (app/src/forest/forest_script.cpp). */
(function () {
'use strict';
const AF = window.AnkiForest;
const { layer, animates } = AF.u;
const { HORIZON, GROUND_BOTTOM } = AF.GEOM;
const BASE_W = 320;  // (core.js: everything is sized against a canvas this wide)

let env = null, canvas = null, marked = null, lifted = null;

function build(json, W, H) {
  const data = JSON.parse(json);
  // (each animal's name and why it came, which the add-on's Python adds: app/forest/build.py)
  const words = globalThis.__forestVisitors || {};
  data.visitors = (data.visitors || []).map(v => Object.assign({}, words[v.key] || {}, v));
  marked = null; lifted = null;
  AF.i18n.use(data.i18n);
  AF.LOOP = null;
  const engine = AF.engines.pixel, layout = AF.layout(data.trees);
  canvas = document.createElement('canvas');
  canvas.width = W; canvas.height = H;
  env = { W, H, u: W / BASE_W, hor: Math.round(H * HORIZON), mood: data.mood, data, layout, engine, bot: GROUND_BOTTOM };
  env.theme = AF.theme(data.mood, data.stats, data.events);
  env.still = !animates(data);
  engine.prepare(env);
  env.deepAlpha = layout.deepAlpha == null ? 1 : layout.deepAlpha;
  env.deep = data.merged && env.deepAlpha > 0 ? AF.deepForest(data.merged, env) : null;
  env.placed = layout.items.map(it => Object.assign({ it }, AF.place(it, W, H, env))).sort((a, b) => a.y - b.y);
  env.puddles = AF.findPuddles(env);
  AF.placeVisitors(env);
  env.sky = engine.sky(env);
  const [land, lg] = layer(W, H); env.land = land; env.lg = lg;
  engine.steps(env).forEach(s => s(lg));
  engine.post(env);
  env.fx = AF.fx.init(env);
  env.g = canvas.getContext('2d');
  env.glowing = (data.anniversaries || []).map(idx => env.placed.find(q => !q.it.pond && q.it.index === idx)).filter(Boolean);
  return canvas._n;
}

/* an anniversary tree glows (core.js drawGlow) */
const GLOW_DISCS = [0.85, 0.55], GLOW_SPARKLES = 6;
function drawGlow(g, t) {
  env.glowing.forEach(p => {
    const h = AF.STAGE_H[p.it.stage] * env.u * p.s, cx = Math.round(p.x), cy = Math.round(p.y - h * 0.55);
    g.save(); g.globalCompositeOperation = 'lighter';
    g.fillStyle = `rgba(255,226,150,${0.06 + 0.03 * Math.sin(t * AF.u.cyc(1.4))})`;
    for (const k of GLOW_DISCS) {
      const r = Math.max(2, Math.round(h * k)), disc = new Path2D();
      for (let dy = -r; dy <= r; dy++) { const w = Math.round(Math.sqrt(r * r - dy * dy)); if (w) disc.rect(cx - w, cy + dy, w * 2, 1); }
      g.fill(disc);
    }
    g.restore();
    for (let i = 0; i < GLOW_SPARKLES; i++) {
      const a = i / GLOW_SPARKLES * Math.PI * 2 + t * AF.u.cyc(0.3), on = Math.sin(t * AF.u.cyc(2.2) + i * 1.7) > 0.2;
      if (!on) continue;
      g.fillStyle = '#ffe6a0';
      g.fillRect(Math.round(cx + Math.cos(a) * h * 0.6), Math.round(cy + Math.sin(a) * h * 0.5), 1, 1);
    }
  });
}

function frame(t) {
  if (!env || !env.g) return;
  const g = env.g, engine = env.engine;
  env.t = t;
  g.clearRect(0, 0, env.W, env.H);
  g.drawImage(env.sky, 0, 0);
  if (env.fx) AF.fx.back(g, env, t);
  AF.events.run('back', g, env, t);
  AF.drawFlyers(g, env, t);
  if (engine.frameBack) engine.frameBack(g, env, t);
  g.drawImage(env.fx && env.fx.flash && env.landLit ? env.landLit : env.land, 0, 0);
  engine.frame(g, env, t);
  AF.events.run('grass', g, env, t);
  AF.drawVisitors(g, env, t);
  drawGlow(g, t);
  AF.events.run('front', g, env, t);
  if (env.fx) AF.fx.front(g, env, t);
  if (env.quantize) env.quantize(g, env.W, env.H);
  drawMarker(g);
}

/* ---------- picking (web/hover.js, without a pointer or a page) ---------- */

// a tree's box, as hover.js boxOf has it
function boxOf(p) {
  const h = AF.STAGE_H[p.it.stage] * env.u * p.s, w = Math.max(h * 0.8, 4 * env.u);
  return { x0: p.x - w / 2, x1: p.x + w / 2, y0: p.y - h, y1: p.y + env.u };
}
// a tooltip's HTML (AF.tips) as lines of text: the first is its title
const lines = html => html.split('<br>').map(l => l.replace(/<[^>]*>/g, '').replace(/&lt;/g, '<').replace(/&gt;/g, '>')
  .replace(/&quot;/g, '"').replace(/&amp;/g, '&')).filter(Boolean);
function described(hit) {
  if (hit.visitor) return { kind: 'visitor', lines: lines(AF.tips.visitor(hit.visitor)) };
  if (hit.deep) return { kind: 'deep', from: hit.deep.from_ago, to: hit.deep.to_ago, lines: lines(AF.tips.deep(hit.deep)) };
  if (hit.pond) return { kind: 'pond', lines: lines(AF.tips.pond(hit.p.it)) };
  const b = boxOf(hit.p);
  return { kind: 'tree', ago: hit.p.it.ago, x: hit.p.x, top: b.y0, lines: lines(AF.tips.tree(hit.p.it, '')) };
}
/* What is at (x, y), in the canvas' pixels: an animal, a tree, a pond or the deep forest, with its
 * tooltip; null for nothing */
function pick(x, y) {
  if (!env) return null;
  const inBox = b => b && x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1;
  for (const b of env.visitorBoxes || []) if (inBox(b)) return described({ visitor: b.v });
  for (let i = env.placed.length - 1; i >= 0; i--) {
    const p = env.placed[i];
    if (p.it.pond) {
      if (!p.it.first) continue;
      const b = AF.pondBox(env, p);
      if (((x - b.cx) / (b.w / 2)) ** 2 + ((y - b.cy) / (b.h / 2)) ** 2 <= 1) return described({ p, pond: true });
      continue;
    }
    if (inBox(boxOf(p))) return described({ p });
  }
  for (const b of (env.deep && env.deep.boxes) || []) if (inBox(b)) return described({ deep: env.deep.merged });
  return null;
}
/* Every tree, for moving between them with the controller: its day and where it stands */
function trees() {
  return env ? env.placed.filter(p => !p.it.pond).map(p => ({ ago: p.it.ago, x: p.x, y: p.y })) : [];
}
/* The tree of `ago` lifted out of the forest with a marker over it (null: none), and what it says */
function mark(ago) {
  marked = ago; lifted = null;
  const p = env && ago != null && env.placed.find(q => !q.it.pond && q.it.ago === ago);
  return p ? described({ p }) : null;
}
// (hover.js drawMarker: the tree outlined, a little arrow over it)
function drawMarker(g) {
  const p = marked != null && env.placed.find(q => !q.it.pond && q.it.ago === marked);
  if (!p) return;
  if (!lifted) {
    const { h, hzq } = AF.pixel.sizeOf(p, env), spr = AF.pixel.sprite(p.it, h, hzq, env);
    const [cv, cg] = layer(spr.width + 2, spr.height + 2);
    for (const [x, y] of [[0, 1], [2, 1], [1, 0], [1, 2]]) cg.drawImage(spr, x, y);
    cg.globalCompositeOperation = 'source-in'; cg.fillStyle = '#fff8e0'; cg.fillRect(0, 0, cv.width, cv.height);
    cg.globalCompositeOperation = 'source-over'; cg.drawImage(spr, 1, 1);
    lifted = cv;
  }
  const cv = lifted, x = Math.round(p.x - (cv.width - 2) / 2) - 1, y = Math.round(p.y - (cv.height - 2) + 1) - 1;
  g.save();
  if (env.flipLight) { g.translate(x + cv.width, y); g.scale(-1, 1); g.drawImage(cv, 0, 0); } else g.drawImage(cv, x, y);
  g.restore();
  const mx = Math.round(p.x), my = Math.round(y - 3);
  g.fillStyle = 'rgba(20,20,20,.7)'; g.fillRect(mx - 3, my - 1, 7, 1);
  g.fillStyle = '#fff8e0';
  for (let r = 0; r < 3; r++) g.fillRect(mx - (2 - r), my + r, 5 - 2 * r, 1);
}

/* The app's questions, by name, with JSON in and out (ForestScript::query) */
const QUERIES = { pick: a => pick(a.x, a.y), trees: () => trees(), mark: a => mark(a.ago) };
function query(name, args) {
  const fn = QUERIES[name];
  return JSON.stringify(fn ? fn(JSON.parse(args || '{}')) : null);
}

globalThis.__forest = { build, frame, query, busy: () => Boolean(env && AF.busy(env)) };
})();
