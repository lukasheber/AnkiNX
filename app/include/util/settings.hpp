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

// Any other setting (null when unset), e.g. "controls" (util/controls.cpp).
anki::json get(const std::string& key);
void set(const std::string& key, anki::json value);

} // namespace settings
