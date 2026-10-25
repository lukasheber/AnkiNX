# The forest behind the deck list

[Memory Forest](https://github.com/baraklevy20/memory-forest) (MIT), an Anki add-on by
Barak Levy, grows a pixel-art forest from your study history: one tree per day of new cards, grown
by how well you remember them. AnkiNX draws it behind the deck list and on its own screen
(**Settings › Background**, off by default). The tree count by the streak, or **L**, opens it on
the whole screen, with its journal and numbers across the top, where each tree says its day and
how well its cards are remembered, and **A** opens them in the browser. The streak opens the
statistics, the add-on's study heatmap first.

| Part | What it does |
| --- | --- |
| `switch/anki_switch_core/src/forest.rs` | The forest from the collection: the add-on's Python (`study_log`, `forest_data`, `memory`, `milestones`, `events` and `events_state`, `journal`, the heatmap's `_calendar`, the mood from `scene`) in Rust, as the backend's `forest` call |
| `switch/anki_switch_core/src/weather.rs` | Its live weather (`weather.py`): MET Norway's forecast for a city found with Photon |
| `src/shim.js` | The browser the scenes expect: canvases, `Path2D`, `ImageData`, `Intl` in the app's languages, a document that only makes canvases, animation frames the runner drives |
| `src/runner.js` | The add-on's `AF.mount` without a page: builds a scene at a given size, draws frames, plays the asteroid's strike, and answers the app's questions: what is at a point (`web/hover.js`), the caption and journal (`web/caption.js`), the heatmap and a day's tooltip (`web/calendar.js`) |
| `../patches/memory-forest/` | Exposes the heatmap's parts (`calendar.js` keeps them inside its page code) |
| `build.py` | Bundles the two with the add-on's `web/` scripts into `resources/forest/forest.js`, with each scenery's heatmap look; lists its sceneries in `resources/forest/presets.json`; writes the page's words in each of the app's languages to `resources/forest/i18n/`; makes the sample forests in `samples/` |
| `reference.py` | Runs the add-on's own Python on a collection and lists every difference from `forest.rs` |
| `../src/forest/pixel_canvas.cpp` | The canvas: pixel-exact fills, paths, clipping, images and image data, natively |
| `../src/forest/forest_script.cpp` | QuickJS running the bundle |
| `../src/forest/forest_thread.cpp` | Its threads: 1 MB of stack, core 2 on the Switch |
| `../src/view/forest_view.cpp` | The view that shows the forest's frames, behind the deck list or on its own |
| `../src/activity/forest.cpp` | The forest on the whole screen |
| `../src/view/forest_heatmap.cpp` | The heatmap on the statistics |

The add-on is fetched at the commit pinned in `app/library/PINS` (`tools/fetch_frontend_deps.sh`,
which applies the patch). After a new pin, or a change in `src/`, run `python3 app/forest/build.py`
in WSL; its outputs are committed. After a new pin, also check that `forest.rs` still matches the
add-on's Python:

```sh
make_forest_history <dir>          # a collection with two years of made-up study
forest_dump <dir>/collection.anki2 > dump.json
python3 app/forest/reference.py <dir>/collection.anki2 dump.json
```

(both are `switch/anki_switch_core` examples; add a nature, `wild` or `merciless`, to both, and
`800 2 4` to `make_forest_history` for a history away 2 to 4 days ago). What the add-on remembers
between days (the animals that came, the ancient trees, what was shown, the strike played) is kept
in `forest-state.json` beside the collection, its live weather (`weather_probe <city>`) in
`forest-weather.json`. Surprise me daily and the holidays' sceneries are worked out in the app
(`src/util/forest.cpp`).

Not ported: zooming in among the trees (the add-on holds it back from release too), the heatmap's
other views and its settings (By year, Streak, shading by time or pace, marking days), and the
add-on's other settings (decks left out, a start date).

`build_desktop/forest_bench resources/forest/forest.js ../forest/samples <out> [w h frames]` (from
`app/`, after `build_desktop.sh`) builds every sample, times its frames and saves a PNG of each.
