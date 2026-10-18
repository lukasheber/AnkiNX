#pragma once

#include <string>

// litehtml has no ruby layout: furigana (<ruby>漢字<rt>かんじ</rt></ruby>, and
// rslib's <ruby><rb>..</rb><rt>..</rt></ruby>) would show the reading after
// the base. Each base/reading pair becomes its own
// <ruby class="ankinx-ruby"><rt>reading</rt><rb>base</rb></ruby>, which the
// reviewer's CSS lays out as an inline block: reading above, base on the
// text's baseline. <rp> fallbacks are dropped. HTML without <ruby> is
// returned unchanged.
std::string layoutRuby(const std::string& html);
