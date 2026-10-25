#pragma once

#include <string>
#include <vector>

#include "anki/api.hpp"

// Memory Forest (app/forest): the sceneries it can be drawn in, which one
// today's is, and the forest grown from the collection, as its scenes take it.
namespace forest
{

// A calendar day.
struct Date
{
    int year = 1970, month = 1, day = 1;
    // Days since 1 January of year 1, which is day 1 (Python's toordinal).
    long ordinal() const;
    static Date fromOrdinal(long n);
    static Date today(); // the console's
    bool operator==(const Date& o) const { return year == o.year && month == o.month && day == o.day; }
    bool operator<(const Date& o) const { return ordinal() < o.ordinal(); }
    std::string iso() const;
};

// One of Memory Forest's sceneries (resources/forest/presets.json).
struct Scenery
{
    std::string key, label;
    anki::json look; // environment, landscape, landmark, weather, time
    // A seasonal one (a holiday's): only in its week, from the year it arrives.
    bool seasonal = false;
    Date first;
    int fromMonth = 0, fromDay = 0, toMonth = 0, toDay = 0;
};

// The setting that takes a different scenery each day (presets.DAILY).
constexpr const char* DAILY = "daily";

// Every scenery, in Memory Forest's order.
const std::vector<Scenery>& sceneries();
// The one with this key, or null.
const Scenery* scenery(const std::string& key);
// Those that can be had on `date` (a seasonal one only in its week).
std::vector<const Scenery*> available(const Date& date);
// Surprise me daily's scenery for `date`: they take turns, one a day
// (presets.of_the_day).
const Scenery* ofTheDay(const Date& date);
// The scenery to draw today for the setting (Settings > Background), or null
// for none: Surprise me daily's of the day; in a holiday's week its scenery,
// once a year, and the one from before after it (presets.follow_season,
// which may change the setting).
const Scenery* forToday();

// The forest grown from the collection, drawn in `scenery`, with Nature
// (settings::forestNature): the scenes' data as JSON (src/forest.rs in the
// backend). Blocking (it reads every card and the review log): call from
// brls::async. Throws anki::Error.
std::string grow(const Scenery& scenery);

// What's in that data: how many trees (all of them, the deep forest's too).
int treeCount(const std::string& data);

} // namespace forest
