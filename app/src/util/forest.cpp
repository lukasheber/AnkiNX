#include "util/forest.hpp"

#include <algorithm>
#include <borealis.hpp>
#include <chrono>
#include <ctime>
#include <fstream>

#include "util/settings.hpp"

namespace forest
{

namespace
{

// Python's ordinal of 1970-01-01.
constexpr long EPOCH_ORDINAL = 719163;
// How many holiday weeks the record keeps, so a week already had is never had twice.
constexpr size_t SEASONS_REMEMBERED = 10;

// Days since 1970-01-01 of a civil date, and back (H. Hinnant's algorithms).
long daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

Date civilFromDays(long z)
{
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const long doe = z - era * 146097;
    const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long mp  = (5 * doy + 2) / 153;
    const int d    = (int)(doy - (153 * mp + 2) / 5 + 1);
    const int m    = (int)(mp < 10 ? mp + 3 : mp - 9);
    return { (int)(yoe + era * 400 + (m <= 2)), m, d };
}

bool inWeek(const Scenery& s, const Date& date)
{
    int md = date.month * 100 + date.day;
    return s.fromMonth * 100 + s.fromDay <= md && md <= s.toMonth * 100 + s.toDay;
}

bool unlocked(const Scenery& s, const Date& date) { return !s.seasonal || (!(date < s.first) && inWeek(s, date)); }

// The days up to `date` on which a seasonal scenery comes or goes.
std::vector<Date> seasonChanges(const Date& date)
{
    std::vector<long> days;
    for (const auto& s : sceneries())
        if (s.seasonal)
            for (int year = s.first.year; year <= date.year; year++)
            {
                days.push_back(Date { year, s.fromMonth, s.fromDay }.ordinal());
                days.push_back(Date { year, s.toMonth, s.toDay }.ordinal() + 1);
            }
    std::sort(days.begin(), days.end());
    days.erase(std::unique(days.begin(), days.end()), days.end());
    std::vector<Date> out;
    for (long d : days)
        if (d <= date.ordinal())
            out.push_back(Date::fromOrdinal(d));
    return out;
}

// The scenery whose week `date` is in, if any.
const Scenery* inSeason(const Date& date)
{
    for (const Scenery* s : available(date))
        if (s->seasonal && inWeek(*s, date))
            return s;
    return nullptr;
}

} // namespace

long Date::ordinal() const { return daysFromCivil(year, month, day) + EPOCH_ORDINAL; }

Date Date::fromOrdinal(long n) { return civilFromDays(n - EPOCH_ORDINAL); }

Date Date::today()
{
    std::time_t now = std::time(nullptr);
    std::tm local {};
    localtime_r(&now, &local);
    return { local.tm_year + 1900, local.tm_mon + 1, local.tm_mday };
}

std::string Date::iso() const
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, month, day);
    return buf;
}

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
            Scenery s;
            s.key   = p.value("key", "");
            s.label = p.value("label", "");
            s.look  = { { "environment", p.value("environment", "") }, { "landscape", p.value("landscape", "") },
                        { "landmark", p.value("landmark", "none") },  { "weather", p.value("weather", "clear") },
                        { "time", p.value("time", "day") } };
            const auto& season = p.contains("season") ? p["season"] : anki::json();
            if (season.is_object())
            {
                std::string first = season.value("first", "2000-01-01");
                s.seasonal        = true;
                s.first           = { std::atoi(first.substr(0, 4).c_str()), std::atoi(first.substr(5, 2).c_str()),
                                      std::atoi(first.substr(8, 2).c_str()) };
                s.fromMonth       = season["from"][0];
                s.fromDay         = season["from"][1];
                s.toMonth         = season["to"][0];
                s.toDay           = season["to"][1];
            }
            out.push_back(s);
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

std::vector<const Scenery*> available(const Date& date)
{
    std::vector<const Scenery*> out;
    for (const auto& s : sceneries())
        if (unlocked(s, date))
            out.push_back(&s);
    return out;
}

const Scenery* ofTheDay(const Date& date)
{
    // A seasonal scenery's coming and going changes how many take turns, which
    // could give a day yesterday's again: from that day on they take turns one
    // further along, until the next change.
    long shift = 0;
    for (const Date& day : seasonChanges(date))
    {
        auto before = available(Date::fromOrdinal(day.ordinal() - 1)), turn = available(day);
        if (turn == before || before.empty() || turn.empty())
            continue;
        const Scenery* yesterday = before[(day.ordinal() - 1 + shift) % (long)before.size()];
        shift = turn[day.ordinal() % (long)turn.size()] == yesterday && turn.size() > 1 ? 1 : 0;
    }
    auto turn = available(date);
    return turn.empty() ? nullptr : turn[(date.ordinal() + shift) % (long)turn.size()];
}

const Scenery* forToday()
{
    std::string chosen = settings::forest();
    if (chosen.empty())
        return nullptr;
    // A holiday's week: its scenery once a year, and after it the one from
    // before, unless the setting was changed meanwhile (follow_season).
    Date date        = Date::today();
    anki::json rec   = settings::get("forest_season");
    anki::json done  = rec.is_object() && rec["done"].is_array() ? rec["done"] : anki::json::array();
    anki::json act   = rec.is_object() ? rec.value("active", anki::json()) : anki::json();
    const Scenery* s = inSeason(date);
    std::string tag  = s ? s->key + ":" + std::to_string(date.year) : "";
    std::string change;
    auto gone = [&](const std::string& key) {
        const Scenery* g = scenery(key);
        return g && g->seasonal && !unlocked(*g, date);
    };
    if (act.is_object() && act.value("tag", "") != tag)
    {
        if (chosen == act.value("applied", "") || gone(chosen))
            change = act.value("before", "");
        // (the clock went back before that week: it hasn't happened yet)
        std::string old = act.value("tag", "");
        size_t colon    = old.rfind(':');
        const Scenery* was = colon == std::string::npos ? nullptr : scenery(old.substr(0, colon));
        if (was && was->seasonal &&
            date < Date { std::atoi(old.substr(colon + 1).c_str()), was->fromMonth, was->fromDay })
        {
            anki::json kept = anki::json::array();
            for (const auto& t : done)
                if (t != old)
                    kept.push_back(t);
            done = kept;
        }
        act = nullptr;
    }
    else if (gone(chosen))
        change = sceneries().empty() ? "" : sceneries()[0].key;
    if (s && std::find(done.begin(), done.end(), tag) == done.end())
    {
        std::string before = change.empty() ? chosen : change;
        change             = s->key;
        act                = { { "tag", tag }, { "before", before }, { "applied", change } };
        done.push_back(tag);
    }
    while (done.size() > SEASONS_REMEMBERED)
        done.erase(done.begin());
    anki::json next = { { "done", done }, { "active", act } };
    if (next != rec)
        settings::set("forest_season", next);
    if (!change.empty() && change != chosen)
    {
        settings::setForest(change);
        chosen = change;
    }
    return chosen == DAILY ? ofTheDay(date) : scenery(chosen);
}

std::string grow(const Scenery& scenery)
{
    auto t0          = std::chrono::steady_clock::now();
    anki::json args  = scenery.look;
    args["nature"]   = settings::forestNature();
    // what counts (Settings > Background)
    args["excluded"]           = settings::forestExcluded();
    args["ignore_before"]      = settings::forestStart();
    args["keep_earlier_trees"] = settings::forestKeepEarlier();
    args["keep_suspended"]     = settings::forestKeepSuspended();
    // (the heatmap's weeks start on Sunday, as the console's calendar does in English, Japanese
    // and Brazil's Portuguese; on Monday in Spanish)
    args["week_start"] = brls::Application::getLocale().rfind("es", 0) == 0 ? 1 : 0;
    std::string city = settings::forestCity();
    if (!city.empty())
    {
        // the forecast, if due (MET Norway; a city is looked up once, with Photon):
        // without it the forest keeps the scenery's own weather
        anki::json weather = anki::call("forest_weather", { { "city", city } });
        if (weather["weather"].is_null() && weather["error"].is_string())
            brls::Logger::warning("forest: no live weather: {}", weather["error"].get<std::string>());
        args["city"] = city;
    }
    anki::json data = anki::call("forest", args);
    // the page's words in the app's language, where Memory Forest is translated into it
    std::string locale = brls::Application::getLocale();
    std::string lang   = locale.rfind("pt", 0) == 0 ? "pt-BR" : locale.rfind("es", 0) == 0 ? "es"
                         : locale.rfind("ja", 0) == 0 ? "ja"
                                                     : "en";
    std::ifstream words(std::string(BRLS_ASSET("forest/i18n/")) + lang + ".json");
    anki::json i18n = anki::json::parse(words, nullptr, false);
    if (i18n.is_object())
        data["i18n"] = i18n;
    std::string out = data.dump();
    brls::Logger::info("forest: grown in {} ms ({}, {})",
                       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
                           .count(),
                       scenery.key, args["nature"].get<std::string>());
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
