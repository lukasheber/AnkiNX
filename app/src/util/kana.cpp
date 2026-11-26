#include "util/kana.hpp"

#include <cctype>
#include <map>
#include <string>

namespace kana
{
namespace
{

// Romaji (lower case) to hiragana: Hepburn, Kunrei and the usual input
// method spellings.
const std::map<std::string, std::string>& table()
{
    static const std::map<std::string, std::string> TABLE = [] {
        std::map<std::string, std::string> t;
        auto row = [&t](const std::string& consonant, const char* a, const char* i, const char* u, const char* e,
                        const char* o) {
            const char* kana[] = { a, i, u, e, o };
            const char* vowels = "aiueo";
            for (int v = 0; v < 5; v++)
                if (kana[v][0])
                    t[consonant + vowels[v]] = kana[v];
        };
        row("", "あ", "い", "う", "え", "お");
        row("k", "か", "き", "く", "け", "こ");
        row("g", "が", "ぎ", "ぐ", "げ", "ご");
        row("s", "さ", "し", "す", "せ", "そ");
        row("z", "ざ", "じ", "ず", "ぜ", "ぞ");
        row("t", "た", "ち", "つ", "て", "と");
        row("d", "だ", "ぢ", "づ", "で", "ど");
        row("n", "な", "に", "ぬ", "ね", "の");
        row("h", "は", "ひ", "ふ", "へ", "ほ");
        row("b", "ば", "び", "ぶ", "べ", "ぼ");
        row("p", "ぱ", "ぴ", "ぷ", "ぺ", "ぽ");
        row("m", "ま", "み", "む", "め", "も");
        row("y", "や", "", "ゆ", "いぇ", "よ");
        row("r", "ら", "り", "る", "れ", "ろ");
        row("w", "わ", "うぃ", "う", "うぇ", "を");
        row("f", "ふぁ", "ふぃ", "ふ", "ふぇ", "ふぉ");
        row("v", "ゔぁ", "ゔぃ", "ゔ", "ゔぇ", "ゔぉ");
        row("j", "じゃ", "じ", "じゅ", "じぇ", "じょ");
        row("sh", "しゃ", "し", "しゅ", "しぇ", "しょ");
        row("ch", "ちゃ", "ち", "ちゅ", "ちぇ", "ちょ");
        row("ts", "つぁ", "つぃ", "つ", "つぇ", "つぉ");
        // Small kana.
        for (const char* small : { "x", "l" })
        {
            std::string s = small;
            row(s, "ぁ", "ぃ", "ぅ", "ぇ", "ぉ");
            row(s + "y", "ゃ", "", "ゅ", "", "ょ");
            t[s + "tu"]  = "っ";
            t[s + "tsu"] = "っ";
            t[s + "wa"]  = "ゎ";
            t[s + "ka"]  = "ゕ";
            t[s + "ke"]  = "ゖ";
        }
        t["xn"] = "ん";
        // Yōon: consonant + y + a/u/o (and the less common i/e).
        const std::map<std::string, std::string> stems = { { "k", "き" }, { "g", "ぎ" }, { "s", "し" }, { "z", "じ" },
                                                           { "t", "ち" }, { "c", "ち" }, { "d", "ぢ" }, { "n", "に" },
                                                           { "h", "ひ" }, { "b", "び" }, { "p", "ぴ" }, { "m", "み" },
                                                           { "r", "り" }, { "j", "じ" } };
        for (const auto& [consonant, stem] : stems)
        {
            t[consonant + "ya"] = stem + "ゃ";
            t[consonant + "yi"] = stem + "ぃ";
            t[consonant + "yu"] = stem + "ゅ";
            t[consonant + "ye"] = stem + "ぇ";
            t[consonant + "yo"] = stem + "ょ";
        }
        t["dhi"] = "でぃ";
        t["thi"] = "てぃ";
        t["dhu"] = "でゅ";
        t["twu"] = "とぅ";
        t["dwu"] = "どぅ";
        t["-"]   = "ー";
        return t;
    }();
    return TABLE;
}

bool isVowel(char c) { return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o'; }

bool isRomaji(char c) { return std::isalpha((unsigned char)c) || c == '-' || c == '\''; }

// Whether some romaji in the table begins with `prefix`.
bool startsSomeKey(const std::string& prefix)
{
    auto it = table().lower_bound(prefix);
    return it != table().end() && it->first.compare(0, prefix.size(), prefix) == 0;
}

// Hiragana to katakana (the hiragana block's letters are 0x60 below).
std::string katakana(const std::string& hiragana)
{
    std::string out;
    for (size_t i = 0; i < hiragana.size();)
    {
        unsigned char c = hiragana[i];
        size_t len      = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (len == 3 && i + 2 < hiragana.size())
        {
            unsigned cp = ((c & 0x0F) << 12) | ((hiragana[i + 1] & 0x3F) << 6) | (hiragana[i + 2] & 0x3F);
            if (cp >= 0x3041 && cp <= 0x3096)
            {
                cp += 0x60;
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
                i += 3;
                continue;
            }
        }
        out.append(hiragana, i, len);
        i += len;
    }
    return out;
}

} // namespace

Converted convert(const std::string& romaji, bool final)
{
    Converted out;
    size_t i = 0;
    while (i < romaji.size())
    {
        char c = romaji[i];
        if (!isRomaji(c))
        {
            out.kana += c;
            i++;
            continue;
        }
        // (capitals give katakana; the rest is matched in lower case)
        bool upper = std::isupper((unsigned char)c);
        std::string lower;
        for (size_t j = i; j < romaji.size() && j < i + 4 && isRomaji(romaji[j]); j++)
            lower += (char)std::tolower((unsigned char)romaji[j]);
        auto emit = [&](const std::string& hiragana, size_t used) {
            out.kana += upper ? katakana(hiragana) : hiragana;
            i += used;
        };
        if (lower[0] == 'n' && lower.size() >= 2 && (lower[1] == 'n' || lower[1] == '\''))
        {
            emit("ん", 2); // "nn", "n'"
            continue;
        }
        if (lower[0] == 'n' && lower.size() >= 2 && !isVowel(lower[1]) && lower[1] != 'y')
        {
            emit("ん", 1); // before a consonant
            continue;
        }
        // (only letters that begin kana; not the small-kana prefixes x and l)
        if (lower.size() >= 2 && lower[0] == lower[1] && std::isalpha((unsigned char)lower[0]) &&
            !isVowel(lower[0]) && lower[0] != 'n' && lower[0] != 'x' && lower[0] != 'l' &&
            startsSomeKey(std::string(1, lower[0])))
        {
            emit("っ", 1); // a doubled consonant
            continue;
        }
        if (lower.compare(0, 3, "tch") == 0)
        {
            emit("っ", 1); // "tchi": っち
            continue;
        }
        bool matched = false;
        for (size_t len = lower.size(); len > 0 && !matched; len--)
        {
            auto it = table().find(lower.substr(0, len));
            if (it != table().end())
            {
                emit(it->second, len);
                matched = true;
            }
        }
        if (matched)
            continue;
        // Not (yet) romaji: wait for more letters, unless it is the end or
        // nothing could follow.
        bool atEnd = i + lower.size() == romaji.size();
        if (!final && atEnd && (startsSomeKey(lower) || lower == "n"))
        {
            out.rest = romaji.substr(i);
            break;
        }
        if (lower == "n")
            emit("ん", 1);
        else
        {
            out.kana += c;
            i++;
        }
    }
    return out;
}

std::string convertTail(const std::string& text, bool final)
{
    size_t start = text.size();
    while (start > 0 && isRomaji(text[start - 1]))
        start--;
    if (start == text.size())
        return text;
    Converted converted = convert(text.substr(start), final);
    return text.substr(0, start) + converted.kana + converted.rest;
}

std::string toKana(const std::string& text) { return convert(text, true).kana; }

} // namespace kana
