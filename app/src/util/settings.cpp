#include "util/settings.hpp"

#include <algorithm>
#include <borealis.hpp>
#include <fstream>
#include <iterator>

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

// The settings that go with a collection (its decks, its account): each
// profile has its own. The first profile's are with the others, where they
// all were before there were profiles; the rest are in "profiles", by id.
const char* const PER_PROFILE[] = { "sync_server",         "zoom",                  "forest_excluded", "forest_start",
                                    "forest_keep_earlier", "forest_keep_suspended", "kana_card_types" };
std::string profile;

bool perProfile(const std::string& key)
{
    return !profile.empty() && std::find(std::begin(PER_PROFILE), std::end(PER_PROFILE), key) != std::end(PER_PROFILE);
}

// Where `key` is read from.
const anki::json& in(const std::string& key)
{
    static const anki::json none = anki::json::object();
    if (!perProfile(key))
        return values();
    auto all = values().find("profiles");
    if (all == values().end() || !all->is_object())
        return none;
    auto mine = all->find(profile);
    return mine != all->end() && mine->is_object() ? *mine : none;
}

// Where `key` is written.
anki::json& slot(const std::string& key)
{
    if (!perProfile(key))
        return values()[key];
    anki::json& all = values()["profiles"];
    if (!all.is_object())
        all = anki::json::object();
    anki::json& mine = all[profile];
    if (!mine.is_object())
        mine = anki::json::object();
    return mine[key];
}

void save()
{
    std::ofstream file(path(), std::ios::trunc);
    file << values().dump(2);
    if (!file)
        brls::Logger::error("settings: cannot write {}", path());
}

} // namespace

void setProfile(const std::string& id) { profile = id; }

void forgetProfile(const std::string& id)
{
    auto all = values().find("profiles");
    if (id.empty() || all == values().end() || !all->is_object() || !all->erase(id))
        return;
    save();
}

bool autoSync() { return values().value("auto_sync", true); }

void setAutoSync(bool enabled)
{
    values()["auto_sync"] = enabled;
    save();
}

bool cardScripts() { return values().value("card_scripts", true); }

float tvCardScale() { return values().value("tv_card_scale", 1.25f); }

void setCardScripts(bool enabled)
{
    values()["card_scripts"] = enabled;
    save();
}

std::string theme()
{
    std::string value = values().value("theme", "system");
    return value == "light" || value == "dark" ? value : "system";
}

void setTheme(const std::string& value)
{
    values()["theme"] = value;
    save();
    applyTheme();
}

void applyTheme()
{
    static brls::ThemeVariant console = brls::Application::getPlatform()->getThemeVariant();
    std::string value                 = theme();
    brls::ThemeVariant variant        = value == "light" ? brls::ThemeVariant::LIGHT
                                        : value == "dark" ? brls::ThemeVariant::DARK
                                                          : console;
    brls::Application::getPlatform()->setThemeVariant(variant);
}

std::string forest()
{
    anki::json value = values().value("forest", anki::json());
    return value.is_string() ? value.get<std::string>() : "";
}

void setForest(const std::string& scenery)
{
    values()["forest"] = scenery;
    save();
}

std::vector<int64_t> forestExcluded()
{
    std::vector<int64_t> out;
    const anki::json& value = in("forest_excluded").value("forest_excluded", anki::json::array());
    if (value.is_array())
        for (const auto& id : value)
            if (id.is_number_integer())
                out.push_back(id.get<int64_t>());
    return out;
}

void setForestExcluded(const std::vector<int64_t>& decks)
{
    slot("forest_excluded") = decks;
    save();
}

// Card types typed in hiragana: "<note type id>:<card type ord>".
namespace
{
std::string kanaKey(int64_t notetype, int ord) { return std::to_string(notetype) + ":" + std::to_string(ord); }
} // namespace

bool kanaCardType(int64_t notetype, int ord)
{
    const anki::json& value = in("kana_card_types").value("kana_card_types", anki::json::array());
    if (!value.is_array())
        return false;
    std::string key = kanaKey(notetype, ord);
    for (const auto& entry : value)
        if (entry.is_string() && entry.get<std::string>() == key)
            return true;
    return false;
}

void setKanaCardType(int64_t notetype, int ord, bool on)
{
    anki::json value = in("kana_card_types").value("kana_card_types", anki::json::array());
    if (!value.is_array())
        value = anki::json::array();
    std::string key = kanaKey(notetype, ord);
    anki::json kept = anki::json::array();
    for (const auto& entry : value)
        if (entry.is_string() && entry.get<std::string>() != key)
            kept.push_back(entry);
    if (on)
        kept.push_back(key);
    slot("kana_card_types") = kept;
    save();
}

std::string forestStart()
{
    anki::json value = in("forest_start").value("forest_start", anki::json());
    return value.is_string() ? value.get<std::string>() : "";
}

bool forestKeepEarlier() { return in("forest_keep_earlier").value("forest_keep_earlier", false); }

bool forestKeepSuspended() { return in("forest_keep_suspended").value("forest_keep_suspended", true); }

std::string forestCity()
{
    anki::json value = values().value("forest_city", anki::json());
    return value.is_string() ? value.get<std::string>() : "";
}

std::string forestNature()
{
    anki::json value = values().value("forest_nature", anki::json());
    std::string level = value.is_string() ? value.get<std::string>() : "";
    return level == "wild" || level == "merciless" ? level : "peaceful";
}

anki::json get(const std::string& key) { return in(key).contains(key) ? in(key)[key] : anki::json(); }

void set(const std::string& key, anki::json value)
{
    slot(key) = std::move(value);
    save();
}

} // namespace settings
