#pragma once

#include <stdexcept>
#include <string>

#include <borealis/extern/nlohmann/json.hpp>

// Thin C++ layer over the Rust backend's JSON command API
// (switch/anki_switch_core/src/api.rs).
namespace anki
{

using json = nlohmann::json;

// Folder holding collection.anki2, media and backups: /switch/ankinx on the SD
// card. The desktop preview build uses $ANKINX_DATA_DIR (default ./ankinx-data).
std::string dataDir();

class Error : public std::runtime_error
{
  public:
    Error(const std::string& message, std::string kind)
        : std::runtime_error(message)
        , kind(std::move(kind))
    {
    }

    // Machine-readable cause: "auth", "network", "sync", "not_open", ...
    std::string kind;
};

// Run a backend command and return its result, or throw anki::Error.
// Thread-safe. Blocking: run anything that may touch the network or the SD card
// heavily from brls::async().
json call(const std::string& cmd, const json& args = json::object());

} // namespace anki
