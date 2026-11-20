# RWKV models (Tiny)

The FSRS-7 fork's RWKV "Tiny" pair, int8 weights, as bundled with
[JSchoreels/anki](https://github.com/JSchoreels/anki) and
[kuma3-anki-core](https://github.com/Pedrubik2000/kuma3-anki-core)
(`qt/aqt/rwkv_inference/`), AGPL-3.0 like Anki:

- Tiny (the default): `sm106/` two-head model (RWKV-Curve: intervals, S90),
  `sm101/` Instant-only model (RWKV-Instant: recall now).
- Small (for testing; `"rwkv_size": "small"` in settings.json): `sm92/`
  two-head model with its answer-time head, `sm98/` Instant-only model.

Loaded by the backend (`switch/anki_switch_core/src/rwkv.rs`) from romfs.
