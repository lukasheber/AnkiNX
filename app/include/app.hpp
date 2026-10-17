#pragma once

#include <borealis.hpp>

// App-wide actions.
namespace app
{

// Quit, after syncing changes to AnkiWeb if automatic sync is on. Replaces
// borealis' global quit, which exits immediately.
void quit();

// Register + (quit) on a screen. Every activity calls this.
void registerQuit(brls::View* contentView);

} // namespace app
