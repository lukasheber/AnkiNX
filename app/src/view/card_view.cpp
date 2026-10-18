#include "view/card_view.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <lunasvg.h>
#include <map>
#include <sys/stat.h>
#include <webp/decode.h>

using namespace litehtml;

namespace
{

NVGcolor toNvg(const web_color& c)
{
    return nvgRGBA(c.red, c.green, c.blue, c.alpha);
}

bool fileExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string urlDecode(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); i++)
    {
        if (in[i] == '%' && i + 2 < in.size() && std::isxdigit((unsigned char)in[i + 1]) &&
            std::isxdigit((unsigned char)in[i + 2]))
        {
            out += (char)std::stoi(in.substr(i + 1, 2), nullptr, 16);
            i += 2;
        }
        else
        {
            out += in[i];
        }
    }
    return out;
}

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// Payload of a "data:[<type>][;base64],<data>" URI; empty if malformed.
std::string decodeDataUri(const std::string& uri)
{
    size_t comma = uri.find(',');
    if (uri.rfind("data:", 0) != 0 || comma == std::string::npos)
        return {};
    std::string meta    = uri.substr(5, comma - 5);
    std::string payload = uri.substr(comma + 1);
    std::transform(meta.begin(), meta.end(), meta.begin(), ::tolower);
    if (meta.size() < 7 || meta.compare(meta.size() - 7, 7, ";base64") != 0)
        return urlDecode(payload);

    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::string out;
    uint32_t bits = 0;
    int count     = 0;
    for (char c : urlDecode(payload))
    {
        int v = value(c);
        if (v < 0)
            continue; // padding, whitespace
        bits = (bits << 6) | (uint32_t)v;
        count += 6;
        if (count >= 8)
        {
            count -= 8;
            out += (char)((bits >> count) & 0xff);
        }
    }
    return out;
}

bool isWebp(const std::string& data)
{
    return data.size() > 12 && data.compare(0, 4, "RIFF") == 0 && data.compare(8, 4, "WEBP") == 0;
}

bool isSvg(const std::string& path, const std::string& data)
{
    std::string ext = path.substr(path.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == "svg" || data.find("<svg") != std::string::npos;
}

// Upload tightly packed RGBA pixels, repacking rows if `stride` has padding.
int uploadRgba(NVGcontext* vg, const uint8_t* pixels, int width, int height, int stride)
{
    if (stride == width * 4)
        return nvgCreateImageRGBA(vg, width, height, 0, pixels);
    std::vector<uint8_t> packed((size_t)width * height * 4);
    for (int y = 0; y < height; y++)
        std::copy_n(pixels + (size_t)y * stride, width * 4, packed.data() + (size_t)y * width * 4);
    return nvgCreateImageRGBA(vg, width, height, 0, packed.data());
}

// Image Occlusion: the backend marks the occluded image's src with this.
const char* OCCLUDED_IMAGE = "#ankinx-io";

bool isOccludedImage(const std::string& src)
{
    size_t n = std::strlen(OCCLUDED_IMAGE);
    return src.size() > n && src.compare(src.size() - n, n, OCCLUDED_IMAGE) == 0;
}

// "#rgb", "#rrggbb" or "#rrggbbaa"; anything else gives `fallback`.
NVGcolor parseColor(const std::string& text, NVGcolor fallback)
{
    if (text.size() < 4 || text[0] != '#' ||
        !std::all_of(text.begin() + 1, text.end(), [](char c) { return std::isxdigit((unsigned char)c); }))
        return fallback;
    auto hex = [&](size_t at, size_t len) { return (unsigned char)std::stoi(text.substr(at, len), nullptr, 16); };
    switch (text.size())
    {
        case 4: return nvgRGB(hex(1, 1) * 17, hex(2, 1) * 17, hex(3, 1) * 17);
        case 7: return nvgRGB(hex(1, 2), hex(3, 2), hex(5, 2));
        case 9: return nvgRGBA(hex(1, 2), hex(3, 2), hex(5, 2), hex(7, 2));
        default: return fallback;
    }
}

void roundedRect(NVGcontext* vg, const position& p, const border_radiuses& r)
{
    if (r.top_left_x.value() > 0 || r.top_right_x.value() > 0 || r.bottom_right_x.value() > 0 ||
        r.bottom_left_x.value() > 0)
        nvgRoundedRectVarying(vg, p.x, p.y, p.width, p.height, r.top_left_x, r.top_right_x,
                              r.bottom_right_x, r.bottom_left_x);
    else
        nvgRect(vg, p.x, p.y, p.width, p.height);
}

} // namespace

// litehtml callbacks implemented with NanoVG. Coordinates are document pixels;
// CardView sets up the translation/zoom transform before drawing.
class NvgContainer : public document_container
{
  public:
    explicit NvgContainer(CardView* view)
        : view(view)
    {
    }

    ~NvgContainer() override { clearImages(); }

    NVGcontext* vg = nullptr;
    std::string mediaDir;

    void clearImages()
    {
        NVGcontext* ctx = brls::Application::getNVGContext();
        for (auto& [_, img] : images)
            if (img.id > 0 && ctx)
                nvgDeleteImage(ctx, img.id);
        images.clear();
    }

  private:
    struct Font
    {
        int face;
        float size;
        int weight;
        bool italic;
        int decoration;
        font_metrics metrics;
    };

    struct Image
    {
        int id = 0;
        // Layout size (CSS pixels); SVG textures are rasterised larger.
        int width  = 0;
        int height = 0;
    };

    // PNG, JPEG, GIF (first frame), BMP via NanoVG's stb_image; WebP via
    // libwebp; SVG (including Anki's LaTeX SVGs) via lunasvg.
    // `path` is only used to recognise SVG by extension.
    Image decodeImage(const std::string& path, const std::string& data)
    {
        Image img;
        if (data.empty())
            return img;
        if (isWebp(data))
        {
            int w = 0, h = 0;
            uint8_t* pixels = WebPDecodeRGBA((const uint8_t*)data.data(), data.size(), &w, &h);
            if (pixels)
            {
                img.id = nvgCreateImageRGBA(vg, w, h, 0, pixels);
                img.width  = w;
                img.height = h;
                WebPFree(pixels);
            }
        }
        else if (isSvg(path, data))
        {
            auto doc = lunasvg::Document::loadFromData(data);
            if (!doc)
                return img;
            float w = doc->width() > 0 ? doc->width() : 300.0f;
            float h = doc->height() > 0 ? doc->height() : 150.0f;
            float scale = std::min(2.0f, 2048.0f / std::max(w, h));
            auto bitmap = doc->renderToBitmap((int)std::ceil(w * scale), (int)std::ceil(h * scale));
            if (bitmap.isNull())
                return img;
            bitmap.convertToRGBA();
            img.id     = uploadRgba(vg, bitmap.data(), bitmap.width(), bitmap.height(), bitmap.stride());
            img.width  = (int)std::ceil(w);
            img.height = (int)std::ceil(h);
        }
        else
        {
            img.id = nvgCreateImageMem(vg, 0, (unsigned char*)data.data(), (int)data.size());
            if (img.id > 0)
                nvgImageSize(vg, img.id, &img.width, &img.height);
        }
        if (img.id <= 0)
            img = Image();
        return img;
    }

    CardView* view;
    std::map<std::string, Image> images;

    void applyFont(const Font* f)
    {
        nvgFontFaceId(vg, f->face);
        nvgFontSize(vg, f->size);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    }

    std::string resolve(std::string src) const
    {
        if (isOccludedImage(src))
            src.resize(src.size() - std::strlen(OCCLUDED_IMAGE));
        if (src.empty() || src.find("://") != std::string::npos || src.rfind("data:", 0) == 0)
            return {};
        // Absolute: files the backend made, such as rendered maths (Anki media
        // file names never contain '/').
        if (src[0] == '/')
            return fileExists(src) ? src : std::string();
        std::string path = mediaDir + "/" + src;
        if (fileExists(path))
            return path;
        path = mediaDir + "/" + urlDecode(src);
        return fileExists(path) ? path : std::string();
    }

  public:
    uint_ptr create_font(const font_description& descr, const document*, font_metrics* fm) override
    {
        auto* f       = new Font();
        f->face       = brls::Application::getFont(brls::FONT_REGULAR);
        f->size       = std::max(1.0f, (float)descr.size);
        f->weight     = descr.weight;
        f->italic     = descr.style == font_style_italic;
        f->decoration = descr.decoration_line;

        float ascender = 0, descender = 0, lineHeight = 0, bounds[4] = {};
        applyFont(f);
        nvgTextMetrics(vg, &ascender, &descender, &lineHeight);
        nvgTextBounds(vg, 0, 0, "x", nullptr, bounds);
        float xHeight = bounds[3] - bounds[1];
        float zeroWidth = nvgTextBounds(vg, 0, 0, "0", nullptr, nullptr);

        f->metrics.font_size = f->size;
        f->metrics.ascent    = ascender;
        f->metrics.descent   = -descender;
        f->metrics.height    = lineHeight;
        f->metrics.x_height  = xHeight > 0 ? xHeight : f->size * 0.5f;
        f->metrics.ch_width  = zeroWidth;
        if (fm)
            *fm = f->metrics;
        return (uint_ptr)f;
    }

    void delete_font(uint_ptr hFont) override { delete (Font*)hFont; }

    pixel_t text_width(const char* text, uint_ptr hFont) override
    {
        applyFont((Font*)hFont);
        return nvgTextBounds(vg, 0, 0, text, nullptr, nullptr);
    }

    void draw_text(uint_ptr, const char* text, uint_ptr hFont, web_color color, const position& pos) override
    {
        auto* f = (Font*)hFont;
        applyFont(f);
        float baseline = pos.y + pos.height - f->metrics.descent;
        nvgFillColor(vg, toNvg(color));

        auto drawRun = [&](float x) {
            if (f->italic)
            {
                // No italic face: slant the regular one around the baseline.
                nvgSave(vg);
                nvgTranslate(vg, x, baseline);
                nvgSkewX(vg, -0.2f);
                nvgText(vg, 0, 0, text, nullptr);
                nvgRestore(vg);
            }
            else
            {
                nvgText(vg, x, baseline, text, nullptr);
            }
        };
        drawRun(pos.x);
        if (f->weight >= 600) // no bold face either: overstrike
            drawRun(pos.x + std::max(0.6f, f->size / 28.0f));

        if (f->decoration & (text_decoration_line_underline | text_decoration_line_line_through))
        {
            float w = nvgTextBounds(vg, 0, 0, text, nullptr, nullptr);
            nvgStrokeColor(vg, toNvg(color));
            nvgStrokeWidth(vg, std::max(1.0f, f->size / 16.0f));
            nvgBeginPath(vg);
            if (f->decoration & text_decoration_line_underline)
            {
                nvgMoveTo(vg, pos.x, baseline + 2);
                nvgLineTo(vg, pos.x + w, baseline + 2);
            }
            if (f->decoration & text_decoration_line_line_through)
            {
                float y = baseline - f->metrics.x_height / 2;
                nvgMoveTo(vg, pos.x, y);
                nvgLineTo(vg, pos.x + w, y);
            }
            nvgStroke(vg);
        }
    }

    pixel_t pt_to_px(float pt) const override { return pt * 96.0f / 72.0f; }
    pixel_t get_default_font_size() const override { return 20; }
    const char* get_default_font_name() const override { return "sans-serif"; }

    void draw_list_marker(uint_ptr hdc, const list_marker& marker) override
    {
        const position& p = marker.pos;
        nvgBeginPath(vg);
        switch (marker.marker_type)
        {
            case list_style_type_none:
                return;
            case list_style_type_disc:
                nvgCircle(vg, p.x + p.width / 2, p.y + p.height / 2, p.width / 2);
                nvgFillColor(vg, toNvg(marker.color));
                nvgFill(vg);
                return;
            case list_style_type_circle:
                nvgCircle(vg, p.x + p.width / 2, p.y + p.height / 2, p.width / 2 - 0.5f);
                nvgStrokeColor(vg, toNvg(marker.color));
                nvgStrokeWidth(vg, 1);
                nvgStroke(vg);
                return;
            case list_style_type_square:
                nvgRect(vg, p.x, p.y, p.width, p.height);
                nvgFillColor(vg, toNvg(marker.color));
                nvgFill(vg);
                return;
            default:
                if (marker.font)
                {
                    // Numbered lists: right-aligned "n." before the item.
                    std::string label = std::to_string(marker.index) + ".";
                    float w           = text_width(label.c_str(), marker.font);
                    position at       = p;
                    at.x              = p.x + p.width - w;
                    draw_text(hdc, label.c_str(), marker.font, marker.color, at);
                }
                return;
        }
    }

    void load_image(const char* src, const char*, bool) override
    {
        if (images.count(src))
            return;
        if (!vg)
        {
            images[src] = Image();
            return;
        }
        if (std::strncmp(src, "data:", 5) == 0)
        {
            std::string uri = src;
            bool svg        = uri.rfind("data:image/svg", 0) == 0;
            images[src]     = decodeImage(svg ? "inline.svg" : "inline", decodeDataUri(uri));
            return;
        }
        std::string path = resolve(src);
        images[src]      = path.empty() ? Image() : decodeImage(path, readFile(path));
    }

    void get_image_size(const char* src, const char*, size& sz) override
    {
        auto it = images.find(src);
        if (it == images.end() || it->second.id <= 0)
        {
            sz = size(0, 0);
            return;
        }
        sz = size(it->second.width, it->second.height);
    }

    void draw_image(uint_ptr, const background_layer& layer, const std::string& url, const std::string&) override
    {
        auto it = images.find(url);
        if (it == images.end() || it->second.id <= 0)
            return;
        const position& o = layer.origin_box;
        const position& c = layer.clip_box;
        if (o.width.value() <= 0 || o.height.value() <= 0)
            return;
        nvgSave(vg);
        // litehtml clips inline images to the text line's height; a formula
        // taller than the line (rendered maths, by absolute path) must not be.
        if (url.empty() || url[0] != '/')
            nvgIntersectScissor(vg, c.x, c.y, c.width, c.height);
        NVGpaint paint = nvgImagePattern(vg, o.x, o.y, o.width, o.height, 0, it->second.id, 1.0f);
        nvgBeginPath(vg);
        if (layer.repeat == background_repeat_no_repeat)
            roundedRect(vg, o, layer.border_radius);
        else
            roundedRect(vg, layer.border_box, layer.border_radius);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
        nvgRestore(vg);
        if (isOccludedImage(url))
            drawOcclusions(o);
    }

    // Image Occlusion masks over `image` (document pixels), following Anki's
    // drawShapes/drawShape in ts/routes/image-occlusion/review.ts. Colours and
    // borders are the defaults of its notetype CSS.
    void drawOcclusions(const position& image)
    {
        const float w = image.width, h = image.height;
        for (const Occlusion& s : view->occlusions)
        {
            if (view->masksHidden && s.kind != Occlusion::Kind::Text)
                continue;
            NVGcolor fill, stroke;
            switch (s.state)
            {
                case Occlusion::State::Active:
                    fill   = nvgRGB(0xff, 0x8e, 0x8e);
                    stroke = nvgRGB(0x21, 0x21, 0x21);
                    break;
                case Occlusion::State::Inactive:
                    if (!s.occludeInactive)
                        continue;
                    fill   = parseColor(s.fill, nvgRGB(0xff, 0xeb, 0xa2));
                    stroke = nvgRGB(0x21, 0x21, 0x21);
                    break;
                case Occlusion::State::Highlight:
                    fill   = nvgRGBA(0xff, 0x8e, 0x8e, 0);
                    stroke = nvgRGB(0xff, 0x8e, 0x8e);
                    break;
            }
            float left = image.x + s.left * w, top = image.y + s.top * h;
            nvgSave(vg);
            nvgIntersectScissor(vg, image.x, image.y, w, h);
            if (s.angle != 0 && s.kind != Occlusion::Kind::Polygon)
            {
                nvgTranslate(vg, left, top);
                nvgRotate(vg, nvgDegToRad(s.angle));
                nvgTranslate(vg, -left, -top);
            }
            nvgBeginPath(vg);
            switch (s.kind)
            {
                case Occlusion::Kind::Rect:
                    nvgRect(vg, left, top, s.width * w, s.height * h);
                    break;
                case Occlusion::Kind::Ellipse:
                    nvgEllipse(vg, left + s.rx * w, top + s.ry * h, s.rx * w, s.ry * h);
                    break;
                case Occlusion::Kind::Polygon:
                {
                    if (s.points.empty())
                        break;
                    // Points are placed so their top-left corner is at (left, top).
                    float minX = s.points[0].first, minY = s.points[0].second;
                    for (const auto& [x, y] : s.points)
                        minX = std::min(minX, x), minY = std::min(minY, y);
                    float dx = left - (image.x + minX * w), dy = top - (image.y + minY * h);
                    for (size_t i = 0; i < s.points.size(); i++)
                    {
                        float x = image.x + s.points[i].first * w + dx;
                        float y = image.y + s.points[i].second * h + dy;
                        i == 0 ? nvgMoveTo(vg, x, y) : nvgLineTo(vg, x, y);
                    }
                    nvgClosePath(vg);
                    break;
                }
                case Occlusion::Kind::Text:
                    drawOcclusionText(s, left, top, h);
                    nvgRestore(vg);
                    continue;
            }
            nvgFillColor(vg, fill);
            nvgFill(vg);
            nvgStrokeColor(vg, stroke);
            nvgStrokeWidth(vg, 1);
            nvgStroke(vg);
            nvgRestore(vg);
        }
    }

    // A text label: dark text on a white box (review.ts, Text branch).
    void drawOcclusionText(const Occlusion& s, float left, float top, float imageHeight)
    {
        int font = brls::Application::getFont(brls::FONT_REGULAR);
        if (font == brls::FONT_INVALID)
            return;
        float size = (s.fontSize > 0 ? s.fontSize * imageHeight : 40.0f) * s.scale;
        nvgFontFaceId(vg, font);
        nvgFontSize(vg, size);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        // review.ts: line height is 1.5x the height of "M".
        float bounds[4];
        nvgTextBounds(vg, 0, 0, "M", nullptr, bounds);
        float lineHeight = 1.5f * (bounds[3] - bounds[1]);
        std::vector<std::string> lines;
        size_t start = 0;
        for (size_t end; (end = s.text.find('\n', start)) != std::string::npos; start = end + 1)
            lines.push_back(s.text.substr(start, end - start));
        lines.push_back(s.text.substr(start));
        float maxWidth = 0;
        for (const auto& line : lines)
            maxWidth = std::max(maxWidth, nvgTextBounds(vg, 0, 0, line.c_str(), nullptr, nullptr));
        const float padding = 5 * s.scale;
        nvgBeginPath(vg);
        nvgRect(vg, left, top, maxWidth + padding, lineHeight * lines.size() + padding);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);
        nvgFillColor(vg, parseColor(s.fill, nvgRGB(0, 0, 0)));
        for (size_t i = 0; i < lines.size(); i++)
            nvgText(vg, left, top + i * lineHeight, lines[i].c_str(), nullptr);
    }

    void draw_solid_fill(uint_ptr, const background_layer& layer, const web_color& color) override
    {
        if (color.alpha == 0)
            return;
        nvgSave(vg);
        const position& c = layer.clip_box;
        nvgIntersectScissor(vg, c.x, c.y, c.width, c.height);
        nvgBeginPath(vg);
        roundedRect(vg, layer.border_box, layer.border_radius);
        nvgFillColor(vg, toNvg(color));
        nvgFill(vg);
        nvgRestore(vg);
    }

    void draw_linear_gradient(uint_ptr, const background_layer& layer,
                              const background_layer::linear_gradient& g) override
    {
        if (g.color_points.empty())
            return;
        // NanoVG gradients have two stops: use the first and last.
        NVGcolor from = toNvg(g.color_points.front().color);
        NVGcolor to   = toNvg(g.color_points.back().color);
        nvgSave(vg);
        const position& c = layer.clip_box;
        nvgIntersectScissor(vg, c.x, c.y, c.width, c.height);
        nvgBeginPath(vg);
        roundedRect(vg, layer.border_box, layer.border_radius);
        nvgFillPaint(vg, nvgLinearGradient(vg, g.start.x, g.start.y, g.end.x, g.end.y, from, to));
        nvgFill(vg);
        nvgRestore(vg);
    }

    void draw_radial_gradient(uint_ptr hdc, const background_layer& layer,
                              const background_layer::radial_gradient& g) override
    {
        if (!g.color_points.empty())
            draw_solid_fill(hdc, layer, g.color_points.front().color);
    }

    void draw_conic_gradient(uint_ptr hdc, const background_layer& layer,
                             const background_layer::conic_gradient& g) override
    {
        if (!g.color_points.empty())
            draw_solid_fill(hdc, layer, g.color_points.front().color);
    }

    void draw_borders(uint_ptr, const borders& b, const position& p, bool) override
    {
        const border* sides[4] = { &b.top, &b.right, &b.bottom, &b.left };
        bool uniform = true;
        for (const border* s : sides)
            uniform = uniform && s->width == b.top.width && s->style == b.top.style && s->color == b.top.color;

        if (uniform && b.top.width.value() > 0 && b.top.style != border_style_none &&
            b.top.style != border_style_hidden)
        {
            float w = b.top.width;
            position inner(p.x + w / 2, p.y + w / 2, p.width - w, p.height - w);
            nvgBeginPath(vg);
            roundedRect(vg, inner, b.radius);
            nvgStrokeColor(vg, toNvg(b.top.color));
            nvgStrokeWidth(vg, w);
            nvgStroke(vg);
            return;
        }
        auto side = [&](const border& s, float x, float y, float w, float h) {
            if (s.width.value() <= 0 || s.style == border_style_none || s.style == border_style_hidden)
                return;
            nvgBeginPath(vg);
            nvgRect(vg, x, y, w, h);
            nvgFillColor(vg, toNvg(s.color));
            nvgFill(vg);
        };
        side(b.top, p.x, p.y, p.width, b.top.width);
        side(b.bottom, p.x, p.y + p.height - b.bottom.width, p.width, b.bottom.width);
        side(b.left, p.x, p.y, b.left.width, p.height);
        side(b.right, p.x + p.width - b.right.width, p.y, b.right.width, p.height);
    }

    void set_caption(const char*) override { }
    void set_base_url(const char*) override { }
    void link(const std::shared_ptr<document>&, const element::ptr&) override { }

    void on_anchor_click(const char* url, const element::ptr&) override
    {
        view->linkActivated = true;
        if (view->onLink && url)
            view->onLink(url);
    }

    void on_mouse_event(const element::ptr&, mouse_event) override { }
    void set_cursor(const char*) override { }

    void transform_text(std::string& text, text_transform tt) override
    {
        // ASCII only; good enough for the occasional uppercase heading.
        if (tt == text_transform_uppercase)
            std::transform(text.begin(), text.end(), text.begin(), ::toupper);
        else if (tt == text_transform_lowercase)
            std::transform(text.begin(), text.end(), text.begin(), ::tolower);
        else if (tt == text_transform_capitalize && !text.empty())
            text[0] = (char)std::toupper((unsigned char)text[0]);
    }

    void import_css(std::string&, const std::string&, std::string&) override { }

    void set_clip(const position& pos, const border_radiuses&) override
    {
        nvgSave(vg);
        nvgIntersectScissor(vg, pos.x, pos.y, pos.width, pos.height);
    }

    void del_clip() override { nvgRestore(vg); }

    void get_viewport(position& viewport) const override
    {
        float w  = view->layoutWidth > 0 ? view->layoutWidth : 1280;
        viewport = position(0, 0, w, view->viewHeight / view->zoom);
    }

    element::ptr create_element(const char*, const string_map&, const std::shared_ptr<document>&) override
    {
        return nullptr;
    }

    void get_media_features(media_features& media) const override
    {
        position vp;
        get_viewport(vp);
        media.type          = media_type_screen;
        media.width         = vp.width;
        media.height        = vp.height;
        media.device_width  = 1280;
        media.device_height = 720;
        media.color         = 8;
        media.monochrome    = 0;
        media.color_index   = 256;
        media.resolution    = 96;
    }

    void get_language(std::string& language, std::string& culture) const override
    {
        language = "en";
        culture  = "";
    }
};

CardView::CardView()
    : container(std::make_unique<NvgContainer>(this))
{
    this->setFocusable(true);
    this->setHideHighlight(true);

    this->registerAction(
        "", brls::BUTTON_UP, [this](brls::View*) {
            this->scrollBy(-80);
            return true;
        },
        true, true);
    this->registerAction(
        "", brls::BUTTON_DOWN, [this](brls::View*) {
            this->scrollBy(80);
            return true;
        },
        true, true);
    this->registerAction(
        "", brls::BUTTON_NAV_UP, [this](brls::View*) {
            this->scrollBy(-80);
            return true;
        },
        true, true);
    this->registerAction(
        "", brls::BUTTON_NAV_DOWN, [this](brls::View*) {
            this->scrollBy(80);
            return true;
        },
        true, true);

    // Touch: drag to scroll, tap to activate links (or flip the card).
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound*) {
            if (status.state == brls::GestureState::STAY || status.state == brls::GestureState::END)
                this->scrollBy(-status.delta.y);
        },
        brls::PanAxis::VERTICAL));
    this->addGestureRecognizer(new brls::TapGestureRecognizer(
        [this](brls::TapGestureStatus status, brls::Sound*) {
            if (status.state == brls::GestureState::END)
                this->handleTap(status.position);
        }));
}

CardView::~CardView() = default;

brls::View* CardView::create() { return new CardView(); }

void CardView::setHtml(const std::string& value, bool keepScroll)
{
    html        = value;
    needsLayout = true;
    if (!keepScroll)
    {
        scroll         = 0;
        scrollToAnswer = false;
    }
}

void CardView::scrollToAnswerAfterLayout() { scrollToAnswer = true; }

void CardView::setOcclusions(std::vector<Occlusion> shapes) { occlusions = std::move(shapes); }

void CardView::setMasksHidden(bool hidden) { masksHidden = hidden; }

void CardView::setMediaDir(const std::string& dir) { container->mediaDir = dir; }

void CardView::setOnLink(std::function<void(const std::string&)> callback) { onLink = std::move(callback); }

void CardView::setOnTap(std::function<void()> callback) { onTap = std::move(callback); }

void CardView::setOnElementTap(std::function<bool(int)> callback) { onElementTap = std::move(callback); }

void CardView::setZoom(float value)
{
    zoom        = std::clamp(value, 0.6f, 3.0f);
    needsLayout = true;
}

float CardView::maxScroll() const
{
    if (!document)
        return 0;
    return std::max(0.0f, (float)document->height() * zoom - viewHeight);
}

void CardView::scrollBy(float dy) { scroll = std::clamp(scroll + dy, 0.0f, maxScroll()); }

void CardView::layout(float width)
{
    container->clearImages();
    layoutWidth = width / zoom;
    document    = document::createFromString(html, container.get());
    if (document)
        document->render(layoutWidth);
    needsLayout = false;

    if (scrollToAnswer && document)
    {
        scrollToAnswer = false;
        if (auto root = document->root())
            if (auto hr = root->select_one("#answer"))
                scroll = std::clamp((float)hr->get_placement().y * zoom - 20.0f, 0.0f, maxScroll());
    }
}

void CardView::handleTap(brls::Point point)
{
    if (!document)
        return;
    // Screen point -> document coordinates.
    float docX = (point.x - this->getX()) / zoom;
    float docY = (point.y - this->getY() + scroll) / zoom;
    auto redraw   = [](const position&) {}; // the whole view is redrawn every frame anyway
    linkActivated = false;
    // litehtml acts on the element found by the last mouse-over; touch has no
    // hover, so move the pointer there first.
    document->on_mouse_over(docX, docY, docX, docY, redraw);
    document->on_lbutton_down(docX, docY, docX, docY, redraw);
    document->on_lbutton_up(docX, docY, docX, docY, redraw); // may call on_anchor_click
    if (linkActivated)
        return;
    // Card scripts: the innermost element with a node serial gets a click.
    if (onElementTap)
        for (auto el = std::const_pointer_cast<litehtml::element>(document->get_over_element()); el;
             el    = el->parent())
            if (const char* serial = el->get_attr("data-ankinx-node"))
            {
                if (onElementTap(std::atoi(serial)))
                    return;
                break;
            }
    if (onTap)
        onTap();
}

void CardView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*)
{
    container->vg = vg;
    viewHeight    = height;
    if (needsLayout || std::fabs(width / zoom - layoutWidth) > 0.5f)
        layout(width);
    if (!document)
        return;
    scroll = std::clamp(scroll, 0.0f, maxScroll());

    nvgSave(vg);
    nvgScissor(vg, x, y, width, height);
    nvgTranslate(vg, x, y - scroll);
    nvgScale(vg, zoom, zoom);
    position clip(0, scroll / zoom, width / zoom, height / zoom);
    document->draw(0, 0, 0, &clip);
    nvgRestore(vg);

    // Scroll indicator when the card is taller than the screen.
    float total = (float)document->height() * zoom;
    if (total > height + 1)
    {
        float barH = std::max(40.0f, height * height / total);
        float barY = y + (height - barH) * (scroll / maxScroll());
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + width - 6, barY, 4, barH, 2);
        nvgFillColor(vg, nvgRGBA(128, 128, 128, 160));
        nvgFill(vg);
    }
}
