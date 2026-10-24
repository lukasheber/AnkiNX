#include "view/shaping.hpp"

#include <borealis.hpp>
#include <hb.h>
#include <iterator>
#include <utility>

namespace shaping
{
namespace
{

// Each shaped script: its codepoint ranges and its bundled font
// (resources/card_fonts). Another script needs only a row here and its font.
struct Script
{
    const char* font;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
};
const Script SCRIPTS[] = {
    { "NotoSansDevanagari-Regular.ttf", { { 0x0900, 0x097F }, { 0xA8E0, 0xA8FF }, { 0x1CD0, 0x1CFF } } },
    { "NotoSansThai-Regular.ttf", { { 0x0E00, 0x0E7F } } },
};
constexpr int PLAIN   = -1;
constexpr int INHERIT = -2; // joiners and the dotted circle: part of the run they are in

uint32_t next(const std::string& s, size_t& i)
{
    auto byte = [&](size_t k) { return k < s.size() ? (uint32_t)(unsigned char)s[k] : 0u; };
    uint32_t c = byte(i);
    if (c < 0x80)
        return i++, c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    c &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : 0x1F;
    i++;
    for (int k = 0; k < extra; k++, i++)
        c = (c << 6) | (byte(i) & 0x3F);
    return c;
}

int scriptOf(uint32_t c)
{
    if (c == 0x200C || c == 0x200D || c == 0x25CC)
        return INHERIT;
    for (int s = 0; s < (int)std::size(SCRIPTS); s++)
        for (const auto& [first, last] : SCRIPTS[s].ranges)
            if (c >= first && c <= last)
                return s;
    return PLAIN;
}

int scriptOfRun(const std::string& run)
{
    for (size_t i = 0; i < run.size();)
        if (int s = scriptOf(next(run, i)); s >= 0)
            return s;
    return PLAIN;
}

struct Font
{
    hb_font_t* font = nullptr;
    float upem      = 1000;
    bool tried      = false;
};

// The script's font, loaded on first use and kept for the app's lifetime.
Font& fontFor(int script)
{
    static Font fonts[std::size(SCRIPTS)];
    Font& f = fonts[script];
    if (f.tried)
        return f;
    f.tried          = true;
    std::string path = std::string(BRLS_ASSET("card_fonts/")) + SCRIPTS[script].font;
    hb_blob_t* blob  = hb_blob_create_from_file_or_fail(path.c_str());
    if (!blob)
    {
        brls::Logger::error("shaping: cannot read {}", path);
        return f;
    }
    hb_face_t* face = hb_face_create(blob, 0);
    hb_blob_destroy(blob);
    f.upem = (float)hb_face_get_upem(face);
    f.font = hb_font_create(face);
    hb_face_destroy(face);
    hb_font_set_scale(f.font, (int)f.upem, (int)f.upem); // positions in font units
    return f;
}

struct Glyph
{
    hb_codepoint_t id;
    float x, y; // offset from the run's start, font units (y up)
};

// The run's glyphs and its total advance (font units).
std::vector<Glyph> shape(const Font& font, const std::string& run, float* advance)
{
    std::vector<Glyph> glyphs;
    *advance = 0;
    if (!font.font)
        return glyphs;
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, run.data(), (int)run.size(), 0, (int)run.size());
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font.font, buffer, nullptr, 0);
    unsigned count            = 0;
    hb_glyph_info_t* info     = hb_buffer_get_glyph_infos(buffer, &count);
    hb_glyph_position_t* pos  = hb_buffer_get_glyph_positions(buffer, &count);
    float x                   = 0;
    for (unsigned i = 0; i < count; i++)
    {
        glyphs.push_back({ info[i].codepoint, x + pos[i].x_offset, (float)pos[i].y_offset });
        x += pos[i].x_advance;
    }
    *advance = x;
    hb_buffer_destroy(buffer);
    return glyphs;
}

// Glyph outlines to NanoVG paths. Each contour's winding (from its signed
// area in font units) says whether it is ink or a hole: NanoVG would
// otherwise fill every contour, closing the counters of letters.
struct Pen
{
    NVGcontext* vg;
    float x, y, scale;  // the glyph origin on screen, px per font unit
    float area;         // twice the contour's signed area so far
    float lastX, lastY; // current point, font units
    float startX, startY;
};

void track(Pen* p, float x, float y)
{
    p->area += p->lastX * y - x * p->lastY;
    p->lastX = x;
    p->lastY = y;
}
float sx(const Pen* p, float x) { return p->x + x * p->scale; }
float sy(const Pen* p, float y) { return p->y - y * p->scale; }

void moveTo(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x, float y, void*)
{
    auto* p   = (Pen*)data;
    p->area   = 0;
    p->lastX  = p->startX = x;
    p->lastY  = p->startY = y;
    nvgMoveTo(p->vg, sx(p, x), sy(p, y));
}
void lineTo(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float x, float y, void*)
{
    auto* p = (Pen*)data;
    track(p, x, y);
    nvgLineTo(p->vg, sx(p, x), sy(p, y));
}
void quadTo(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float cx, float cy, float x, float y, void*)
{
    auto* p = (Pen*)data;
    track(p, cx, cy);
    track(p, x, y);
    nvgQuadTo(p->vg, sx(p, cx), sy(p, cy), sx(p, x), sy(p, y));
}
void cubicTo(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float c1x, float c1y, float c2x, float c2y, float x,
             float y, void*)
{
    auto* p = (Pen*)data;
    track(p, c1x, c1y);
    track(p, c2x, c2y);
    track(p, x, y);
    nvgBezierTo(p->vg, sx(p, c1x), sy(p, c1y), sx(p, c2x), sy(p, c2y), sx(p, x), sy(p, y));
}
void closePath(hb_draw_funcs_t*, void* data, hb_draw_state_t*, void*)
{
    auto* p = (Pen*)data;
    track(p, p->startX, p->startY);
    nvgClosePath(p->vg);
    // TrueType: ink runs clockwise (negative area, y up), holes the other way.
    nvgPathWinding(p->vg, p->area <= 0 ? NVG_SOLID : NVG_HOLE);
}

hb_draw_funcs_t* drawFuncs()
{
    static hb_draw_funcs_t* funcs = [] {
        hb_draw_funcs_t* f = hb_draw_funcs_create();
        hb_draw_funcs_set_move_to_func(f, moveTo, nullptr, nullptr);
        hb_draw_funcs_set_line_to_func(f, lineTo, nullptr, nullptr);
        hb_draw_funcs_set_quadratic_to_func(f, quadTo, nullptr, nullptr);
        hb_draw_funcs_set_cubic_to_func(f, cubicTo, nullptr, nullptr);
        hb_draw_funcs_set_close_path_func(f, closePath, nullptr, nullptr);
        hb_draw_funcs_make_immutable(f);
        return f;
    }();
    return funcs;
}

} // namespace

bool needed(const std::string& utf8)
{
    for (size_t i = 0; i < utf8.size();)
    {
        if ((unsigned char)utf8[i] < 0x80)
        {
            i++;
            continue;
        }
        if (scriptOf(next(utf8, i)) >= 0)
            return true;
    }
    return false;
}

std::vector<Run> runs(const std::string& utf8)
{
    std::vector<Run> out;
    int current = PLAIN;
    for (size_t i = 0; i < utf8.size();)
    {
        size_t start = i;
        int script   = scriptOf(next(utf8, i));
        if (script == INHERIT)
            script = out.empty() ? PLAIN : current;
        if (out.empty() || script != current)
        {
            out.push_back({ "", script >= 0 });
            current = script;
        }
        out.back().text.append(utf8, start, i - start);
    }
    return out;
}

float width(const std::string& run, float size)
{
    int script = scriptOfRun(run);
    if (script < 0)
        return 0;
    const Font& font = fontFor(script);
    float advance    = 0;
    shape(font, run, &advance);
    return advance * size / font.upem;
}

void draw(NVGcontext* vg, const std::string& run, float x, float y, float size)
{
    int script = scriptOfRun(run);
    if (script < 0)
        return;
    const Font& font = fontFor(script);
    float advance    = 0;
    auto glyphs      = shape(font, run, &advance);
    if (glyphs.empty())
        return;
    Pen pen { vg, 0, 0, size / font.upem, 0, 0, 0, 0, 0 };
    nvgBeginPath(vg);
    for (const Glyph& g : glyphs)
    {
        pen.x = x + g.x * pen.scale;
        pen.y = y - g.y * pen.scale;
        hb_font_draw_glyph(font.font, g.id, drawFuncs(), &pen);
    }
    nvgFill(vg);
}

} // namespace shaping
