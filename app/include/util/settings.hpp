#pragma once

// App settings, kept in settings.json in the data folder (/switch/ankinx on
// the Switch). Read once, written on every change.
namespace settings
{

// Sync with AnkiWeb when the app opens, after studying, and before + exits,
// like Anki desktop's default and AnkiDroid's "Automatic synchronization".
bool autoSync();
void setAutoSync(bool enabled);

} // namespace settings
