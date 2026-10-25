#pragma once

#include <string>
#include <vector>

#include "anki/api.hpp"

// Memory Forest (app/forest): the sceneries it can be drawn in, and the
// forest grown from the collection, as its scenes take it.
namespace forest
{

// One of Memory Forest's sceneries (resources/forest/presets.json).
struct Scenery
{
    std::string key, label;
    anki::json look; // environment, landscape, landmark, weather, time
};

// Every scenery, in Memory Forest's order.
const std::vector<Scenery>& sceneries();
// The one with this key, or null.
const Scenery* scenery(const std::string& key);

// The forest grown from the collection, drawn in `scenery`: the scenes' data
// as JSON (src/forest.rs in the backend). Blocking (it reads every card and
// the review log): call from brls::async. Throws anki::Error.
std::string grow(const Scenery& scenery);

// What's in that data: how many trees (all of them, the deep forest's too).
int treeCount(const std::string& data);

} // namespace forest
