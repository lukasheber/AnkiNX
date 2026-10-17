#!/usr/bin/env bash
# Run the desktop preview against a fresh sample collection, walk through the
# main screens with simulated key presses, and save screenshots.
# Usage: bash app/preview.sh   ->  app/dist/preview/*.png
set -euo pipefail
source "$(dirname "$0")/../tools/env.sh"
ROOT="$ANKISWITCH_WORK/sysroot"
export PATH="$ROOT/usr/bin:$PATH"
export LD_LIBRARY_PATH="$ROOT/usr/lib/x86_64-linux-gnu"
PY="bash $ANKISWITCH_SRC/tools/py_tools.sh"
OUT="$ANKISWITCH_SRC/app/dist/preview"
DATA="$ANKISWITCH_WORK/preview-data"
mkdir -p "$OUT"

# Sample collection + an image for the Geography card.
rm -rf "$DATA"
"$ANKISWITCH_WORK/switch/dist-host/make_sample" "$DATA"
$PY -c "
from PIL import Image, ImageDraw
img = Image.new('RGB', (360, 200), (70, 130, 180))
d = ImageDraw.Draw(img)
d.polygon([(180, 20), (230, 180), (130, 180)], fill=(60, 60, 60))   # a tower, roughly
d.rectangle([0, 170, 360, 200], fill=(90, 160, 90))
img.save('$DATA/collection.media/paris.png')
img = Image.new('RGB', (240, 120), (250, 200, 60))
ImageDraw.Draw(img).ellipse([80, 20, 160, 100], fill=(200, 60, 60))
img.save('$DATA/collection.media/badge.webp', 'WEBP')"
# Like Anki's LaTeX SVGs (dvisvgm): glyphs defined once in <defs>, drawn via <use>.
cat > "$DATA/collection.media/formula.svg" <<'SVG'
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="220" height="60" viewBox="0 0 220 60">
  <defs>
    <path id="bar" d="M0 0h40v8h-40z"/>
    <path id="dot" d="M0 6a6 6 0 1 0 12 0a6 6 0 1 0 -12 0z"/>
  </defs>
  <use xlink:href="#bar" x="10" y="20" fill="#202020"/>
  <use xlink:href="#bar" x="10" y="34" fill="#202020"/>
  <use xlink:href="#dot" x="70" y="24" fill="#2050c0"/>
  <use xlink:href="#bar" x="100" y="27" fill="#c03020"/>
  <use xlink:href="#dot" x="160" y="24" fill="#2050c0"/>
  <use xlink:href="#dot" x="185" y="10" fill="#20a040"/>
</svg>
SVG

cd "$ANKISWITCH_WORK/app"
ANKINX_DATA_DIR="$DATA" ./build_desktop/AnkiNX > "$OUT/app.log" 2>&1 &
APP=$!
trap 'kill $APP 2>/dev/null || true' EXIT

WIN=""
for _ in $(seq 40); do
    WIN=$(xdotool search --name "AnkiNX" 2>/dev/null | head -1) && [[ -n "$WIN" ]] && break
    sleep 0.5
done
[[ -n "$WIN" ]] || { echo "window not found"; cat "$OUT/app.log"; exit 1; }
xdotool windowactivate --sync "$WIN" 2>/dev/null || true

# borealis polls key state once per frame: hold keys briefly.
press() { xdotool keydown "$1"; sleep 0.2; xdotool keyup "$1"; sleep "${2:-1.5}"; }
shot() { xwd -silent -id "$WIN" > /tmp/ankinx.xwd && $PY "$ANKISWITCH_SRC/tools/xwd2png.py" /tmp/ankinx.xwd "$OUT/$1.png"; echo "saved $1.png"; }
# Focus deck row N (0-based) regardless of the current position.
# Mouse button held across frames (1 = touch tap, 2 = X button on desktop).
mclick() { xdotool mousemove --window "$WIN" "$2" "$3"; sleep 0.2; xdotool mousedown "$1"; sleep 0.2; xdotool mouseup "$1"; sleep "${4:-1.5}"; }
goto_deck() { for _ in $(seq 8); do press Up 0.3; done; for _ in $(seq "$1"); do press Down 0.4; done; }

sleep 4
shot 01_decks
# The first synthetic key after the window activates can be dropped (XWayland);
# move focus down and back up so nothing important depends on it.
press Down 0.5; press Up 0.5
press Return 3          # study the first deck (Biology: a cloze)
shot 02_question
press Return            # show answer
shot 03_answer
press Escape            # back to the deck list
press Down 0.5          # Geography (second row)
press Return 3
shot 04_question_geo
press Return
shot 05_answer_geo
press Return 2          # Good -> next card
shot 06_next_card
press Escape
press Down 0.5; press Down 0.5; press Down 0.5   # Japanese::Vocabulary
press Return 3
shot 07_question_ja
press Return
shot 08_answer_ja
press Escape 2
shot 09_decks_after
goto_deck 5             # Media (alphabetical: Biology, Geography, Japanese, Kanji, Vocabulary, Media)
press Return 3
shot 10_media_question
press Return
shot 11_media_answer
press Return 2          # Good -> the hint card
shot 12_hint_hidden
mclick 1 640 250        # tap the hint link (2 lines down)
shot 13_hint_revealed
press Return            # answer side: hints are hidden again, as in Anki
press Escape 2
goto_deck 5
press Return 3          # the hint card again (now in learning), via the controller
mclick 2 640 400        # X button (middle mouse button on desktop)
shot 14_hint_controller
