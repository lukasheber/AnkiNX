#pragma once

#include <borealis.hpp>

// App-wide actions.
namespace app
{

// Quit, after syncing changes to AnkiWeb if automatic sync is on. Replaces
// borealis' global quit, which exits immediately.
void quit();

// Register + (quit) on a screen, and hide the footer's status (see
// hideStatus). Only the deck list and the login screen quit with +; the
// reviewer and settings go back with B (and call hideStatus instead).
// hidden: no footer hint (the deck list shows + in its header).
void registerQuit(brls::View* contentView, bool hidden = false);

// Leave the footer of an AppletFrame to button hints: no battery, Wi-Fi or
// clock (the reviewer needs the room), the hints centred, and no greyed A hint.
void hideStatus(brls::View* frame);

// Whether the console is in handheld mode (touch available); true off the Switch.
bool isHandheld();

// The same for a dropdown (which has its own frame); returns it, to push.
brls::Dropdown* withoutStatus(brls::Dropdown* dropdown);

} // namespace app
