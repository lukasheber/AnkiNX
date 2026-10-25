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

anki::json get(const std::string& key) { return values().contains(key) ? values()[key] : anki::json(); }

void set(const std::string& key, anki::json value)
{
    values()[key] = std::move(value);
    save();
}

} // namespace settings
