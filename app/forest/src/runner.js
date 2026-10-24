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

let env = null, canvas = null;

function build(json, W, H) {
  const data = JSON.parse(json);
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
}

globalThis.__forest = { build, frame, busy: () => Boolean(env && AF.busy(env)) };
})();
