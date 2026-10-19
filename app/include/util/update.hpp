#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Updates from the project's GitHub releases: a check when the app opens
// (Settings > About can turn it off), and on request a download of the new
// .nro next to the running one, which replaces it when the app exits.
namespace update
{

struct Info
{
    bool checked = false; // a check has answered
    bool newer   = false; // the latest release is newer than this build
    std::string latest;   // "0.4.0"
    std::string url;      // the .nro asset (empty if the release has none)
    std::string notes;    // release notes (Markdown)
    uint64_t size = 0;
};

// Where the running .nro is (argv[0] from hbmenu), for the download.
void setSelfPath(const std::string& path);

const Info& info();

// Whether a download can be installed here (on the Switch, path known).
bool canInstall();

// Once per run, if enabled: check in the background and notify when a newer
// version exists. `onResult` runs on the UI thread after a check.
void checkInBackground(std::function<void()> onResult = nullptr);

// Settings > About: check now (with a wait dialog), then offer it.
void checkNow(std::function<void()> onResult);

// Ask to download the newer version (with its notes), download it with
// progress, and say it will be installed on exit.
void offer();

// At exit, after the backend has shut down: replace the .nro with a
// finished download, if there is one.
void applyPending();

} // namespace update
