# RWKV models

The FSRS-7 fork's RWKV model pairs, f32 weights, as bundled with
[JSchoreels/anki](https://github.com/JSchoreels/anki) (`qt/aqt/rwkv_inference/`,
AGPL-3.0 like Anki), the files its desktop loads (`RWKV_SIZE_MODELS` and
`RWKV_MODEL_FILES` in `qt/aqt/rwkv_model.py`):

- Small (the default, as on the desktop): `sm92/` two-head model (RWKV-Curve:
  intervals, S90; with its answer-time head), `sm98/` Instant-only model
  (RWKV-Instant: recall now).
- Tiny (Settings > RWKV model): `sm106/` two-head model, `sm101/` Instant-only
  model.

Loaded by the backend (`switch/anki_switch_core/src/rwkv.rs`) from romfs.
