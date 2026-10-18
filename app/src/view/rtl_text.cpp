#include "view/rtl_text.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

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

// Unicode joining types (ArabicShaping.txt): U, R, D, C.
enum class Joining
{
    None,
    Right,
    Dual,
    Causing,
};

// A letter's presentation forms (Arabic Presentation Forms-A/B); 0 where
// Unicode has none.
struct Letter
{
    uint32_t base;
    Joining joining;
    uint32_t isolated, final, initial, medial;
};

constexpr Joining U = Joining::None, R = Joining::Right, D = Joining::Dual;

// Sorted by base.
const Letter LETTERS[] = {
    { 0x0621, U, 0xFE80, 0, 0, 0 },                // hamza
    { 0x0622, R, 0xFE81, 0xFE82, 0, 0 },           // alef with madda
    { 0x0623, R, 0xFE83, 0xFE84, 0, 0 },           // alef with hamza above
    { 0x0624, R, 0xFE85, 0xFE86, 0, 0 },           // waw with hamza
    { 0x0625, R, 0xFE87, 0xFE88, 0, 0 },           // alef with hamza below
    { 0x0626, D, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C }, // yeh with hamza
    { 0x0627, R, 0xFE8D, 0xFE8E, 0, 0 },           // alef
    { 0x0628, D, 0xFE8F, 0xFE90, 0xFE91, 0xFE92 }, // beh
    { 0x0629, R, 0xFE93, 0xFE94, 0, 0 },           // teh marbuta
    { 0x062A, D, 0xFE95, 0xFE96, 0xFE97, 0xFE98 }, // teh
    { 0x062B, D, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C }, // theh
    { 0x062C, D, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0 }, // jeem
    { 0x062D, D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4 }, // hah
    { 0x062E, D, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8 }, // khah
    { 0x062F, R, 0xFEA9, 0xFEAA, 0, 0 },           // dal
    { 0x0630, R, 0xFEAB, 0xFEAC, 0, 0 },           // thal
    { 0x0631, R, 0xFEAD, 0xFEAE, 0, 0 },           // reh
    { 0x0632, R, 0xFEAF, 0xFEB0, 0, 0 },           // zain
    { 0x0633, D, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4 }, // seen
    { 0x0634, D, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8 }, // sheen
    { 0x0635, D, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC }, // sad
    { 0x0636, D, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0 }, // dad
    { 0x0637, D, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4 }, // tah
    { 0x0638, D, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8 }, // zah
    { 0x0639, D, 0xFEC9, 0xFECA, 0xFECB, 0xFECC }, // ain
    { 0x063A, D, 0xFECD, 0xFECE, 0xFECF, 0xFED0 }, // ghain
    { 0x0641, D, 0xFED1, 0xFED2, 0xFED3, 0xFED4 }, // feh
    { 0x0642, D, 0xFED5, 0xFED6, 0xFED7, 0xFED8 }, // qaf
    { 0x0643, D, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC }, // kaf
    { 0x0644, D, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0 }, // lam
    { 0x0645, D, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4 }, // meem
    { 0x0646, D, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8 }, // noon
    { 0x0647, D, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC }, // heh
    { 0x0648, R, 0xFEED, 0xFEEE, 0, 0 },           // waw
    { 0x0649, D, 0xFEEF, 0xFEF0, 0xFBE8, 0xFBE9 }, // alef maksura
    { 0x064A, D, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4 }, // yeh
    { 0x0671, R, 0xFB50, 0xFB51, 0, 0 },           // alef wasla
    { 0x0679, D, 0xFB66, 0xFB67, 0xFB68, 0xFB69 }, // tteh (Urdu)
    { 0x067E, D, 0xFB56, 0xFB57, 0xFB58, 0xFB59 }, // peh (Persian)
    { 0x0686, D, 0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D }, // tcheh (Persian)
    { 0x0688, R, 0xFB88, 0xFB89, 0, 0 },           // ddal (Urdu)
    { 0x0691, R, 0xFB8C, 0xFB8D, 0, 0 },           // rreh (Urdu)
    { 0x0698, R, 0xFB8A, 0xFB8B, 0, 0 },           // jeh (Persian)
    { 0x06A9, D, 0xFB8E, 0xFB8F, 0xFB90, 0xFB91 }, // keheh (Persian kaf)
    { 0x06AF, D, 0xFB92, 0xFB93, 0xFB94, 0xFB95 }, // gaf (Persian)
    { 0x06BA, R, 0xFB9E, 0xFB9F, 0, 0 },           // noon ghunna (Urdu)
    { 0x06BE, D, 0xFBAA, 0xFBAB, 0xFBAC, 0xFBAD }, // heh doachashmee (Urdu)
    { 0x06C0, R, 0xFBA4, 0xFBA5, 0, 0 },           // heh with yeh above (Persian)
    { 0x06C1, D, 0xFBA6, 0xFBA7, 0xFBA8, 0xFBA9 }, // heh goal (Urdu)
    { 0x06CC, D, 0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF }, // farsi yeh
    { 0x06D2, R, 0xFBAE, 0xFBAF, 0, 0 },           // yeh barree (Urdu)
};

const Letter* letter(uint32_t c)
{
    auto it = std::lower_bound(std::begin(LETTERS), std::end(LETTERS), c,
                               [](const Letter& l, uint32_t value) { return l.base < value; });
    return it != std::end(LETTERS) && it->base == c ? it : nullptr;
}

// Combining marks: they sit on the previous letter and don't break joining.
bool transparent(uint32_t c)
{
    return (c >= 0x0591 && c <= 0x05BD) || c == 0x05BF || c == 0x05C1 || c == 0x05C2 || c == 0x05C4 ||
           c == 0x05C5 || c == 0x05C7 || (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) ||
           c == 0x0670 || (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) || c == 0x06E7 ||
           c == 0x06E8 || (c >= 0x06EA && c <= 0x06ED);
}

Joining joiningOf(uint32_t c)
{
    if (c == 0x0640 || c == 0x200D) // tatweel, zero width joiner
        return Joining::Causing;
    const Letter* l = letter(c);
    return l ? l->joining : Joining::None;
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

// Contextual forms; zero width (non-)joiners are dropped once used.
std::vector<uint32_t> shape(const std::vector<uint32_t>& in)
{
    std::vector<uint32_t> out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++)
    {
        uint32_t c      = in[i];
        const Letter* l = letter(c);
        if (!l)
        {
            if (c != 0x200C && c != 0x200D)
                out.push_back(c);
            continue;
        }
        Joining prev = Joining::None;
        for (size_t p = i; p > 0;)
            if (!transparent(in[--p]))
            {
                prev = joiningOf(in[p]);
                break;
            }
        Joining next     = Joining::None;
        size_t nextIndex = in.size();
        for (size_t n = i + 1; n < in.size(); n++)
            if (!transparent(in[n]))
            {
                next      = joiningOf(in[n]);
                nextIndex = n;
                break;
            }
        bool joinPrev = (prev == Joining::Dual || prev == Joining::Causing) && l->joining != Joining::None;

        // Lam followed by alef: one ligature.
        if (c == 0x0644 && nextIndex < in.size())
        {
            uint32_t a   = in[nextIndex];
            uint32_t lig = a == 0x0622 ? 0xFEF5 : a == 0x0623 ? 0xFEF7 : a == 0x0625 ? 0xFEF9 : a == 0x0627 ? 0xFEFB : 0;
            if (lig)
            {
                out.push_back(joinPrev ? lig + 1 : lig);
                for (size_t m = i + 1; m < nextIndex; m++)
                    out.push_back(in[m]);
                i = nextIndex;
                continue;
            }
        }

        bool joinNext = l->joining == Joining::Dual &&
                        (next == Joining::Dual || next == Joining::Right || next == Joining::Causing);
        uint32_t form = joinPrev && joinNext ? l->medial : joinPrev ? l->final : joinNext ? l->initial : l->isolated;
        out.push_back(form ? form : l->isolated ? l->isolated : c);
    }
    return out;
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

std::string visual(const std::string& utf8)
{
    // Words of Arabic-Indic digits and punctuation, like "(۱۱۰", are in
    // right-to-left text too: their punctuation goes the right-to-left way.
    if (!hasRtl(utf8))
    {
        std::vector<uint32_t> all = decode(utf8);
        if (std::none_of(all.begin(), all.end(), [](uint32_t c) { return isDigit(c) && c >= 0x0660; }))
            return utf8;
    }
    std::vector<uint32_t> cps = shape(decode(utf8));

    // Clusters (a character and its marks) with a direction each; neutrals
    // take the direction around them, else right to left (the word's).
    struct Cluster
    {
        size_t start, end;
        Dir dir;
    };
    std::vector<Cluster> clusters;
    for (size_t i = 0; i < cps.size(); i++)
    {
        if (transparent(cps[i]) && !clusters.empty())
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

    // Runs of one direction, drawn last to first; right-to-left runs reversed
    // cluster by cluster (marks stay after their letter), brackets mirrored.
    std::string out;
    out.reserve(utf8.size());
    size_t runEnd = clusters.size();
    while (runEnd > 0)
    {
        size_t runStart = runEnd - 1;
        while (runStart > 0 && clusters[runStart - 1].dir == clusters[runEnd - 1].dir)
            runStart--;
        if (clusters[runStart].dir == Dir::Rtl)
            for (size_t k = runEnd; k > runStart; k--)
            {
                const Cluster& cl = clusters[k - 1];
                encode(out, mirrored(cps[cl.start]));
                for (size_t m = cl.start + 1; m < cl.end; m++)
                    encode(out, cps[m]);
            }
        else
            for (size_t k = runStart; k < runEnd; k++)
                for (size_t m = clusters[k].start; m < clusters[k].end; m++)
                    encode(out, cps[m]);
        runEnd = runStart;
    }
    return out;
}

} // namespace rtl
