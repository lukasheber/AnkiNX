#include "util/settings.hpp"

#include <borealis.hpp>
#include <fstream>

#include "anki/api.hpp"

namespace settings
{
namespace
{

std::string path() { return anki::dataDir() + "/settings.json"; }

anki::json& values()
{
    static anki::json loaded = [] {
        std::ifstream file(path());
        anki::json v = anki::json::parse(file, nullptr, false);
        return v.is_object() ? v : anki::json::object();
    }();
    return loaded;
}

void save()
{
    std::ofstream file(path(), std::ios::trunc);
    file << values().dump(2);
    if (!file)
        brls::Logger::error("settings: cannot write {}", path());
}

} // namespace

bool autoSync() { return values().value("auto_sync", true); }

void setAutoSync(bool enabled)
{
    values()["auto_sync"] = enabled;
    save();
}

} // namespace settings
