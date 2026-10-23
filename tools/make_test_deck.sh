#!/usr/bin/env bash
# Build the AnkiNX test deck: test-deck/AnkiNX Test.apkg. Anki's card types and
# template features, each card saying what to check (see
# switch/anki_switch_core/examples/make_test_deck.rs). Run inside WSL.
# Import the .apkg into Anki desktop (keep "Import any learning progress" on
# for the card states), then sync; the deck reaches the Switch with AnkiWeb.
set -euo pipefail
source "$(dirname "$0")/env.sh"
VENV="$ANKISWITCH_WORK/.venv-tools"
bash "$ANKISWITCH_SRC/tools/py_tools.sh" -c "" # makes the venv (with Pillow)
"$VENV/bin/pip" install -q numpy==2.2.6 soundfile==0.13.1
bash "$ANKISWITCH_SRC/switch/build_core_host.sh" > /dev/null
MEDIA=$(mktemp -d)
trap 'rm -rf "$MEDIA"' EXIT
"$VENV/bin/python" "$ANKISWITCH_SRC/tools/test_deck_media.py" "$MEDIA" > /dev/null
OUT="$ANKISWITCH_SRC/test-deck/AnkiNX Test.apkg"
mkdir -p "$(dirname "$OUT")"
# Checked by importing it into a fresh collection and rendering every card;
# ANKINX_TEST_DECK_KEEP=<dir> keeps that collection (for the desktop preview).
"$ANKISWITCH_SRC/switch/dist-host/make_test_deck" "$MEDIA" "$OUT" ${ANKINX_TEST_DECK_KEEP:+"$ANKINX_TEST_DECK_KEEP"}
