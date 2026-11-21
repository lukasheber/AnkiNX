# Changelog

AnkiNX follows [semantic versioning](https://semver.org/) (0.x while
features are still arriving). The version lives in `VERSION`: the `.nro`
(hbmenu shows it) and `switch/anki_switch_core/Cargo.toml` must match, and
`switch/build_core.sh` checks this.

How to release: update `VERSION` and the crate version, add a section here,
build, then commit and tag (`git tag v0.2.0`).

## 0.12.0 (2026-11-21)

More of the computer's deck options on the console: switch presets, set the
display order and plan easy days.

### Added

- **Switching presets.** The **Options preset** row in **Deck options** now
  chooses the deck's preset, and **Manage presets** adds, clones, renames or
  removes one, or saves it to all subdecks.
- **Display order.** **Deck options › Display Order**: the order new cards
  are gathered and sorted in, where new cards and learning cards go among
  reviews, and the review sort order.
- **Easy days.** **Deck options › Easy Days**: fewer reviews on the days you
  choose (Minimum, Reduced or Normal for each weekday).

### Changed

- **Thumb keyboard.** Each half is one key column narrower, easier to reach
  while holding the console.
- **Loading message.** When the next card takes more than a moment (after a
  sync or a change of options), the study screen says so instead of staying
  blank.

### Good to know

- Removing a preset makes the next sync a one-way sync, as on the computer.

## 0.11.1-fsrs7 (2026-11-21, experimental)

A fix for large collections that froze the console, and more of the
computer's deck options: display order, easy days and switching presets.

### Fixed

- **Freeze when opening a collection.** Some collections that use RWKV froze
  the console while loading or when switching to their profile, with the HOME
  menu slow or frozen too. They now open in moments.

### Added

- **Display order.** **Deck options › Display Order**: the order new cards
  are gathered and sorted in, where new cards and learning cards go among
  reviews, and the review sort order.
- **Easy days.** **Deck options › Easy Days**: fewer reviews on the days you
  choose (Minimum, Reduced or Normal for each weekday), the load balancer, and
  review fuzz with a preview of the ranges it gives.
- **Switching presets.** The **Options preset** row now chooses the deck's
  preset, and **Manage presets** adds, clones, renames or removes one, or saves
  it to all subdecks.

### Changed

- **Thumb keyboard.** Each half is one key column narrower, easier to reach
  while holding the console.
- **Loading message.** "Loading the next card" appears only when the next
  card takes more than 1.6 seconds, so it no longer flashes.

### Good to know

- Removing a preset makes the next sync a one-way sync, as on the computer.
- In-app updates don't offer this build.

### How to install

Copy `AnkiNX-fsrs7.nro` to `/switch/` on the SD card, replacing the previous
one, and start it from the homebrew menu.

## 0.11.0-fsrs7 (2026-11-21, experimental)

An experimental build for people who use the FSRS-7 fork of Anki on their
computer: FSRS-7 and RWKV scheduling on the Switch, with the same results
as the fork's desktop app.

### Added

- **FSRS-7.** Decks set to FSRS-7 on the computer are scheduled with FSRS-7
  on the console too, including its same-day intervals.
- **RWKV-Instant and RWKV-Curve.** Decks set to RWKV pick their reviews and
  intervals as on the computer. The first time, RWKV reads your whole review
  history (about two and a half minutes for 75,000 reviews); until then,
  studying those decks shows **RWKV is getting ready** and starts by itself.
  Later starts are much quicker (about 15 seconds).
- **Prediction model.** Small (the default) or Tiny, in **Deck options ›
  RWKV › Advanced**. Tiny uses less memory. Changing it reads the history
  again.
- **Optimize on the console.** **Deck options › FSRS › Optimize Current
  Preset** fits the FSRS parameters to your reviews and shows how much
  better they fit before you use them.
- **More FSRS settings.** Dynamic DR, the optimizer's search filters,
  reschedule cards on change, and the health check.

### Changed

- **Deck options as on the computer.** The same sections, in the same order,
  with the computer's wording. Choose the **Review scheduler** (and, with
  RWKV-Instant, its **Fallback** for syncing); the console shows the
  settings of the scheduler you chose.
- **Loading message.** When the next card takes a moment (after a sync or a
  change of options), the study screen says so instead of staying blank.

### Good to know

- Install it as `AnkiNX-fsrs7.nro` next to the regular `AnkiNX.nro`: both
  use the same profiles and collections.
- Studying an FSRS-7 or RWKV deck in the regular AnkiNX, official Anki or
  AnkiDroid drops the extra FSRS-7 data of the cards you answer. Stick to
  this build and the fork's desktop app for those decks.
- If there isn't enough memory for RWKV, those decks are scheduled with
  FSRS, as on phones without RWKV, and a notice says so.
- In-app updates don't offer this build. A regular release from 0.12.0 on
  would replace it.

## 0.10.0 (2026-11-20)

Type your answers without letting go of the console.

### Added

- **Thumb keyboard.** An optional on-screen keyboard for type-answer cards,
  made for typing while you hold the console in both hands. Choose it in
  **Settings › Study › Keyboard for typed answers**: split in two halves
  under your thumbs, or stacked at the right or left edge for one thumb.
  The card stays visible while you type.
- **Accents and symbols.** Hold a letter for its accented forms (é, ñ, ü,
  ç…), and tap **123** for digits and punctuation.
- **Buttons work too.** B deletes, Y types a space, + confirms and −
  cancels; the D-pad and A pick keys.

The thumb keyboard is used in handheld mode. On a TV, the system keyboard
opens as before.

## 0.9.3 (2026-11-16)

Faster reviews on decks with their own fonts.

### Fixed

- **Pause before the answer on decks with their own fonts.** Decks that
  bring a font (such as a Japanese font in WaniKani decks) had it read from
  the SD card again for every card side, so showing the answer could hang
  for a moment, and memory use grew as you reviewed. Each font is now read
  once per session.

## 0.9.2 (2026-11-16)

Two fixes: media files with non-English names, and icon fonts on cards.

### Fixed

- **Media sync failed on some file names.** A media file whose name has
  characters such as Japanese ones could stop the sync with "Failed to
  write … No such file or directory": the console refused to create it.
  AnkiNX now stores these files under a plain-letter name on the SD card,
  while your cards and AnkiWeb keep the real one.
- **Icons from a deck's own font showed as a square.** Decks such as
  WaniKani draw some radicals with an icon font. AnkiNX did not load the
  font when the deck's styling listed its files in the usual way for old
  browsers, so the icon was missing. These now show as in Anki on a
  computer.

### Good to know

- On the SD card, files with such names begin with `^u^` and are not named
  as on a computer. To take this media to a computer, sync through AnkiWeb
  rather than copying the folder.

## 0.9.1 (2026-11-15)

A fix for media sync on large collections, which could stop partway and
never finish.

### Fixed

- **Media sync stuck on large collections.** With around 17,000 media files
  or more, the sync failed at the same place every time ("Failed to write …
  No such file or directory"): the media folder had reached the SD card's
  limit for files in one folder. AnkiNX now spreads media over several
  folders, so collections of any size sync. Files already on the console
  stay where they are.
- **Interrupted media sync picks up where it stopped.** Closing AnkiNX or
  losing the connection no longer throws away up to 1,000 downloaded files.
- **The console stays awake while media syncs.** It no longer goes to sleep
  in the middle of a long download.

### Good to know

- New media is stored in `collection.media.d` beside `collection.media` in
  your profile's folder. If you copy your media to a computer by hand, move
  the files from its subfolders back into `collection.media`.

## 0.9.0 (2026-11-15)

Profiles arrive, as in Anki desktop, and the review screen gives your cards
more room. Media sync now shows its progress, and cards with SVG images or
comment-heavy templates display as they do on the computer.

### Profiles

- **Several collections on one console.** **Settings › Account & sync ›
  Profile** lists your profiles: A switches, X renames, Y deletes, and you
  can add new ones. Each has its own collection, media, backups and AnkiWeb
  login. The last one used opens at start.

### Studying

- **More room for cards.** The bottom bar is gone from the review screen;
  its button hints now sit on the right of the top bar.
- **Sharper SVG images.** SVG pictures on cards are drawn at the size they
  appear, so they stay crisp at any zoom.

### Sync

- **Media sync progress.** While media downloads in the background, the
  sync icon turns and shows the files on the console and how far it has
  got. The text disappears when it's done.
- **Clearer message.** After a sync, AnkiNX now says "Collection synced",
  since media may still be on its way.

### Fixed

- **Type-in answers on some shared decks.** Decks whose templates close
  comments with `--!>` (WaniKani Ultimate, for one) no longer mark correct
  typed answers as wrong.
- **Faster cards with large media folders.** Missing images and fonts are
  looked up only once per study session, instead of on every card.

## 0.8.0 (2026-10-30)

Your custom scheduling code from Anki desktop now runs on the Switch, and
a deck's statistics can open the cards of any day on its heatmap.

### Studying

- **Custom scheduling.** The JavaScript in your deck options (set in Anki
  desktop under **Advanced › Custom scheduling**) now runs when you study,
  as it does in Anki and AnkiDroid, so your answer buttons and intervals
  follow it on the Switch too. **Deck options › Advanced** shows whether
  your collection has some, and can turn it off (this deletes the code, as
  the reset button in Anki desktop does).

### Fixed

- **See the day's cards in a deck's statistics.** On a deck's heatmap,
  the D-pad (or a tap) now picks a day, shows its date and reviews, and A
  opens that day's cards in the browser, as clicking a day in Anki desktop
  does. Elsewhere A is greyed out instead of doing nothing.
- **Undo stays in your study session.** Pressing undo repeatedly no longer
  goes back into a deck you studied earlier, which could open one of its
  cards in the middle of your current deck.

## 0.7.0 (2026-10-25)

Introducing Memory Forest: an optional pixel-art forest behind your deck
list that grows as you study. It is a port of the Memory Forest add-on for
Anki by Barak Levy.

### Memory Forest

Off by default: turn it on in **Settings › Background**.

- **A tree for each day you learn new cards.** Trees grow as you remember
  their cards. Ponds mark the days you were away, and animals arrive as you
  reach milestones.
- **Explore the forest.** Tap the tree count next to your streak, or press
  **L**, to see it full screen. Select a tree to see its day and how well
  you remember its cards, then press **A** to open them.
- **Sceneries.** Choose one of seven, or let AnkiNX pick a new one each
  day. Halloween and Christmas sceneries appear in their week.
- **Live weather.** Optionally show the current weather and time of day in
  your city.
- **Nature.** Choose what missed days do to your forest: nothing
  (Peaceful), smoke and fire (Wild) or an asteroid strike (Merciless).
- **What counts.** Leave decks out of the forest, or start it from a chosen
  date.

### Statistics

- Tap your streak to open the statistics for all your decks, starting with
  a study heatmap. Select a day to see what you studied, then press **A** to
  open those cards.

## 0.6.0 (2026-10-24)

More cards look as they do in Anki: grid layouts, canvas drawings and text
fields in templates work, and Hindi, Thai, Arabic, Persian, Urdu and Hebrew
are shaped as in print. Also new: answering with a tap, a theme setting and
syncing after the console wakes. Verified on hardware.

Cards:

- CSS grid: cards laid out with `display: grid` show their columns
  (fixed, `fr`, `repeat()` including auto-fill/auto-fit, `minmax()`), with
  spans, gaps and alignment. They showed as one column.
- Canvas: templates that draw on a `<canvas>` (shapes, lines, curves,
  gradients, text, images) show their drawing.
- Text fields in templates show their placeholder (in grey), and tapping
  one (or highlighting it and pressing A) opens the keyboard; the text goes
  into the field, and the template sees it as typed, followed by Enter.

Languages:

- Hindi and Thai as in print: letters join into conjuncts (स्त), vowel
  signs go where they belong (the ि of हिंदी before ह) and Thai marks stack
  over their letters.
- Arabic, Persian, Urdu and Hebrew: letters join as the font intends
  (ligatures included), and harakat and Hebrew vowel points sit where they
  belong.
- Both shaped with HarfBuzz, using the Noto fonts for these scripts.

Studying:

- Tap the answer (Settings › Study, off by default): in handheld mode, a tap
  on the left third of the answer is Again and on the right third Good, or
  the card is four columns from Again to Easy.
- Theme (Settings › About): the console's, or always light or dark.
- Waking the console with AnkiNX open: the card you were studying stays,
  and the deck list refreshes its counts and syncs (automatic sync on) once
  Wi-Fi is back, or as soon as you return to it.

Fixed:

- Coming back from studying or Settings while a sync starts, the deck list's
  highlight no longer draws over the "Syncing…" dialog.

Also a new README, with current screenshots.

## 0.5.0 (2026-10-24)

Card templates' JavaScript now runs as in a browser, cards look closer to
Anki desktop, and two crashes are fixed. Verified on hardware, with a new
test deck (`test-deck/`) that has a card for each feature saying what to
check.

- JavaScript in cards: templates get a real browser page (a standard DOM
  with events, the real jQuery 3.7.1, Anki desktop's reviewer functions),
  instead of the partial DOM written for AnkiNX. Shared templates such as
  multiple-choice note types work as on the desktop. Card pages are prepared
  while you read the answer, so the next card opens quickly.
- Touch: taps that land just beside a link, button or option still reach
  it, and words inside a sentence can be tapped (for templates that react to
  them). Checkboxes, radio buttons, text fields and buttons in templates are
  drawn as such.
- Card display:
  - animated GIFs play;
  - Thai and Hindi (Devanagari) text shows, with bundled fonts;
  - chemistry formulas (`\ce{...}`, `\pu{...}`, as in Anki's MathJax);
  - subscript and superscript, `<small>`, `<mark>`, monospace text for code
    and serif text where a card asks for them;
  - box shadows, rotated, scaled and skewed elements, and badges placed on a
    word.
- Night mode: text left in night mode's light colour on a light background
  the card paints itself (a highlight, a code block) is drawn dark, so it
  stays readable. Anki and AnkiDroid show it light on light.
- Fixed: a crash after the console had been asleep with AnkiNX open.
- Fixed: a crash on opening when a dialog appeared during start-up.
- The app writes a log to `/switch/ankinx/ankinx.log` (the previous run's
  is `ankinx.old.log`), to send with a problem report.
- Update dialogs: the download question no longer shows the release notes
  (they ran off the screen as one line), and "please wait" dialogs (the
  download's progress, syncing, backing up) take the focus, so the
  highlighted row underneath no longer draws over them.

## 0.4.0 (2026-10-19)

Study streak and time estimate, backups you can restore on the console,
in-app updates, larger card text on a TV, and a clearer whiteboard.
Verified on hardware; updating from inside the app is first exercised by
updating to this release from 0.3.0 builds that have the update check.

- Deck list: the header shows about how long today's cards will take
  ("19 cards due · about 12 min", from your pace over the last 30 days),
  and the status line a study streak on the right (days in a row with
  reviews; today counts once you have studied).
- Settings > Collection: Check Database (Anki's), Back up now, and Restore
  from backup (a list of the backups by date; the current collection is
  backed up first and the next sync asks which side to keep, as in Anki).
  Backups made in the same second no longer overwrite each other.
- Updates: the app checks GitHub for a newer release when it opens (this
  can be turned off in Settings > About) and says so; Settings > About >
  Updates downloads the new .nro with progress, and it replaces the old one
  when AnkiNX exits.
- On a TV (docked), card text is a size larger by default (Settings >
  Study > On a TV: same, 125%, 150%), on top of each deck's zoom; it
  follows docking and undocking while studying.
- Whiteboard easier to recognise: the pen and bin icons (which read as
  "edit card" and "delete card") are now labelled pills, "Draw" with a brush
  and "Clear"; Draw fills blue and the card gets a blue outline while
  drawing, and the first time a hint says that you can draw anywhere on the
  card (scrolling is paused meanwhile).
- Replay on the answer side plays the question's audio first, then the
  answer's, as Anki does, unless the deck has "Skip question when replaying
  answer" (that option was editable but not applied).

## 0.3.0 (2026-10-18)

Deck overview and options, custom study, statistics, a card browser, maths,
card scripts, right-to-left text, translations and a reworked main screen.
Verified on hardware.

- Deck list: the table is only as wide as the deck names need, centred, so
  the counts sit close to the names; the header shows "N cards due" under
  AnkiNX; Y reads Expand or Collapse for the highlighted deck and is greyed
  out on decks without subdecks; A reads Study.
- Deck list header: Sync (X), Settings (−) and Exit (+) on the right, as
  button glyph and icon, tappable; a dot on Sync while changes are not on
  AnkiWeb. The footer keeps what applies to the highlighted deck.
- Deck list status line, fixed under the list (it doesn't scroll away):
  "Studied N cards in X minutes today (Ys/card)" for the whole collection,
  as under Anki's deck list ("No cards studied today yet" before the first
  review), and the AnkiWeb account on the left (cloud icon and username,
  cut with "…" before it reaches the study line).
  The header reads "No cards due" when nothing is due.
- Deck overview redesigned: today's counts and the totals in a panel with
  Study now (which holds the focus), the other actions as outlined buttons
  below, then the description; the group is centred on the screen. Works
  in the dark theme too (the "cards due" line was hard to read there).
- Slimmer top bar on every screen (72 px instead of 88).
- Finished screen: "Studied N cards in X minutes today (Ys/card)" under the
  congratulations, as in Anki.

- Maths in cards (`\(...\)` inline, `\[...\]` display, as Anki's MathJax):
  typeset natively with Typst (LaTeX converted by MiTeX), drawn as SVG on the
  text baseline, cached on the SD card. The `.nro` grows to about 76 MB.
- Footer shows "Toggle masks" for X on Image Occlusion answers.
- Controls: one Joy-Con held sideways is detected and grades with SL / Y / A /
  SR; every reviewer button can be rebound per layout.
- Settings screen on − (Account & sync, Controls, Study, About), replacing the
  account dialog.
- JavaScript in card templates (QuickJS-ng with a small DOM): scripts run on
  load, onclick and listeners on tap, timers, state kept from question to
  answer as in Anki. Can be turned off in Settings > Study.
- Card scripts written for jQuery work without it (a built-in subset of
  jQuery), as do `pycmd("ans")` / `pycmd("ease3")`, checkbox and radio taps,
  `<label>` taps and more selectors (`:nth-of-type`, `:checked`, ...).
  Tested with a shared multiple-choice note type.
- Deck overview: R on a deck (or tap ⋯ at the end of its row) shows
  today's counts and the deck's description, with Study now (A), Custom
  study (X) and Options (Y).
- Deck options: limits, learning steps, intervals, lapses and leeches,
  burying, audio, FSRS and desired retention (or SM-2's ease and
  multipliers), saved to the deck's preset at once; a shared preset says so.
  "Don't play audio automatically" is honoured while studying.
- Custom study: raise today's limits, review forgotten cards, review ahead,
  preview new cards, or study by card state and tag; the session starts at
  once.
- Answer timer (deck option) in the reviewer's header, and auto advance as
  in Anki 23.12+: started from the card menu, it shows the answer and then
  answers (or buries, or reminds) after the deck's seconds, waiting for the
  audio if the deck says so. Both are set in deck options.
- Statistics: today's study, a 30-day forecast, a 26-week review heatmap,
  true retention and card counts, for a deck (R on its overview) or the
  whole collection (Settings > Statistics).
- Fix: crash when going back from a card preview to the browser (the list
  was rebuilt, deleting the row that still had focus). The same pattern is
  removed from deck options (FSRS switch) and Settings > Controls (button
  capture and reset): rows are now updated in place.
- Translations: Portuguese (Brazil), Spanish and Japanese, following the
  console's language or Settings > About > Language.
- Read-only card browser (L on the deck overview): any Anki search, the
  matching cards with deck, flag and suspension, a preview, and flag /
  suspend / unsuspend from it.
- Self-hosted sync server: Settings > Account > Sync server.
- Whiteboard (handheld): the pen in the reviewer's header lets you draw over
  the card; the bin clears it, and each new card starts clean.
- Settings > Study: swipe to answer (← Again, ↓ Hard, → Good, ↑ Easy) and
  rumble when answering, both off by default.
- Card zoom (stick presses) is remembered per deck; Redo in the card menu.
- Dropdown menus show only their button hints too (no clock).
- + quits only from the deck list (it syncs first when automatic sync is
  on); the reviewer and settings have no + (B goes back).
- Footer: only button hints, centred (no battery, Wi-Fi or clock); X shows
  only on cards with hints (or masks to toggle); A only shows the answer
  and has no hint (the Show Answer button shows it); Y reads "Hold: card
  options" on sides without audio.
- Replay buttons work in templates with broken HTML (an unclosed `<div`
  that swallows the field's play link), as they do in Anki; and a replay
  started as another sound ended is no longer silenced.
- Image Occlusion: X toggles the masks on "hide one, guess one" answers too
  (Anki's back template always has the toggle).
- Card menu: hold the replay audio button (Y; the stick press on one
  Joy-Con) or tap ⋯ in the header to flag the card, mark the note, bury,
  suspend, forget, set the due date, or see the card's (or the previous
  card's) info and review history. The header shows the flag and ★ mark.
- Messages as in Anki: a leech dialog after answering; on the finished
  screen, when the next learning card is due (studying resumes then), and
  whether daily limits held cards back or some were buried.
- Card buttons without touch: the D-pad (Left/Right, then Up/Down) highlights
  what a tap could activate (template buttons and options, links such as
  audio replay, form controls); A taps it, B removes the highlight. With
  nothing highlighted, A and Up/Down work as before.
- Card fonts from the media folder (`@font-face` in the note type's CSS, as
  shared decks use for Japanese, Arabic or Persian fonts): TrueType and
  OpenType, with their bold and italic faces; missing glyphs come from the
  system font.
- Right-to-left text: Persian and Arabic letters are joined as they should
  be (they showed as separate letters, or not at all: the Switch's fonts
  have no Arabic, so DejaVu Sans is bundled as a fallback), and words and
  lines read right to left, also with `dir="rtl"`. Hebrew is reordered too.
- Furigana (`{{furigana:...}}` and `<ruby>` in cards) shows the reading
  above the text, as in Anki, instead of after it.
- Inline `<svg>` in cards, and `data:` image URIs (such as SVG icons in CSS
  backgrounds), are drawn.
- Fix: script-edited cards no longer garble characters whose UTF-8 encoding
  contains byte A0 (such as "à" or Persian letters).

## 0.2.0 (2026-10-17)

First version verified on hardware for sync and study.

- AnkiWeb login screen (email and password rows that each open the system
  keyboard), replacing two keyboards in a row, which lost the input box on
  hardware.
- Sync fixes found on hardware:
  - Full download works: collections from AnkiWeb and Anki desktop are saved
    in WAL mode, which this SQLite build rejected (error 26); the SQLite VFS
    now accepts them.
  - Horizon can't reopen a file that is open for writing: full download and
    backups no longer do that.
  - Rust thread-locals no longer run out of libnx TLS slots ("out of TLS
    keys" abort when loading a collection or syncing).
  - Clean exit: the backend shuts down before the network does (no crash
    dialog after +).
- Automatic sync when the app opens, after studying and before + exits; on
  by default, switchable in the account menu (-). Verified on hardware (open,
  after studying); exit with + only syncs when something is still unsynced.
- Type-in-the-answer cards (`{{type:...}}`): answer box with the system
  keyboard, Anki's comparison on the answer side.
- Image Occlusion cards: masks drawn natively; X toggles them on the answer
  side. Verified on hardware.
- "Missing media file" notice instead of silent audio.
- Backups (.colpkg) written after closing, restorable in Anki.
- New app icon.
- `crash.log` in the data folder records Rust panics.

## 0.1.0

First builds: deck list, review with Anki's scheduler, hints, audio, images
(PNG/JPEG/WebP/SVG), manual sync.
