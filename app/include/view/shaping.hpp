#pragma once

#include <string>
#include <vector>

struct NVGcontext;

// Scripts that need shaping for the card view. NanoVG draws one glyph per
// codepoint, which is enough for Latin, Cyrillic, Greek and CJK, but not for
// scripts whose letters join, reorder, form conjuncts or stack marks:
// Devanagari (Hindi, Marathi, Nepali, Sanskrit), Thai, Arabic (with Persian
// and Urdu letters) and Hebrew. Their runs are shaped with HarfBuzz against
// the bundled Noto fonts and drawn from the glyph outlines as NanoVG paths.
// Right-to-left words are cut into runs by rtl_text.cpp.
namespace shaping
{

struct Run
{
    std::string text;
    bool shaped; // false: draw with NanoVG as usual
};

// Whether `utf8` has letters of a shaped script.
bool needed(const std::string& utf8);

// `utf8` cut into shaped and plain runs, in logical order.
std::vector<Run> runs(const std::string& utf8);

// Advance width of a shaped run at `size` px.
float width(const std::string& run, float size);

// Fill a shaped run with the current fill colour, its baseline starting at (x, y).
void draw(NVGcontext* vg, const std::string& run, float x, float y, float size);

} // namespace shaping
