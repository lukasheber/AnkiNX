#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "anki/api.hpp"

// App settings, kept in settings.json in the data folder (/switch/ankinx on
// the Switch). Read once, written on every change.
namespace settings
{

// The profile in use (the backend's id; "" for the first one, and until the
// collection is open). Each profile has its own of the settings that go with
// a collection: the sync server, each deck's zoom, and what Memory Forest
// counts (decks left out, first day, earlier and suspended trees).
void setProfile(const std::string& id);
// Drop a deleted profile's settings.
void forgetProfile(const std::string& id);

// Sync with AnkiWeb when the app opens, after studying, and before + exits,
// like Anki desktop's default and AnkiDroid's "Automatic synchronization".
bool autoSync();
void setAutoSync(bool enabled);

// Run JavaScript in card templates (Settings > Study); on by default.
bool cardScripts();

// Card text size on a TV (docked), on top of the deck's zoom: 1, 1.25, 1.5.
float tvCardScale();
void setCardScripts(bool enabled);

// Light or dark (Settings > About): "system" follows the console, else
// "light" or "dark". applyTheme() sets it for borealis; the first call, at
// start, remembers the console's own theme.
std::string theme();
void setTheme(const std::string& value);
void applyTheme();

// Memory Forest behind the deck list (Settings > Background): a scenery's
// key (resources/forest/presets.json), "daily" (Surprise me daily), or ""
// for none, the default.
std::string forest();
void setForest(const std::string& scenery);
// What missing days cost the forest: "peaceful" (the default), "wild" or
// "merciless" (Memory Forest's Nature).
std::string forestNature();
// The city whose live weather the forest shows (and whose sun sets its time
// of day), or "" for the scenery's own weather and hour, the default.
std::string forestCity();
// What the forest counts (Memory Forest's Decks tab): the decks left out
// (each with its subdecks); the day it starts ("" for your first day of
// study) and whether the trees from before it stay; whether suspended cards
// keep their trees (the default).
std::vector<int64_t> forestExcluded();
void setForestExcluded(const std::vector<int64_t>& decks);
std::string forestStart();
bool forestKeepEarlier();
bool forestKeepSuspended();

// The card types whose typed answers are hiragana (Settings › Study ›
// Hiragana keyboard): romaji typed there becomes hiragana. Per profile (the
// ids are the collection's).
bool kanaCardType(int64_t notetype, int ord);
void setKanaCardType(int64_t notetype, int ord, bool on);

// Any other setting (null when unset), e.g. "controls" (util/controls.cpp).
anki::json get(const std::string& key);
void set(const std::string& key, anki::json value);

} // namespace settings
