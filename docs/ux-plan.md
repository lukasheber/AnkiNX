# AnkiNX: UX plan for upcoming features

Draft for discussion. Covers the next work (controls, then the ROADMAP
backlog groups) at the level of screens, buttons and placement. Principles:

- **Controller first, touch everywhere.** Every action has a button; every
  button action can also be tapped. Footer hints always show the current
  buttons (they follow key bindings).
- **One screen, one job.** Few new screens; reuse Switch system patterns
  (side-tab settings like System Settings, sidebars, dialogs).
- **Fast path stays fast.** A on a deck still starts studying immediately.
  New things live one button away, never in the way.
- **Same layout handheld and docked.** Sizes scale with resolution (720p
  handheld, 1080p docked).

## 1. Controls and key bindings (next)

### Default mappings

| Action | Pro Controller / two Joy-Cons | One Joy-Con, sideways |
| --- | --- | --- |
| Show answer | A | A |
| Again / Hard / Good / Easy | ZL / L / R / ZR | SL / Y / A / SR |
| Good (shortcut) | A | A |
| Undo | − | X on the answer side |
| Replay audio | Y | Stick press |
| Show hints / toggle masks | X | X on the question side |
| Card menu (section 2) | Hold Y | Hold stick press |
| Zoom − / + | Left / right stick press | not mapped (touch: pinch) |
| Back to decks | B | B |
| Exit | + | + (when no other action needs it) |

A sideways Joy-Con has 4 face buttons, SL, SR, the stick (with press) and
one of +/−. borealis already rotates the face buttons; AnkiNX detects the
layout (only single Joy-Cons connected) and switches to the right-hand
column automatically. Holding the button that replays audio opens the card
menu (Y on a controller, the stick press on one Joy-Con); a short press still
replays.

### Settings > Controls

```
┌ Settings ───────────────────────────────────────────────────────────┐
│ Account & sync  │  Controls                     Layout: [Automatic ▾]│
│▶Controls        │  ─────────────────────────────────────────────────  │
│ Study           │  Show answer                                    Ⓐ  │
│ Statistics      │  Again                                         ZL  │
│ About           │  Hard                                           L  │
│                 │  Good                                           R  │
│                 │  Easy                                          ZR  │
│                 │  Undo                                           ⊖  │
│                 │  Replay audio                                   Ⓨ  │
│                 │  ...                                               │
│                 │  Reset to defaults                                 │
└─────────────────┴───────────────────────────────────────────────────┘
 Ⓐ Change   Ⓑ Back
```

- Select an action, then press the new button in a small dialog
  ("Press a button for Again… Ⓑ cancels"). If the button is taken, the two
  actions swap and a notice says so.
- Layout: Automatic (detects one Joy-Con), Standard, One Joy-Con. Each layout
  keeps its own bindings.
- Stored in `settings.json`; the reviewer reads them when it opens.

### Settings screen (new home for account options)

`−` on the deck list opens **Settings** (it opens the account dialog today).
Side tabs, like the Switch's System Settings:

- **Account & sync**: AnkiWeb login/logout, automatic sync, custom sync
  server, last sync time.
- **Controls**: above.
- **Study**: card zoom, audio autoplay and speed, answer timer, tap zones.
- **Statistics**: section 6.
- **About**: version, licences, data folder, crash log present.

## 2. In the reviewer: card menu, card info, messages

### Layout

```
┌ Japanese::Kanji                        🚩 ★        3  0  12     ⋯ ┐
│                                                                    │
│                                学                                  │
│                         ─────────────────                          │
│                          learn, study                              │
│                                                                    │
├────────────────────────────────────────────────────────────────────┤
│   [ZL Again <1m]  [L Hard <6m]  [R Good 1d]  [ZR Easy 4d]          │
└ ⊕ Exit  ⊖ Undo  Ⓧ Hints  Ⓨ Replay  Ⓑ Back  Ⓐ OK ───────────────────┘
```

- Header right: flag colour and ★ (marked) when set, the counts, and a ⋯
  icon (tap) for the card menu.
- Flagged cards also get a thin coloured bar on the card's top edge, like
  AnkiDroid.

### Card menu (hold Y, or tap ⋯)

A sidebar sliding in from the right, over the card:

```
                                        ┌─────────────────────────┐
                                        │ Flag                  ▸ │
                                        │ Mark note               │
                                        │ Bury card               │
                                        │ Bury note               │
                                        │ Suspend card            │
                                        │ Suspend note            │
                                        │ Forget card             │
                                        │ Set due date…           │
                                        │ Card info               │
                                        │ Previous card info      │
                                        └─────────────────────────┘
```

- Flag ▸ opens the seven colours. Actions that remove the card (bury,
  suspend, forget) move to the next card and show an Undo-able notice.
- Set due date: numeric keyboard (`0` = today, `3-7` = random range, as in
  Anki).

### Card info

Full screen, B to close: added date, first review, interval, due, ease or
FSRS stability/difficulty/retrievability, review count, lapses, and the
review history as a table (date, rating, interval, time).

### Messages

- **Leech**: dialog "This card is a leech" (and "suspended" if the deck does
  that) when Anki reports it after answering.
- **Learning card due soon**: the "finished" screen shows "Next learning
  card in 4 min" with a countdown, and studying resumes when it's due.
- **Limits reached**: "Today's new card limit reached" with a shortcut to
  custom study.

## 3. Before studying: deck overview, options, custom study

A on a deck still starts studying. **R** (or a long touch on the row) opens
the deck overview:

```
┌ Japanese::Kanji ──────────────────────────────────────────────────┐
│                                                                   │
│        New 20          Learn 3          Due 145                   │
│                                                                   │
│   Deck description (HTML, rendered like a card)…                  │
│                                                                   │
│   ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │
│   │ Ⓐ Study now  │  │ Ⓧ Custom     │  │ Ⓨ Options    │            │
│   └──────────────┘  └──────────────┘  └──────────────┘            │
└ Ⓑ Back ───────────────────────────────────────────────────────────┘
```

- **Options**: a list screen with sections (Daily limits, New cards,
  Lapses, FSRS) using number and switch rows; learning steps edited with the
  keyboard (`1m 10m`). Changes apply to the deck's preset, with a notice
  when other decks share it.
- **Custom study**: a dialog with AnkiDroid's choices (increase new limit,
  increase review limit, review forgotten, review ahead, preview new, by
  card state or tag), then a number or tag field.

## 4. Touch and handheld

- **Tap zones** (Settings > Study): question side, tap anywhere shows the
  answer (as today); answer side, optional swipes: ← Again, ↓ Hard,
  → Good, ↑ Easy. Off by default to avoid accidental grades.
- **Whiteboard**: a pen icon in the header (and a bindable button) puts a
  transparent drawing layer over the card. A thin toolbar on the right edge:
  pen, eraser, clear, hide. Cleared on the next card. Handheld only.
- **Zoom**: stick presses or pinch; remembered per deck.
- **Card elements without touch** (done): D-pad Left/Right highlights what a
  tap could activate (template buttons and options, links, form controls);
  Up/Down then move the highlight, A taps it, B removes it. Without a
  highlight, A and Up/Down keep their usual jobs. Left/Right bound to a
  reviewer action take precedence.
- **Rumble** (Settings > Study): short pulse on grading.

## 5. Audio and timing

- Settings > Study: autoplay on/off, playback speed, "replay question audio
  on answer side".
- **Answer timer** in the header (mm:ss), when the deck's options enable it.
- **Auto advance** (deck option, Anki 23.12+): a small progress ring next to
  the counts shows the time left before the answer shows / the card is
  graded.

## 6. Statistics

Settings > Statistics (all decks) and from the deck overview (one deck):

- **Today**: cards studied, time, again count, correct %.
- **Forecast**: bar chart of due cards for the next 30 days.
- **Heatmap**: calendar grid of the last 6 months, colour by reviews per day.
- **Retention**: true retention for young/mature cards.

Drawn natively (NanoVG) from rslib's statistics data. D-pad Left/Right switch
between panels.

## 7. Settings and platform

- **Language**: follows the console by default; Settings > About can
  override. App strings translated per language file; Anki's own strings
  come from rslib already.
- **Profiles per Switch user**: at start, if several Switch users exist,
  the system user picker appears once; each user gets
  `/switch/ankinx/<user>/`.
- **Docked layout**: at 1080p, card text and buttons scale up.
- **Resume after sleep**: keep the current card; sync when waking if
  automatic sync is on.

## 8. Browser and deck list extras

- **Browser** (read-only), from Settings or the deck overview: search field
  (keyboard), result list (sort field, deck, due), preview, and the card
  menu actions (flag, suspend) on the selected card.
- **Deck list**: collapse all, "only decks with due cards" filter, both in
  the deck overview's ⋯ or Settings > Study.

## Decisions (2026-10-17)

1. Deck overview on **R**; A keeps starting study directly.
2. Card menu on **holding Y** (short press still replays audio).
3. One Joy-Con grading: **SL / Y / A / SR** (Again / Hard / Good / Easy).
