#pragma once

#include <string>

#include "anki/api.hpp"

// App settings, kept in settings.json in the data folder (/switch/ankinx on
// the Switch). Read once, written on every change.
namespace settings
{

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
// key (resources/forest/presets.json), or "" for none, the default.
std::string forest();
void setForest(const std::string& scenery);

// Any other setting (null when unset), e.g. "controls" (util/controls.cpp).
anki::json get(const std::string& key);
void set(const std::string& key, anki::json value);

} // namespace settings
