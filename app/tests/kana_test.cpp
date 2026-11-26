// Romaji to kana (util/kana.cpp), as typed on the thumb keyboard's hiragana
// mode: run with `kana_test`; prints the failures and exits 1 if any.

#include "util/kana.hpp"

#include <cstdio>
#include <string>

static int failures = 0;

static void expect(const std::string& what, const std::string& got, const std::string& wanted)
{
    if (got != wanted)
    {
        std::printf("FAIL %s: got \"%s\", wanted \"%s\"\n", what.c_str(), got.c_str(), wanted.c_str());
        failures++;
    }
}

// The text after typing `keys` one by one (each key converts the tail), then
// finishing it as Space or OK do.
static std::string typed(const std::string& keys)
{
    std::string text;
    for (char key : keys)
        text = kana::convertTail(text + key, false);
    return kana::convertTail(text, true);
}

int main()
{
    // Whole words, as WaniKani readings are typed.
    const char* words[][2] = {
        { "taberu", "たべる" },         { "shinbun", "しんぶん" },     { "sinbun", "しんぶん" },
        { "kippu", "きっぷ" },          { "gakkou", "がっこう" },       { "matcha", "まっちゃ" },
        { "kyou", "きょう" },           { "ryokan", "りょかん" },       { "sensei", "せんせい" },
        { "hannnou", "はんのう" },      { "han'nou", "はんのう" },      { "konnnichiha", "こんにちは" },
        { "kan'i", "かんい" },          { "hon", "ほん" },              { "tsukue", "つくえ" },
        { "tukue", "つくえ" },          { "chiisai", "ちいさい" },      { "jisho", "じしょ" },
        { "zyosi", "じょし" },          { "fujisan", "ふじさん" },      { "wo", "を" },
        { "kooto-", "こおとー" },       { "xtsu", "っ" },               { "ltu", "っ" },
        { "xya", "ゃ" },                { "nya", "にゃ" },              { "shunyuu", "しゅにゅう" },
        { "onnna", "おんな" },          { "dhi", "でぃ" },              { "vu", "ゔ" },
    };
    for (auto& word : words)
        expect(std::string("typing ") + word[0], typed(word[0]), word[1]);

    // Capitals give katakana, as WanaKana does.
    expect("capitals", typed("KOOHI-"), "コオヒー");

    // While typing: a lone n, or the start of a longer kana, waits.
    expect("pending n", kana::convertTail("かn", false), "かn");
    expect("pending ky", kana::convertTail("ky", false), "ky");
    expect("n before a consonant", kana::convertTail("kanj", false), "かんj");
    expect("n before a vowel", kana::convertTail("kana", false), "かな");
    expect("final n", kana::convertTail("hon", true), "ほん");

    // Letters that can't be kana stay; other characters are kept.
    expect("q", typed("q"), "q");
    expect("qq", typed("qqa"), "qqあ");
    expect("spaces and digits", kana::toKana("ni hon 2"), "に ほん 2");
    expect("kana kept", kana::toKana("かna"), "かな");

    if (failures == 0)
        std::printf("kana: all passed\n");
    return failures == 0 ? 0 : 1;
}
