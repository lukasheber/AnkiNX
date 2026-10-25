# The forest behind the deck list

[Memory Forest](https://github.com/baraklevy20/memory-forest) (MIT), an Anki add-on by
Barak Levy, grows a pixel-art forest from your study history: one tree per day of new cards, grown
by how well you remember them. AnkiNX draws it behind the deck list and on its own screen
(**Settings › About › Forest behind the decks**, off by default). The tree count by the streak, or
**L**, opens it on the whole screen, where each tree says its day and how well its cards are
remembered, and **A** opens them in the browser.

| Part | What it does |
| --- | --- |
| `switch/anki_switch_core/src/forest.rs` | The forest from the collection: the add-on's Python (`study_log`, `forest_data`, `memory`, `milestones`, the Peaceful parts of `events`, the mood from `scene`) in Rust, as the backend's `forest` call |
| `src/shim.js` | The browser the scenes expect: canvases, `Path2D`, `ImageData`, an English `Intl`, a document that only makes canvases |
| `src/runner.js` | The add-on's `AF.mount` without a page: builds a scene at a given size, draws frames, and answers what is at a point (`web/hover.js`) |
| `build.py` | Bundles the two with the add-on's `web/` scripts into `resources/forest/forest.js`, lists its sceneries in `resources/forest/presets.json`, and makes the sample forests in `samples/` |
| `reference.py` | Runs the add-on's own Python on a collection and lists every difference from `forest.rs` |
| `../src/forest/pixel_canvas.cpp` | The canvas: pixel-exact fills, paths, clipping, images and image data, natively |
| `../src/forest/forest_script.cpp` | QuickJS running the bundle |
| `../src/view/forest_view.cpp` | The forest's thread (core 2 on the Switch) and the view that shows its frames |
| `../src/activity/forest.cpp` | The forest on the whole screen |

The add-on is fetched at the commit pinned in `app/library/PINS` (`tools/fetch_frontend_deps.sh`).
After a new pin, or a change in `src/`, run `python3 app/forest/build.py` in WSL; its outputs are
committed. After a new pin, also check that `forest.rs` still matches the add-on's Python:

```sh
make_forest_history <dir>          # a collection with two years of made-up study
forest_dump <dir>/collection.anki2 > dump.json
python3 app/forest/reference.py <dir>/collection.anki2 dump.json
```

(both are `switch/anki_switch_core` examples). Not ported yet: the Wild and Merciless natures (fire,
the asteroid, crows, tall grass, tumbleweeds), remembering the day each animal came, live weather
and Surprise me daily.

`build_desktop/forest_bench resources/forest/forest.js ../forest/samples <out> [w h frames]` (from
`app/`, after `build_desktop.sh`) builds every sample, times its frames and saves a PNG of each.
