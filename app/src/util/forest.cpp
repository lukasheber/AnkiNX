#include "util/forest.hpp"

#include <borealis.hpp>
#include <chrono>
#include <fstream>

namespace forest
{

const std::vector<Scenery>& sceneries()
{
    static const std::vector<Scenery> all = [] {
        std::vector<Scenery> out;
        std::ifstream file(BRLS_ASSET("forest/presets.json"));
        anki::json list = anki::json::parse(file, nullptr, false);
        if (!list.is_array())
        {
            brls::Logger::error("forest: no presets.json");
            return out;
        }
        for (const auto& p : list)
        {
            anki::json look = { { "environment", p.value("environment", "") },
                                { "landscape", p.value("landscape", "") },
                                { "landmark", p.value("landmark", "none") },
                                { "weather", p.value("weather", "clear") },
                                { "time", p.value("time", "day") } };
            out.push_back({ p.value("key", ""), p.value("label", ""), look });
        }
        return out;
    }();
    return all;
}

const Scenery* scenery(const std::string& key)
{
    for (const auto& s : sceneries())
        if (s.key == key)
            return &s;
    return nullptr;
}

std::string grow(const Scenery& scenery)
{
    auto t0         = std::chrono::steady_clock::now();
    std::string out = anki::call("forest", scenery.look).dump();
    brls::Logger::info("forest: grown in {} ms",
                       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
                           .count());
    return out;
}

int treeCount(const std::string& data)
{
    anki::json d = anki::json::parse(data, nullptr, false);
    if (!d.is_object() || !d.contains("stats"))
        return 0;
    return d["stats"].value("trees", 0);
}

} // namespace forest
