# AnkiNX test deck

`AnkiNX Test.apkg`: Anki's card types and the template features it supports,
in eleven subdecks. Every card ends with a grey **Check:** line saying what
you should see, so the deck doubles as a checklist on the console.

## Getting it onto the Switch

1. In Anki desktop: File > Import, choose `AnkiNX Test.apkg`. Keep "Import any
   learning progress" on (it carries the learning, due, suspended, buried and
   flagged cards of subdeck 11).
2. Sync with AnkiWeb, then sync on the Switch (X on the deck list).

Without AnkiWeb, import it in Anki desktop and copy the collection to the SD
card as the main README describes.

## Contents

| Subdeck | What it tests |
| --- | --- |
| 01 Basic note types | Basic; and reversed; optional reversed (with and without); type in the answer (plain, accents, Japanese) |
| 02 Cloze | several clozes, hint, same number twice, nested, in lists and tables, in maths, an image, type-in cloze |
| 03 Image Occlusion | hide all / hide one; rectangle, ellipse, polygon; grouped, rotated and coloured masks; a text label |
| 04 Template features | `{{#...}}`/`{{^...}}` sections, a card made only when a field is filled, special fields (Tags, Deck, Subdeck, Type, Card, CardFlag), filters (hint, text, furigana, kana, kanji, tts), `type:nc` |
| 05 Formatting | text styles, lists, tables, headings, quotes, code, entities, a long scrolling card, emoji |
| 06 Languages | furigana, Chinese with pinyin, Korean, Arabic/Persian/Hebrew right to left, Russian, Greek, Thai, Hindi, Vietnamese, combining accents |
| 07 Media | PNG, JPEG, animated GIF, BMP, WebP, SVG, a large image, sized images; MP3, OGG, Opus, FLAC, WAV, two sounds in a row, sound on the answer only, missing files |
| 08 Maths | MathJax inline and display, matrices, aligned equations, sums/integrals/limits, chemistry (`\ce`), a `[latex]` image |
| 09 Styling (CSS) | per-card classes, a card font from the media folder, flexbox, grid, gradient, shadow, transform, CSS variables, night-mode rules, absolute positioning |
| 10 JavaScript | onclick, globals from question to answer, timers, jQuery, addEventListener, checkboxes, localStorage, layout measurement, canvas, `pycmd`, script and JSON files from the media folder, a shared multiple-choice template |
| 11 Card states | new, learning, due today, due in 30 days, suspended, buried, the seven flags, marked, leech |

Some checks name known gaps on the console (CSS grid, shadows and
transforms, canvas, text-to-speech): seeing them fail there is expected,
and they show what is left for card display parity.

## Rebuilding it

`bash tools/make_test_deck.sh` (inside WSL) builds the deck with Anki's own
code (`switch/anki_switch_core/examples/make_test_deck.rs`, media from
`tools/test_deck_media.py`), imports it into a fresh collection and renders
every card to check it, then writes `AnkiNX Test.apkg` here.
