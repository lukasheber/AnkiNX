#pragma once

#include <string>

// Romaji to kana, as Japanese input methods and WaniKani's answer box
// (WanaKana's IME mode) convert while you type: "ka" か, "shi"/"si" し,
// "kya" きゃ, "kka" っか, "tchi" っち, "nn"/"n'" ん, "n" before a consonant
// ん, "-" ー, small kana with "x" or "l" ("xtsu" っ). Capitals give katakana.
namespace kana
{

// The kana for `romaji`, and what is left of it: letters that may still
// become kana with the next ones (a lone "n", "ky", "ts"...). With `final`,
// nothing is left: a lone "n" is ん, other letters stay as they are.
// Characters that aren't romaji letters are copied.
struct Converted
{
    std::string kana;
    std::string rest;
};
Converted convert(const std::string& romaji, bool final);

// Convert the run of romaji at the end of `text` (what a keyboard has typed
// so far); `final` when typing it is done (Space, OK).
std::string convertTail(const std::string& text, bool final);

// The whole of `text`, as typed on a keyboard without conversion (the
// system keyboard): every run of romaji, finished.
std::string toKana(const std::string& text);

} // namespace kana
