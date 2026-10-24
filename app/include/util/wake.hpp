#pragma once

#include <borealis.hpp>

// The console waking from sleep (with AnkiNX open). Fired on the UI thread;
// never on the desktop preview.
namespace wake
{

brls::VoidEvent* event();

} // namespace wake
