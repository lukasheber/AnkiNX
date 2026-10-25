/* AnkiNX: Memory Forest's scene runner without a page. It builds and draws a scene as the
 * add-on's AF.mount does (web/core.js: build() and frame()), onto a canvas the app asks for by
 * size; plays the asteroid's strike on it as the add-on's page does; and answers the app's
 * questions: what is at a point (web/hover.js), and the caption and journal (web/caption.js), for
 * the app to show. Left out: the heatmap and zooming in. The app calls globalThis.__forest
 * (app/src/forest/forest_script.cpp); it hears of a strike seen through __native.event. */
(function () {
'use strict';
const AF = window.AnkiForest, native = globalThis.__native;
const { layer, animates } = AF.u;
const { HORIZON, GROUND_BOTTOM } = AF.GEOM;
const BASE_W = 320;  // (core.js: everything is sized against a canvas this wide)

let env = null, canvas = null, marked = null, lifted = null;
// the weathers that leave the night sky clear (journal.py CLEAR_SKIES)
const CLEAR_SKIES = ['clear', 'after_rain'];

/* A line of the add-on's Python (forest.rs `line`: its id, a plural's count, its placeholders,
 * those in `dates` ISO dates) written in the page's language, as its t and tn do */
function written(entry) {
  if (!entry) return '';
  const values = Object.assign({}, entry.values), dates = values.dates || [];
  delete values.dates;
  for (const k of dates) values[k] = AF.i18n.fmtDate(values[k]);
  return entry.n != null ? AF.tn(entry.id, entry.n, values) : AF.t(entry.id, values);
}

/* ---------- the caption (web/caption.js, as text) ---------- */
const WEATHER_NAMES = { clear: 'caption.clear_sky', cloudy: 'caption.cloudy', fog: 'caption.fog', rain: 'caption.rain',
  storm: 'caption.thunderstorm', snow: 'caption.snow', deep_winter: 'caption.deep_winter', after_rain: 'caption.after_the_rain' };
function weatherLine(m) {
  const v = { weather: WEATHER_NAMES[m.weather] ? AF.t(WEATHER_NAMES[m.weather]) : m.weather, city: m.city, temp: m.temp != null ? Math.round(m.temp) : null };
  if (m.city && v.temp != null) return AF.t('caption.weather_weather_in_city_temp', v);
  if (m.city) return AF.t('caption.weather_weather_in_city', v);
  if (v.temp != null) return AF.t('caption.weather_weather_temp', v);
  return AF.t('caption.weather_weather', v);
}
/* The line under the forest: the journal, and the numbers, each with what its tooltip says */
function caption() {
  if (!env) return null;
  const data = env.data, s = data.stats, m = data.mood, items = [];
  if (s.trees) items.push([AF.tn('caption.n_trees', s.trees), AF.t('caption.one_tree_for_each_day_you')]);
  if (data.merged) {
    const mg = data.merged;
    items.push([AF.tn('caption.n_in_the_deep_forest', mg.count),
      AF.tn('caption.your_trees_from_from_to_to', mg.cards, { from: AF.u.fmtDate(mg.from_date), to: AF.u.fmtDate(mg.to_date) })]);
  }
  if (s.ancient) items.push([AF.tn('caption.n_ancient', s.ancient, { n: s.ancient }), AF.t('caption.trees_whose_cards_you_will_likely')]);
  if (s.streak) items.push([AF.tn('caption.n_day_streak', s.streak, { n: s.streak }), AF.t('caption.days_you_studied_in_a_row')]);
  items.push(...AF.events.collect('caption', data, animates(data)));  // the asteroid, on its way or struck
  if (m.source === 'real') items.push([weatherLine(m), AF.t('caption.live_weather_from_met_norway_cc')]);
  return {
    journal: data.journal || '',
    items: items.map(([text, tip]) => ({ text: lines(String(text)).join(' '), tip: lines(String(tip || '')).join(' ') })),
    credit: data.credit ? AF.t('caption.weather_by_met_norway') : '',
  };
}

/* The page the asteroid's strike plays on (events/asteroid.js playStrike, overlayOn): the scene,
 * whose canvas is the runner's, and the canvas laid over it while the strike plays */
let root = null, overlay = null;
function pageRoot() {
  const scene = {
    querySelector: sel => (sel === 'canvas.af-canvas' ? canvas : null),
    appendChild(c) { overlay = c; c.remove = () => { if (overlay === c) overlay = null; }; return c; },
  };
  return { querySelector: sel => (sel === '.af-scene' ? scene : sel === 'canvas.af-canvas' ? canvas : null) };
}
// (the strike shows the forest it took, then the forest now, each through AF.mount)
AF.mount = (r, data) => { scene(data, canvas.width, canvas.height); r.afEnv = env; };

function build(json, W, H) {
  const data = JSON.parse(json);
  if (!canvas) canvas = document.createElement('canvas');
  if (canvas.width !== W || canvas.height !== H) { canvas.width = W; canvas.height = H; }
  root = pageRoot(); overlay = null;
  prepare(data);
  scene(data, W, H);
  root.afEnv = env;
  // the latest strike plays now if it hasn't been seen (asteroid.js mount: it mounts the
  // forests it shows itself), and the app is told once it has
  AF.events.first('mount', root, data, {}, animates(data), () => native.event('strikeSeen', String(data.strike.seen)));
  return canvas._n;
}

/* What the add-on's Python adds to a page's data, written in its language (app/forest/build.py) */
function prepare(data) {
  AF.i18n.use(data.i18n);
  // (each animal's name and why it came, and the journal's line, which the add-on's Python
  // writes: app/forest/build.py)
  const words = globalThis.__forestVisitors || {};
  const named = v => {
    const w = words[v.key];
    return Object.assign(w ? { name: AF.t(w.name), stayed: AF.t(w.stayed), came: AF.t(w.came) } : {}, v);
  };
  data.visitors = (data.visitors || []).map(named);
  if (data.calendar) data.calendar.animals = (data.calendar.animals || []).map(named);
  const starry = data.mood.time === 'night' && CLEAR_SKIES.includes(data.mood.weather);
  data.journal = written((data.journalLines || {})[starry ? 'starry' : 'plain']);
  data.credit = data.mood.source === 'real';
}

/* Lay out and draw what stays still of a forest (core.js build) */
function scene(data, W, H) {
  marked = null; lifted = null;
  AF.LOOP = null;
  const engine = AF.engines.pixel, layout = AF.layout(data.trees);
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
  // (what waits for this refresh first: a strike playing draws its next step, and may mount a forest)
  globalThis.__animationFrames(performance.now());
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
  if (overlay) g.drawImage(overlay, 0, 0);  // the strike, over the forest
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

/* ---------- the study heatmap (web/calendar.js, through its parts: app/patches/memory-forest) ---------- */

// the forest's data when only the heatmap is wanted (load: no scene is built), and the heatmap's
// history and canvases as last drawn
let loaded = null, heat = null;
const canvases = {};
function load(json) {
  loaded = JSON.parse(json);
  prepare(loaded);
  heat = null;
  return true;
}
const forestData = () => (env ? env.data : loaded);

/* The heatmap's Timeline, `page` months back (negative) or on, `months` of them, on a page of
 * colour `bg` (dark or not): drawn by the add-on's paint into canvases.heatmap (the days) and
 * canvases.heatmapAbove (the trophies, the goal and the animals, which its page shows on hover),
 * with where each day and label stands, in the heatmap's pixels */
function heatmap(a) {
  const data = forestData(), HP = AF.heatmapParts, cal = data && data.calendar;
  if (!HP || !cal || !cal.days || !cal.days.length) return null;
  if (!cal.look) cal.look = (globalThis.__heatmapLooks || {})[data.mood.special] || null;
  HP.setup(cal);
  if (!heat || heat.data !== data) heat = { data, H: HP.history(cal) };
  const H = heat.H, win = HP.windowOf(H, 'timeline', a.page || 0, null, a.months || 12);
  const paper = { bg: a.bg || [45, 45, 45], dark: a.dark !== false };
  const drawn = HP.paint(H, win, HP.sceneOf(data, env), paper);
  canvases.heatmap = drawn.cv; canvases.heatmapAbove = drawn.above;
  heat.drawn = drawn;
  // each month's name over its first week (its year with it in January), and three weekdays
  const months = [];
  for (const c of drawn.cells) {
    const d = new Date(c.ms);
    if (c === drawn.cells[0] || d.getUTCDate() === 1) {
      const m = d.getUTCMonth();
      months.push({ x: c.x, label: m === 0 ? `${AF.i18n.monthName(0, 'short')} ${d.getUTCFullYear()}` : AF.i18n.monthName(m, 'short') });
    }
  }
  const weekStart = cal.weekStart == null ? 1 : cal.weekStart, cell = HP.cell();
  const weekdays = [1, 3, 5].map(r => ({ y: HP.TOP + r * cell + 1, label: AF.i18n.weekdayName((weekStart + r) % 7, 'short') }));
  return {
    w: drawn.W, h: drawn.Hh, cell, tile: HP.TILE, top: HP.TOP, rows: HP.ROWS,
    cells: drawn.cells.map(c => [c.o, c.x, c.y]),
    months, weekdays,
    brackets: drawn.brackets.map(b => ({ x0: b.x0, x1: b.x1, live: b.live, text: AF.tn('heatmap.n_days', b.len) })),
    from: win.from, to: win.to, first: H.first, ahead: H.ahead,
  };
}

/* What a day's tooltip says (calendar.js, the pointer on a day): its date, its reviews or the cards
 * due, its tree, where it stands in its streak, the animals that came */
function heatmapDay(a) {
  const data = forestData();
  if (!heat || !data) return null;
  const H = heat.H, HP = AF.heatmapParts, o = a.o, ms = H.today + o * HP.DAY_MS, n = H.n(o);
  const num = v => AF.i18n.num(v), lasted = HP.lasted;
  const reviewsIn = (k, secs) => (secs ? AF.tn('heatmap.n_reviews_in_time', k, { time: lasted(secs) }) : AF.tn('heatmap.n_reviews', k));
  const due = k => AF.tn('heatmap.n_cards_due', k);
  const withWeekday = (t, year) => AF.i18n.dayIn(t, year ? { weekday: 'long', month: 'short', day: 'numeric', year: 'numeric' } : { weekday: 'long', month: 'short', day: 'numeric' });
  const off = o <= 0 && !n && H.free.has(o);
  const run = o <= 0 && (n || off) ? H.runOf(o) : null;
  const t = H.time(o), still = { due: num(H.dueToday) };
  const what = o < H.first ? AF.t('heatmap.before_your_first_review')
    : o > H.ahead ? AF.t('heatmap.too_far_ahead_to_count')
    : o > 0 ? (n ? due(n) : AF.t('heatmap.nothing_due_yet'))
    : o === 0 && !n && H.dueToday ? AF.tn('heatmap.not_studied_yet_n_cards_due', H.dueToday)
    : o === 0 && H.dueToday ? (t ? AF.tn('heatmap.n_reviews_so_far_in_time', n, Object.assign({ time: lasted(t) }, still))
      : AF.tn('heatmap.n_reviews_so_far_due_still', n, still))
    : n ? reviewsIn(n, t)
    : H.daysOff.has(o) ? AF.t('heatmap.not_studied_a_day_off') : off ? (o === 0 ? AF.t('heatmap.nothing_due_today') : AF.t('heatmap.nothing_was_due'))
    : H.pond.has(o) ? AF.t('heatmap.not_studied_a_pond_in_your') : o === 0 ? AF.t('heatmap.not_studied_yet') : AF.t('heatmap.not_studied');
  let day = 0;
  for (let d = run ? run.from : 1; d <= o; d++) if (H.n(d)) day++;
  const total = run ? run.days : 0, at = { day: num(day) };
  const where = run && off ? (run === H.current ? AF.tn('heatmap.doesnt_break_your_n_day_streak', total) : AF.tn('heatmap.didnt_break_a_n_day_streak', total))
    : !run || (total === 1 && run !== H.current) ? ''
      : run === H.current ? (day === total ? AF.t('heatmap.day_day_of_your_streak', at) : AF.tn('heatmap.day_day_of_your_n_day', total, at))
        : day === total && total >= HP.SHOWN_STREAK ? AF.tn('heatmap.last_day_of_a_n_day', total)
          : total >= HP.SHOWN_STREAK ? AF.tn('heatmap.day_day_of_a_n_day', total, at) : AF.tn('heatmap.day_day_of_n_in_a', total, at);
  const near = o >= -1 && o <= 1;
  const head = near ? AF.t('heatmap.near_date', { near: AF.i18n.nearDay(o), date: withWeekday(ms, false) }) : withWeekday(ms, true);
  const tree = o < H.first ? null : (data.trees || []).find(tr => -tr.ago === o) || (data.calendar.planted || []).map(([ago, k]) => ({ ago, n: k })).find(tr => -tr.ago === o);
  const out = [head, what];
  if (tree) out.push(AF.tn('heatmap.planted_tree_n_new_cards', tree.n));
  if (where) out.push(where);
  for (const animal of H.animals.get(o) || []) if (animal.came) out.push(animal.came);
  // the Anki search its click opens (study_log.calendar_search): the cards answered that day, or
  // due; "" for none (calendar.js `opens`)
  const search = o < H.first || o > H.ahead ? '' : o < 0 ? (n ? `prop:rated=${o}` : '')
    : o > 0 ? (n ? `prop:due=${o}` : '') : n ? 'rated:1' : H.dueToday ? 'is:due -deck:filtered -is:suspended -is:buried' : '';
  return { lines: out.map(l => lines(String(l)).join(' ')), search };
}

/* The app's questions, by name, with JSON in and out (ForestScript::query) */
const QUERIES = { pick: a => pick(a.x, a.y), trees: () => trees(), mark: a => mark(a.ago), caption: () => caption(),
  heatmap: a => heatmap(a), heatmapDay: a => heatmapDay(a) };
function query(name, args) {
  const fn = QUERIES[name];
  return JSON.stringify(fn ? fn(JSON.parse(args || '{}')) : null);
}

// (a strike playing wants every frame there is)
globalThis.__forest = { build, load, frame, query, canvases, busy: () => Boolean(overlay || (root && root.afPlaying) || (env && AF.busy(env))) };
})();
