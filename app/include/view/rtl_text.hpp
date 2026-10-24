#pragma once

#include <string>
#include <vector>

// Right-to-left text for the card view, a word at a time (litehtml draws text
// a word at a time; the order of words on a line is litehtml's job,
// app/patches/litehtml). A word is cut into pieces in drawing order: runs of
// Arabic or Hebrew letters, which HarfBuzz shapes and draws right to left
// (shaping.cpp), and the rest (digits, Latin, punctuation) drawn as is.
namespace rtl
{

// Whether `utf8` has right-to-left letters.
bool hasRtl(const std::string& utf8);

// Whether a word is laid out right to left: right-to-left letters, or
// Arabic-Indic digits (punctuation around them goes the right-to-left way).
bool rtlWord(const std::string& utf8);

struct Piece
{
    std::string text;
    // true: Arabic or Hebrew letters with their marks and joiners, in logical
    // order, to shape right to left; false: text to draw left to right as it
    // is (already in visual order, brackets mirrored).
    bool shaped;
};

// A right-to-left word in drawing order, left to right: right-to-left runs
// reversed, digits and Latin kept left to right, brackets mirrored.
std::vector<Piece> pieces(const std::string& utf8);

} // namespace rtl
