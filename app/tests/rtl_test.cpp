// Host test for view/rtl_text: how right-to-left words are cut into pieces in
// drawing order (shaped runs in logical order; the rest in visual order).
// Built with the desktop preview; run by app/test_scripts.sh.
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>

#include "view/rtl_text.hpp"

namespace
{

int failures = 0;

std::string utf8(std::initializer_list<uint32_t> cps)
{
    std::string out;
    for (uint32_t c : cps)
    {
        if (c < 0x80)
            out += (char)c;
        else if (c < 0x800)
        {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        }
        else
        {
            out += (char)(0xE0 | (c >> 12));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// "S:" a shaped run, "P:" plain text, separated by " | ".
std::string describe(const std::vector<rtl::Piece>& pieces)
{
    std::string out;
    for (const auto& p : pieces)
        out += (out.empty() ? "" : " | ") + std::string(p.shaped ? "S:" : "P:") + p.text;
    return out;
}

void check(bool ok, const char* what, const std::string& detail = "")
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
    {
        std::printf("     %s\n", detail.c_str());
        failures++;
    }
}

void expect(const std::string& input, const std::string& want, const char* what)
{
    std::string got = describe(rtl::pieces(input));
    check(got == want, what, "want " + want + "\n     got  " + got);
}

} // namespace

int main()
{
    const std::string salam = utf8({ 0x0633, 0x0644, 0x0627, 0x0645 }); // سلام
    expect(salam, "S:" + salam, "an Arabic word is one run to shape");
    // می‌خواهم: the zero width non-joiner stays in the run (the shaper breaks joining there).
    const std::string mikhaham = utf8({ 0x0645, 0x06CC, 0x200C, 0x062E, 0x0648, 0x0627, 0x0647, 0x0645 });
    expect(mikhaham, "S:" + mikhaham, "ZWNJ stays inside the run");
    // ۱۲۳تومان: the digits keep their order, on the right of the word.
    expect(utf8({ 0x06F1, 0x06F2, 0x06F3, 0x062A, 0x0648, 0x0645, 0x0627, 0x0646 }),
           "S:" + utf8({ 0x062A, 0x0648, 0x0645, 0x0627, 0x0646 }) + " | P:" + utf8({ 0x06F1, 0x06F2, 0x06F3 }),
           "digits stay left to right, on the right");
    // (سلام): brackets mirrored around the run.
    expect("(" + salam + ")", "P:( | S:" + salam + " | P:)", "brackets mirrored");
    // (۱۱۰ in Persian text: digits stay in order, the bracket goes right.
    expect(utf8({ '(', 0x06F1, 0x06F1, 0x06F0 }), "P:" + utf8({ 0x06F1, 0x06F1, 0x06F0, ')' }),
           "Persian digits with a bracket");
    // بَا: the mark stays with its letter, in the run.
    expect(utf8({ 0x0628, 0x064E, 0x0627 }), "S:" + utf8({ 0x0628, 0x064E, 0x0627 }), "marks stay in the run");
    // שָׁלוֹם: Hebrew with points, one run.
    const std::string shalom = utf8({ 0x05E9, 0x05B8, 0x05C1, 0x05DC, 0x05D5, 0x05B9, 0x05DD });
    expect(shalom, "S:" + shalom, "Hebrew with points is one run");

    check(rtl::rtlWord(salam) && rtl::rtlWord(utf8({ '(', 0x06F1 })) && !rtl::rtlWord("(110") &&
              !rtl::rtlWord("plain, café"),
          "right-to-left words: letters or Arabic-Indic digits");
    check(rtl::hasRtl(utf8({ 'a', 0x0628 })) && !rtl::hasRtl("abc ü") && !rtl::hasRtl(utf8({ 0x06F1 })), "hasRtl");

    std::printf("%s\n", failures ? "RTL TESTS FAILED" : "all rtl tests passed");
    return failures ? 1 : 0;
}
