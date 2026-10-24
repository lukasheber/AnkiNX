# The forest behind the deck list

[Memory Forest](https://github.com/baraklevy20/memory-forest) (MIT), an Anki add-on by
Barak Levy, grows a pixel-art forest from your study history: one tree per day of new cards. AnkiNX
runs its scene scripts unchanged to draw that forest behind the deck list (**Settings › About ›
Forest behind the decks**, off by default).

| Part | What it does |
| --- | --- |
| `src/shim.js` | The browser the scenes expect: canvases, `Path2D`, `ImageData`, a document that only makes canvases, a clock |
| `src/runner.js` | The add-on's `AF.mount` without a page: builds a scene at a given size and draws frames |
| `build.py` | Bundles the two with the add-on's `web/` scripts into `resources/forest/forest.js`, and makes the sample forests in `resources/forest/samples/` |
| `../src/forest/pixel_canvas.cpp` | The canvas: pixel-exact fills, paths, clipping, images and image data, natively |
| `../src/forest/forest_script.cpp` | QuickJS running the bundle |
| `../src/view/forest_view.cpp` | The forest's thread (core 2 on the Switch) and the view that shows its frames |

The add-on is fetched at the commit pinned in `app/library/PINS` (`tools/fetch_frontend_deps.sh`).
After a new pin, or a change in `src/`, run `python3 app/forest/build.py` in WSL; its outputs are committed.

**For now** the forest is the add-on's made-up test forest (`fake_forest.py`) in the chosen scenery.
Growing it from the collection's own review history means porting the add-on's Python
(`study_log.py`, `forest_data.py`, `memory.py`, `events.py`, `milestones.py`, `scene.py`).

`build_desktop/forest_bench resources/forest/forest.js resources/forest/samples <out> [w h frames]`
(from `app/`, after `build_desktop.sh`) builds every sample, times its frames and saves a PNG of each.
