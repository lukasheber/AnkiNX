#pragma once

#include <functional>

// AnkiWeb login and sync UI flows (dialogs, keyboard, progress), modelled on
// AnkiDroid. Completion callbacks run on the UI thread.
namespace sync_flow
{

enum class Mode
{
    // The user asked (X): log in first if needed; errors in a dialog.
    Manual,
    // Automatic sync when the app opens or after studying: skipped when not
    // logged in; errors as a notification; a full sync still asks which side
    // to keep.
    Automatic,
    // Automatic sync before exiting: like Automatic, but a full sync is left
    // for next time, since it can't be done unattended.
    Exit,
};

// Sync the collection and resolve "full sync required" conflicts with the
// user (see Mode). `onFinished` runs afterwards whether or not the sync
// succeeded, so the caller can refresh its view.
void start(std::function<void()> onFinished, Mode mode = Mode::Manual);

// Automatic sync if enabled in settings, logged in and, unless `always`, the
// collection has changes AnkiWeb doesn't have yet. Otherwise just runs
// `onFinished`. `always` also pulls changes made on other devices (app open).
void autoSync(std::function<void()> onFinished, Mode mode, bool always);

// Open the AnkiWeb login screen. `onDone` runs once it closes.
void login(std::function<void(bool loggedIn)> onDone);

void logout();

} // namespace sync_flow
