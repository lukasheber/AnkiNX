#include "view/rtl_text.hpp"

#include <algorithm>
#include <cstdint>

namespace rtl
{
namespace
{

std::vector<uint32_t> decode(const std::string& s)
{
    std::vector<uint32_t> out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();)
    {
        unsigned char c = s[i];
        int extra       = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (extra < 0)
        {
            out.push_back(0xFFFD);
            i++;
            continue;
        }
        uint32_t cp = extra == 0 ? c : c & (0x3F >> extra);
        bool ok     = true;
        for (int k = 1; k <= extra; k++)
        {
            if (i + k >= s.size() || ((unsigned char)s[i + k] >> 6) != 2)
            {
                ok = false;
                break;
            }
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        }
        if (!ok)
        {
            out.push_back(0xFFFD);
            i++;
            continue;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

void encode(std::string& out, uint32_t c)
{
    if (c < 0x80)
        out += (char)c;
    else if (c < 0x800)
    {
        out += (char)(0xC0 | (c >> 6));
        out += (char)(0x80 | (c & 0x3F));
    }
    else if (c < 0x10000)
    {
        out += (char)(0xE0 | (c >> 12));
        out += (char)(0x80 | ((c >> 6) & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    }
    else
    {
        out += (char)(0xF0 | (c >> 18));
        out += (char)(0x80 | ((c >> 12) & 0x3F));
        out += (char)(0x80 | ((c >> 6) & 0x3F));
        out += (char)(0x80 | (c & 0x3F));
    }
}

// Combining marks (Hebrew points, Arabic harakat) and the zero width
// (non-)joiners: they belong with the letter before them.
bool attached(uint32_t c)
{
    return (c >= 0x0591 && c <= 0x05BD) || c == 0x05BF || c == 0x05C1 || c == 0x05C2 || c == 0x05C4 ||
           c == 0x05C5 || c == 0x05C7 || (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) ||
           c == 0x0670 || (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) || c == 0x06E7 ||
           c == 0x06E8 || (c >= 0x06EA && c <= 0x06ED) || c == 0x200C || c == 0x200D;
}

bool isDigit(uint32_t c)
{
    return (c >= '0' && c <= '9') || (c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9);
}

bool isRtl(uint32_t c)
{
    if (isDigit(c))
        return false;
    return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF);
}

bool isLtr(uint32_t c)
{
    if (c < 0x80)
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || isDigit(c);
    // Letters of other scripts; punctuation and symbol blocks are neutral.
    return !isRtl(c) && c >= 0xC0 && c != 0xD7 && c != 0xF7 && !(c >= 0x2000 && c <= 0x2BFF) &&
           !(c >= 0x3000 && c <= 0x303F) && !(c >= 0xFE30 && c <= 0xFE4F) && !(c >= 0xFF00 && c <= 0xFF0F) &&
           c != 0xFFFD;
}

uint32_t mirrored(uint32_t c)
{
    switch (c)
    {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        case 0xAB: return 0xBB; // « »
        case 0xBB: return 0xAB;
        default: return c;
    }
}

enum class Dir
{
    Ltr,
    Rtl,
    Neutral,
};

Dir dirOf(uint32_t c) { return isRtl(c) ? Dir::Rtl : isLtr(c) ? Dir::Ltr : Dir::Neutral; }

} // namespace

bool hasRtl(const std::string& utf8)
{
    // Fast path: ASCII has none.
    if (std::all_of(utf8.begin(), utf8.end(), [](char c) { return (unsigned char)c < 0x80; }))
        return false;
    for (uint32_t c : decode(utf8))
        if (isRtl(c))
            return true;
    return false;
}

bool rtlWord(const std::string& utf8)
{
    if (std::all_of(utf8.begin(), utf8.end(), [](char c) { return (unsigned char)c < 0x80; }))
        return false;
    for (uint32_t c : decode(utf8))
        if (isRtl(c) || (isDigit(c) && c >= 0x0660))
            return true;
    return false;
}

std::vector<Piece> pieces(const std::string& utf8)
{
    std::vector<uint32_t> cps = decode(utf8);

    // Clusters (a character and what is attached to it) with a direction
    // each; neutrals take the direction around them, else right to left (the
    // word's).
    struct Cluster
    {
        size_t start, end;
        Dir dir;
    };
    std::vector<Cluster> clusters;
    for (size_t i = 0; i < cps.size(); i++)
    {
        if (attached(cps[i]) && !clusters.empty())
        {
            clusters.back().end = i + 1;
            continue;
        }
        clusters.push_back({ i, i + 1, dirOf(cps[i]) });
    }
    for (size_t i = 0; i < clusters.size(); i++)
    {
        if (clusters[i].dir != Dir::Neutral)
            continue;
        Dir before = Dir::Rtl, after = Dir::Rtl;
        for (size_t p = i; p > 0;)
            if (clusters[--p].dir != Dir::Neutral)
            {
                before = clusters[p].dir;
                break;
            }
        for (size_t n = i + 1; n < clusters.size(); n++)
            if (clusters[n].dir != Dir::Neutral)
            {
                after = clusters[n].dir;
                break;
            }
        clusters[i].dir = before == after ? before : Dir::Rtl;
    }

    std::vector<Piece> out;
    auto add = [&out](bool shaped, const std::string& text) {
        if (!out.empty() && out.back().shaped == shaped && !shaped)
            out.back().text += text;
        else
            out.push_back({ text, shaped });
    };
    auto text = [&cps](const Cluster& cl, bool mirror) {
        std::string s;
        encode(s, mirror ? mirrored(cps[cl.start]) : cps[cl.start]);
        for (size_t m = cl.start + 1; m < cl.end; m++)
            encode(s, cps[m]);
        return s;
    };

    // Runs of one direction, drawn last to first. In a right-to-left run,
    // letters go to the shaper in logical order (it lays them out right to
    // left); what lies between them is reversed here, brackets mirrored.
    size_t runEnd = clusters.size();
    while (runEnd > 0)
    {
        size_t runStart = runEnd - 1;
        while (runStart > 0 && clusters[runStart - 1].dir == clusters[runEnd - 1].dir)
            runStart--;
        if (clusters[runStart].dir == Dir::Rtl)
        {
            size_t k = runEnd;
            while (k > runStart)
            {
                if (isRtl(cps[clusters[k - 1].start]))
                {
                    size_t first = k - 1;
                    while (first > runStart && isRtl(cps[clusters[first - 1].start]))
                        first--;
                    std::string letters;
                    for (size_t c = first; c < k; c++)
                        letters += text(clusters[c], false);
                    add(true, letters);
                    k = first;
                }
                else
                {
                    add(false, text(clusters[k - 1], true));
                    k--;
                }
            }
        }
        else
            for (size_t k = runStart; k < runEnd; k++)
                add(false, text(clusters[k], false));
        runEnd = runStart;
    }
    return out;
}

} // namespace rtl
