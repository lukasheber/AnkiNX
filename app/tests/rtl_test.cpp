// Host test for view/rtl_text: Arabic-script shaping and visual order.
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

std::string hex(const std::string& s)
{
    std::string out;
    char buf[4];
    for (unsigned char c : s)
    {
        std::snprintf(buf, sizeof buf, "%02X", c);
        out += buf;
    }
    return out;
}

void expect(const std::string& input, const std::string& want, const char* what)
{
    std::string got = rtl::visual(input);
    bool ok         = got == want;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
    {
        std::printf("     want %s\n     got  %s\n", hex(want).c_str(), hex(got).c_str());
        failures++;
    }
}

} // namespace

int main()
{
    // سلام: seen joins lam; lam+alef is one ligature (final, after seen);
    // meem stands alone after alef. Drawn right to left.
    expect(utf8({ 0x0633, 0x0644, 0x0627, 0x0645 }), utf8({ 0xFEE1, 0xFEFC, 0xFEB3 }), "Arabic: joining and lam-alef");
    // کتاب (Persian keheh): initial, medial, final alef, isolated beh.
    expect(utf8({ 0x06A9, 0x062A, 0x0627, 0x0628 }), utf8({ 0xFE8F, 0xFE8E, 0xFE98, 0xFB90 }),
           "Persian letters and right-joining alef");
    // می‌خواهم: the zero width non-joiner splits the word and is dropped.
    expect(utf8({ 0x0645, 0x06CC, 0x200C, 0x062E, 0x0648, 0x0627, 0x0647, 0x0645 }),
           utf8({ 0xFEE2, 0xFEEB, 0xFE8D, 0xFEEE, 0xFEA7, 0xFBFD, 0xFEE3 }), "ZWNJ breaks joining");
    // ۱۲۳تومان: digits keep their order, on the right of the word.
    expect(utf8({ 0x06F1, 0x06F2, 0x06F3, 0x062A, 0x0648, 0x0645, 0x0627, 0x0646 }),
           utf8({ 0xFEE5, 0xFE8E, 0xFEE3, 0xFEEE, 0xFE97, 0x06F1, 0x06F2, 0x06F3 }), "digits stay left to right");
    // (سلام): brackets mirrored.
    expect(utf8({ '(', 0x0633, 0x0644, 0x0627, 0x0645, ')' }), utf8({ '(', 0xFEE1, 0xFEFC, 0xFEB3, ')' }),
           "brackets mirrored");
    // (۱۱۰ in Persian text: digits stay in order, the bracket goes right.
    expect(utf8({ '(', 0x06F1, 0x06F1, 0x06F0 }), utf8({ 0x06F1, 0x06F1, 0x06F0, ')' }),
           "Persian digits with a bracket");
    expect("(110", "(110", "ASCII digits with a bracket unchanged");
    // Marks stay after their letter: بَ (beh + fatha) then ا.
    expect(utf8({ 0x0628, 0x064E, 0x0627 }), utf8({ 0xFE8E, 0xFE91, 0x064E }), "marks keep joining and order");
    expect(utf8({ 0x05E9, 0x05DC, 0x05D5, 0x05DD }), utf8({ 0x05DD, 0x05D5, 0x05DC, 0x05E9 }), "Hebrew reversed");
    expect("plain, café", "plain, café", "left-to-right text unchanged");
    bool detect = rtl::hasRtl(utf8({ 'a', 0x0628 })) && !rtl::hasRtl("abc ü") && !rtl::hasRtl(utf8({ 0x06F1 }));
    std::printf("%s hasRtl\n", detect ? "ok  " : "FAIL");
    failures += detect ? 0 : 1;

    std::printf("%s\n", failures ? "RTL TESTS FAILED" : "all rtl tests passed");
    return failures ? 1 : 0;
}
