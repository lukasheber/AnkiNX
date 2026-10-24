#pragma once

#include <string>

// Right-to-left text for the card view. NanoVG draws codepoints left to right
// with no shaping, so Arabic-script words (Persian, Arabic, Urdu letters in
// common use) are turned into their contextual presentation forms and put
// in visual order here; Hebrew is reordered only. The order of words on a
// line is litehtml's job (app/patches/litehtml).
namespace rtl
{

// Whether `utf8` has right-to-left letters.
bool hasRtl(const std::string& utf8);

// One word (litehtml draws text a word at a time) in drawing order: letters
// joined, right-to-left runs reversed, digits and Latin kept left to right,
// brackets mirrored. Other text comes back as is (Devanagari, Thai and the
// like are shaped by shaping.cpp instead).
std::string visual(const std::string& utf8);

} // namespace rtl
