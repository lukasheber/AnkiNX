#pragma once

#include <borealis.hpp>

// App-wide actions.
namespace app
{

// Quit, after syncing changes to AnkiWeb if automatic sync is on. Replaces
// borealis' global quit, which exits immediately.
void quit();

// Register + (quit) on a screen, and hide the footer's status (see
// hideStatus). Every activity calls this.
void registerQuit(brls::View* contentView);

// Leave the footer of an AppletFrame to button hints: no battery, Wi-Fi or
// clock (the reviewer needs the room), and the hints centred.
void hideStatus(brls::View* frame);

} // namespace app
