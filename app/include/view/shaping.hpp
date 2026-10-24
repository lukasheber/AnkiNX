#pragma once

#include <string>
#include <vector>

struct NVGcontext;

// Scripts that need shaping for the card view. NanoVG draws one glyph per
// codepoint, which is enough for Latin, Cyrillic, Greek, CJK and (with
// rtl_text.cpp) Arabic and Hebrew, but not for scripts whose letters
// reorder, join into conjuncts or stack marks: Devanagari (Hindi, Marathi,
// Nepali, Sanskrit) and Thai. Their runs are shaped with HarfBuzz against the
// bundled Noto fonts and drawn from the glyph outlines as NanoVG paths.
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
