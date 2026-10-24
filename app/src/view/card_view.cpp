#include "view/card_view.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fstream>
#include <iterator>
#include <litehtml/css_parser.h>
#include <litehtml/render_item.h>
#include <lunasvg.h>
#include <map>
#include <set>
#include <strings.h>
#include <sys/stat.h>
#include <webp/decode.h>

#include <borealis/extern/nanovg/stb_image.h> // compiled into NanoVG (nanovg.c)

#include "util/controls.hpp"
#include "view/rtl_text.hpp"
#include "view/shaping.hpp"

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

std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string trimQuotes(std::string s)
{
    auto space = [](char c) { return std::isspace((unsigned char)c); };
    while (!s.empty() && space(s.front()))
        s.erase(0, 1);
    while (!s.empty() && space(s.back()))
        s.pop_back();
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
        s = s.substr(1, s.size() - 2);
    return s;
}

// A length token in a box-shadow or transform value (a bare 0 counts).
bool isCssLength(const css_token& t)
{
    return t.type == DIMENSION || t.type == PERCENTAGE || (t.type == NUMBER && t.n.number == 0);
}

// The length in CSS pixels: em by the element's font size, rem by the root
// default (16px), a percentage of `percentOf`.
float cssLength(const css_token& t, float fontSize, float percentOf = 0)
{
    if (t.type == PERCENTAGE)
        return t.n.number * percentOf / 100;
    if (t.type != DIMENSION)
        return t.type == NUMBER ? t.n.number : 0;
    std::string unit = lowerAscii(t.unit());
    if (unit == "em")
        return t.n.number * fontSize;
    if (unit == "rem")
        return t.n.number * 16;
    if (unit == "pt")
        return t.n.number * 4 / 3;
    return t.n.number;
}

// An angle token in radians (deg, rad, turn, grad; a bare number is radians).
float cssAngle(const css_token& t)
{
    std::string unit = t.type == DIMENSION ? lowerAscii(t.unit()) : "";
    if (unit == "deg")
        return t.n.number * (float)M_PI / 180;
    if (unit == "turn")
        return t.n.number * 2 * (float)M_PI;
    if (unit == "grad")
        return t.n.number * (float)M_PI / 200;
    return t.n.number;
}

// A card font from an @font-face rule (litehtml ignores those rules).
struct FontFace
{
    std::string family; // lower case, unquoted
    int weight  = 400;
    bool italic = false;
    std::vector<std::string> srcs; // url(...)s in order of preference
    int id = -2;                    // NanoVG font; -1 failed, -2 not loaded yet
};

// The value of property `name` in a declaration block (quotes and url(...)
// may contain ';').
std::string declaration(const std::string& block, const std::string& name)
{
    std::string lower = lowerAscii(block);
    for (size_t at = lower.find(name); at != std::string::npos; at = lower.find(name, at + 1))
    {
        size_t before = at;
        while (before > 0 && std::isspace((unsigned char)block[before - 1]))
            before--;
        if (before > 0 && block[before - 1] != ';')
            continue;
        size_t colon = at + name.size();
        while (colon < block.size() && std::isspace((unsigned char)block[colon]))
            colon++;
        if (colon >= block.size() || block[colon] != ':')
            continue;
        size_t i   = colon + 1;
        int parens = 0;
        char quote = 0;
        for (; i < block.size(); i++)
        {
            char c = block[i];
            if (quote)
                quote = c == quote ? 0 : quote;
            else if (c == '"' || c == '\'')
                quote = c;
            else if (c == '(')
                parens++;
            else if (c == ')')
                parens--;
            else if (c == ';' && parens <= 0)
                break;
        }
        return trimQuotes(block.substr(colon + 1, i - colon - 1));
    }
    return "";
}

// The url(...)s of a src descriptor; WOFF/WOFF2 dropped (NanoVG reads
// TrueType/OpenType only).
std::vector<std::string> fontUrls(const std::string& src)
{
    std::vector<std::string> out;
    std::string lower = lowerAscii(src);
    for (size_t at = lower.find("url("); at != std::string::npos; at = lower.find("url(", at + 4))
    {
        size_t i = at + 4;
        while (i < src.size() && std::isspace((unsigned char)src[i]))
            i++;
        size_t end;
        std::string url;
        if (i < src.size() && (src[i] == '"' || src[i] == '\''))
        {
            end = src.find(src[i], i + 1);
            if (end == std::string::npos)
                break;
            url = src.substr(i + 1, end - i - 1);
        }
        else
        {
            end = src.find(')', i);
            if (end == std::string::npos)
                break;
            url = trimQuotes(src.substr(i, end - i));
        }
        std::string path = lowerAscii(url.substr(0, url.find_first_of("?#")));
        bool woff = path.rfind("data:", 0) == 0 ? path.find("woff") != std::string::npos
                                                 : (path.size() > 5 && path.compare(path.size() - 5, 5, ".woff") == 0) ||
                                                       (path.size() > 6 && path.compare(path.size() - 6, 6, ".woff2") == 0);
        if (!url.empty() && !woff)
            out.push_back(url);
        at = end;
    }
    return out;
}

std::vector<FontFace> parseFontFaces(const std::string& html)
{
    std::vector<FontFace> out;
    std::string lower = lowerAscii(html);
    for (size_t at = lower.find("@font-face"); at != std::string::npos; at = lower.find("@font-face", at + 1))
    {
        size_t open  = html.find('{', at);
        size_t close = open == std::string::npos ? open : html.find('}', open);
        if (close == std::string::npos)
            break;
        std::string block = html.substr(open + 1, close - open - 1);
        FontFace face;
        face.family        = lowerAscii(trimQuotes(declaration(block, "font-family")));
        std::string weight = lowerAscii(declaration(block, "font-weight"));
        face.weight        = weight == "bold" || weight == "bolder" ? 700 : std::max(0, std::atoi(weight.c_str()));
        if (face.weight == 0)
            face.weight = 400;
        std::string style = lowerAscii(declaration(block, "font-style"));
        face.italic       = style.find("italic") != std::string::npos || style.find("oblique") != std::string::npos;
        face.srcs         = fontUrls(declaration(block, "src"));
        if (!face.family.empty() && !face.srcs.empty())
            out.push_back(std::move(face));
    }
    return out;
}

bool isFontFile(const std::string& data)
{
    return data.size() > 12 && (data.compare(0, 4, std::string("\0\1\0\0", 4)) == 0 || data.compare(0, 4, "OTTO") == 0 ||
                                data.compare(0, 4, "true") == 0 || data.compare(0, 4, "ttcf") == 0);
}

bool isGif(const std::string& data) { return data.size() > 6 && data.compare(0, 4, "GIF8") == 0; }

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

// lunasvg draws SVG text (canvas text among it) with the fonts registered
// with it, and finds none on the console: the bundled DejaVu faces, under the
// generic family names, the names lunasvg maps them to on each platform, and
// their own. Once, before the first SVG.
void registerSvgFonts()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    static const struct
    {
        const char* file;
        const char* names[3];
    } FACES[] = {
        { "DejaVuSans.ttf", { "sans-serif", "Arial", "DejaVu Sans" } },
        { "DejaVuSerif.ttf", { "serif", "Times New Roman", "DejaVu Serif" } },
        { "DejaVuSansMono.ttf", { "monospace", "Courier New", "DejaVu Sans Mono" } },
    };
    static std::vector<std::string> fonts; // lunasvg reads them in place, for the app's lifetime
    fonts.reserve(std::size(FACES));
    for (const auto& face : FACES)
    {
        fonts.push_back(readFile(std::string(BRLS_ASSET("card_fonts/")) + face.file));
        const std::string& data = fonts.back();
        if (data.empty())
            continue;
        for (const char* name : face.names)
            for (int style = 0; style < 4; style++)
                lunasvg_add_font_face_from_data(name, style & 1, style & 2, data.data(), data.size(), nullptr, nullptr);
    }
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
    std::vector<FontFace> fontFaces; // the current document's @font-face rules

    void clearImages()
    {
        NVGcontext* ctx = brls::Application::getNVGContext();
        for (auto& [_, img] : images)
            if (ctx)
            {
                if (img.id > 0)
                    nvgDeleteImage(ctx, img.id);
                for (int frame : img.frames)
                    nvgDeleteImage(ctx, frame);
            }
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
        // Animated GIF: one texture per frame after the first (id), and when
        // each frame ends, in ms from the start of the loop.
        std::vector<int> frames;
        std::vector<int> frameEnds;

        // The texture to draw now.
        int current() const
        {
            if (frames.empty() || frameEnds.back() <= 0)
                return id;
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count() %
                      frameEnds.back();
            for (size_t i = 0; i < frameEnds.size(); i++)
                if (ms < frameEnds[i])
                    return i == 0 ? id : frames[i - 1];
            return id;
        }
    };

    // An animated GIF: every frame and its delay (stb_image). False for a
    // single frame, or frames too large to keep (the first is used then).
    bool decodeGif(const std::string& data, Image& img)
    {
        int* delays = nullptr;
        int w = 0, h = 0, count = 0, comp = 0;
        stbi_uc* pixels = stbi_load_gif_from_memory((const stbi_uc*)data.data(), (int)data.size(), &delays, &w, &h,
                                                    &count, &comp, 4);
        bool animated = pixels && count > 1 && (size_t)w * h * 4 * count <= (64u << 20);
        if (animated)
        {
            const size_t frameSize = (size_t)w * h * 4;
            int end                = 0;
            for (int i = 0; i < count; i++)
            {
                int id = nvgCreateImageRGBA(vg, w, h, 0, pixels + frameSize * i);
                if (i == 0)
                    img.id = id;
                else
                    img.frames.push_back(id);
                // Browsers show frames of 0 or 10 ms for 100 ms.
                end += delays && delays[i] > 10 ? delays[i] : 100;
                img.frameEnds.push_back(end);
            }
            img.width  = w;
            img.height = h;
        }
        stbi_image_free(pixels);
        free(delays);
        return animated;
    }

    // PNG, JPEG, GIF (animated too), BMP via NanoVG's stb_image; WebP via
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
            registerSvgFonts();
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
        else if (isGif(data) && decodeGif(data, img))
        {
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

    // Load an @font-face font (once per app run: NanoVG keeps fonts by name).
    // Glyphs it lacks come from the system fonts.
    int loadFace(FontFace& face)
    {
        if (face.id != -2)
            return face.id;
        face.id = -1;
        constexpr long MAX_FONT_BYTES = 24 * 1024 * 1024;
        for (const std::string& src : face.srcs)
        {
            std::string data, name;
            if (src.rfind("data:", 0) == 0)
            {
                data = decodeDataUri(src);
                name = "card-font:" + std::to_string(std::hash<std::string> {}(data));
            }
            else
            {
                std::string path = resolve(src);
                struct stat st;
                if (path.empty() || stat(path.c_str(), &st) != 0 || st.st_size > MAX_FONT_BYTES)
                    continue;
                name = "card-font:" + path;
                if (int known = nvgFindFont(vg, name.c_str()); known >= 0)
                    return face.id = known;
                data = readFile(path);
            }
            if (int known = nvgFindFont(vg, name.c_str()); known >= 0)
                return face.id = known;
            if (!isFontFile(data))
                continue;
            auto* copy = (unsigned char*)std::malloc(data.size());
            if (!copy)
                continue;
            std::memcpy(copy, data.data(), data.size());
            int id = nvgCreateFontMem(vg, name.c_str(), copy, (int)data.size(), 1); // NanoVG frees `copy`
            if (id < 0)
                continue;
            addFallbacks(id);
            return face.id = id;
        }
        return -1;
    }

    // Glyphs a card font lacks come from the system fonts, then the bundled ones.
    void addFallbacks(int id)
    {
        for (const std::string& fallback :
             { brls::FONT_REGULAR, std::string("default"), brls::FONT_CHINESE_SIMPLIFIED,
               brls::FONT_CHINESE_SIMPLIFIED_EXT, brls::FONT_CHINESE_TRADITIONAL, brls::FONT_KOREAN_REGULAR,
               brls::FONT_EMOJI, brls::FONT_SWITCH_ICONS, brls::FONT_MATERIAL_ICONS })
            if (int f = brls::Application::getFont(fallback); f >= 0)
                nvgAddFallbackFontId(vg, id, f);
        for (int f : bundledFallbacks())
            nvgAddFallbackFontId(vg, id, f);
    }

    // The bundled font for a font-family list's first generic family, or a
    // common font name standing for one: DejaVu Sans Mono for monospace,
    // DejaVu Serif for serif; -1 for sans-serif (the system font).
    int genericFont(const std::string& families)
    {
        static const std::set<std::string> MONO = { "monospace", "courier", "courier new", "consolas", "menlo",
                                                    "monaco", "lucida console", "dejavu sans mono", "source code pro" };
        static const std::set<std::string> SERIF = { "serif", "times", "times new roman", "georgia", "garamond",
                                                     "cambria", "palatino", "book antiqua", "dejavu serif" };
        static const std::set<std::string> SANS = { "sans-serif", "system-ui", "arial", "helvetica", "verdana",
                                                    "tahoma", "segoe ui", "roboto", "dejavu sans" };
        size_t start = 0;
        while (start <= families.size())
        {
            size_t comma     = families.find(',', start);
            std::string name = lowerAscii(trimQuotes(families.substr(start, comma - start)));
            start            = comma == std::string::npos ? families.size() + 1 : comma + 1;
            if (MONO.count(name))
                return bundledFont("DejaVuSansMono.ttf");
            if (SERIF.count(name))
                return bundledFont("DejaVuSerif.ttf");
            if (SANS.count(name))
                return -1;
        }
        return -1;
    }

    int bundledFont(const char* file)
    {
        std::string name = std::string("ankinx-") + file;
        if (int id = nvgFindFont(vg, name.c_str()); id >= 0)
            return id;
        std::string data = readFile(std::string(BRLS_ASSET("card_fonts/")) + file);
        auto* copy       = isFontFile(data) ? (unsigned char*)std::malloc(data.size()) : nullptr;
        if (!copy)
        {
            brls::Logger::error("card fonts: missing font {}", file);
            return -1;
        }
        std::memcpy(copy, data.data(), data.size());
        int id = nvgCreateFontMem(vg, name.c_str(), copy, (int)data.size(), 1); // NanoVG frees `copy`
        if (id >= 0)
            addFallbacks(id);
        return id;
    }

    // The @font-face font for a CSS font-family list, closest in weight and
    // style; -1 if none applies.
    int cardFont(const std::string& families, int weight, bool italic, FontFace** chosen)
    {
        if (fontFaces.empty() || !vg)
            return -1;
        size_t start = 0;
        while (start <= families.size())
        {
            size_t comma     = families.find(',', start);
            std::string name = lowerAscii(trimQuotes(families.substr(start, comma - start)));
            start            = comma == std::string::npos ? families.size() + 1 : comma + 1;
            FontFace* best   = nullptr;
            int bestScore    = 0;
            for (FontFace& face : fontFaces)
            {
                if (face.family != name || face.id == -1)
                    continue;
                int score = std::abs(face.weight - weight) + (face.italic != italic ? 1000 : 0);
                if (!best || score < bestScore)
                    best = &face, bestScore = score;
            }
            if (best && loadFace(*best) >= 0)
            {
                *chosen = best;
                return best->id;
            }
        }
        return -1;
    }

    // Scripts the console's system fonts lack (resources/card_fonts): DejaVu
    // Sans for Arabic-script glyphs, including the presentation forms
    // rtl::visual produces; Noto Sans Thai; Noto Sans Devanagari (Hindi,
    // Marathi, Sanskrit, Nepali). Loaded once, as fallbacks of the system
    // font and of card fonts.
    const std::vector<int>& bundledFallbacks()
    {
        static std::vector<int> ids;
        static bool loaded = false;
        if (loaded || !vg)
            return ids;
        loaded = true;
        for (const char* file : { "DejaVuSans.ttf", "NotoSansThai-Regular.ttf", "NotoSansDevanagari-Regular.ttf" })
        {
            std::string name = std::string("ankinx-") + file;
            int id           = nvgFindFont(vg, name.c_str());
            if (id < 0)
            {
                std::string data = readFile(std::string(BRLS_ASSET("card_fonts/")) + file);
                auto* copy       = isFontFile(data) ? (unsigned char*)std::malloc(data.size()) : nullptr;
                if (!copy)
                {
                    brls::Logger::error("card fonts: missing fallback font {}", file);
                    continue;
                }
                std::memcpy(copy, data.data(), data.size());
                id = nvgCreateFontMem(vg, name.c_str(), copy, (int)data.size(), 1); // NanoVG frees `copy`
                if (id >= 0)
                    nvgAddFallbackFontId(vg, brls::Application::getFont(brls::FONT_REGULAR), id);
            }
            if (id >= 0)
                ids.push_back(id);
        }
        return ids;
    }

  public:
    uint_ptr create_font(const font_description& descr, const document*, font_metrics* fm) override
    {
        bundledFallbacks(); // the system fallbacks are set up in main.cpp
        auto* f       = new Font();
        f->face       = brls::Application::getFont(brls::FONT_REGULAR);
        f->size       = std::max(1.0f, (float)descr.size);
        f->weight     = descr.weight;
        f->italic     = descr.style == font_style_italic;
        f->decoration = descr.decoration_line;
        FontFace* face = nullptr;
        if (int id = cardFont(descr.family, descr.weight, f->italic, &face); id >= 0)
        {
            f->face = id;
            // A real bold or italic face needs no synthetic one.
            if (face->weight >= 600)
                f->weight = 400;
            if (face->italic)
                f->italic = false;
        }
        else if (int generic = genericFont(descr.family); generic >= 0)
        {
            f->face = generic;
        }

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
        // vertical-align: super/sub (<sup>, <sub>), as browsers place them.
        f->metrics.super_shift = f->size / 3;
        f->metrics.sub_shift   = f->size / 5;
        if (fm)
            *fm = f->metrics;
        return (uint_ptr)f;
    }

    void delete_font(uint_ptr hFont) override { delete (Font*)hFont; }

    // A word's runs: Devanagari, Thai and the like shaped by HarfBuzz
    // (shaping.cpp); the rest by NanoVG, right-to-left words shaped and in
    // visual order (rtl_text.cpp). The font must be applied.
    float wordWidth(const std::string& logical, const Font* f)
    {
        if (!shaping::needed(logical))
            return nvgTextBounds(vg, 0, 0, rtl::visual(logical).c_str(), nullptr, nullptr);
        float width = 0;
        for (const auto& run : shaping::runs(logical))
            width += run.shaped ? shaping::width(run.text, f->size)
                                : nvgTextBounds(vg, 0, 0, rtl::visual(run.text).c_str(), nullptr, nullptr);
        return width;
    }

    void drawWord(const std::string& logical, const Font* f, float x, float baseline)
    {
        if (!shaping::needed(logical))
        {
            nvgText(vg, x, baseline, rtl::visual(logical).c_str(), nullptr);
            return;
        }
        for (const auto& run : shaping::runs(logical))
        {
            if (run.shaped)
            {
                shaping::draw(vg, run.text, x, baseline, f->size);
                x += shaping::width(run.text, f->size);
            }
            else
            {
                std::string shown = rtl::visual(run.text);
                nvgText(vg, x, baseline, shown.c_str(), nullptr);
                x += nvgTextBounds(vg, 0, 0, shown.c_str(), nullptr, nullptr);
            }
        }
    }

    pixel_t text_width(const char* logical, uint_ptr hFont) override
    {
        auto* f = (Font*)hFont;
        applyFont(f);
        return wordWidth(logical, f);
    }

    void draw_text(uint_ptr, const char* logical, uint_ptr hFont, web_color color, const position& pos) override
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
                drawWord(logical, f, 0, 0);
                nvgRestore(vg);
            }
            else
            {
                drawWord(logical, f, x, baseline);
            }
        };
        drawRun(pos.x);
        if (f->weight >= 600) // no bold face either: overstrike
            drawRun(pos.x + std::max(0.6f, f->size / 28.0f));

        if (f->decoration & (text_decoration_line_underline | text_decoration_line_line_through))
        {
            float w = wordWidth(logical, f);
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
    // Browsers' "medium": note types set their own size (Anki's are 20px), and
    // small/large and friends are relative to this, as in Anki.
    pixel_t get_default_font_size() const override { return 16; }
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
        NVGpaint paint = nvgImagePattern(vg, o.x, o.y, o.width, o.height, 0, it->second.current(), 1.0f);
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

    // Outer box shadows (app/patches/litehtml/0003): NanoVG's feathered box
    // gradient, outside the border box only. Inset shadows are not drawn.
    void draw_box_shadow(uint_ptr, const element::ptr& el, const position& box, const border_radiuses& radius) override
    {
        float fontSize = el->css().get_font_size();
        auto shadows   = parse_comma_separated_list(normalize(el->css().get_box_shadow(), f_componentize));
        // The first shadow is on top: draw it last.
        for (auto it = shadows.rbegin(); it != shadows.rend(); ++it)
        {
            float len[4]    = {};
            int count       = 0;
            bool inset      = false;
            web_color color = el->css().get_color(); // currentColor
            for (const css_token& t : *it)
            {
                if (t.type == WHITESPACE)
                    continue;
                if (t.type == IDENT && lowerAscii(t.name()) == "inset")
                    inset = true;
                else if (isCssLength(t) && count < 4)
                    len[count++] = cssLength(t, fontSize);
                else
                    parse_color(t, color, this);
            }
            if (inset || count < 2 || color.alpha == 0)
                continue;
            float blur = std::max(0.0f, len[2]), spread = len[3];
            float x = box.x + len[0] - spread, y = box.y + len[1] - spread;
            float w = box.width + 2 * spread, h = box.height + 2 * spread;
            if (w <= 0 || h <= 0)
                continue;
            float r       = std::max(0.0f, (float)radius.top_left_x + spread);
            float feather = std::max(1.0f, blur * 1.5f);
            NVGcolor inner = toNvg(color), outer = inner;
            outer.a        = 0;
            nvgBeginPath(vg);
            nvgRect(vg, x - feather, y - feather, w + 2 * feather, h + 2 * feather);
            nvgRoundedRect(vg, box.x, box.y, box.width, box.height, (float)radius.top_left_x);
            nvgPathWinding(vg, NVG_HOLE);
            nvgFillPaint(vg, nvgBoxGradient(vg, x, y, w, h, r, feather, inner, outer));
            nvgFill(vg);
        }
    }

    // 2D transforms (app/patches/litehtml/0003) around transform-origin:
    // translate, rotate, scale, skew and matrix; 3D functions are ignored.
    void begin_transform(uint_ptr, const element::ptr& el, const position& box) override
    {
        nvgSave(vg);
        float fontSize = el->css().get_font_size();
        float ox = box.x + box.width / 2, oy = box.y + box.height / 2;
        int axis = 0;
        for (const css_token& t : normalize(el->css().get_transform_origin(), f_componentize))
        {
            if (t.type == WHITESPACE)
                continue;
            std::string word = t.type == IDENT ? lowerAscii(t.name()) : "";
            if (word == "left")
                ox = box.x;
            else if (word == "right")
                ox = box.x + box.width;
            else if (word == "top")
                oy = box.y;
            else if (word == "bottom")
                oy = box.y + box.height;
            else if (word.empty() && axis == 0)
                ox = box.x + cssLength(t, fontSize, box.width);
            else if (word.empty() && axis == 1)
                oy = box.y + cssLength(t, fontSize, box.height);
            axis++;
        }
        nvgTranslate(vg, ox, oy);
        for (const css_token& fn : normalize(el->css().get_transform(), f_componentize))
        {
            if (fn.type != CV_FUNCTION)
                continue;
            std::vector<css_token> args;
            for (const css_token& a : fn.value)
                if (a.type != WHITESPACE && a.type != COMMA)
                    args.push_back(a);
            if (args.empty())
                continue;
            std::string name = lowerAscii(fn.name());
            auto arg = [&](size_t i, float percentOf) {
                return i < args.size() ? cssLength(args[i], fontSize, percentOf) : 0.0f;
            };
            auto number = [&](size_t i, float fallback) {
                if (i >= args.size())
                    return fallback;
                return args[i].type == PERCENTAGE ? args[i].n.number / 100 : args[i].n.number;
            };
            if (name == "rotate" || name == "rotatez")
                nvgRotate(vg, cssAngle(args[0]));
            else if (name == "translate")
                nvgTranslate(vg, arg(0, box.width), arg(1, box.height));
            else if (name == "translatex")
                nvgTranslate(vg, arg(0, box.width), 0);
            else if (name == "translatey")
                nvgTranslate(vg, 0, arg(0, box.height));
            else if (name == "scale")
                nvgScale(vg, number(0, 1), number(1, number(0, 1)));
            else if (name == "scalex")
                nvgScale(vg, number(0, 1), 1);
            else if (name == "scaley")
                nvgScale(vg, 1, number(0, 1));
            else if (name == "skew" || name == "skewx")
            {
                nvgSkewX(vg, cssAngle(args[0]));
                if (name == "skew" && args.size() > 1)
                    nvgSkewY(vg, cssAngle(args[1]));
            }
            else if (name == "skewy")
                nvgSkewY(vg, cssAngle(args[0]));
            else if (name == "matrix" && args.size() == 6)
                nvgTransform(vg, number(0, 1), number(1, 0), number(2, 0), number(3, 1), number(4, 0), number(5, 0));
        }
        nvgTranslate(vg, -ox, -oy);
    }

    void end_transform(uint_ptr, const element::ptr&) override { nvgRestore(vg); }

    void get_viewport(position& viewport) const override
    {
        float w  = view->layoutWidth > 0 ? view->layoutWidth : 1280;
        viewport = position(0, 0, w, view->viewHeight / view->scale());
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

    // Up/Down scroll, or move the highlight once there is one (past either
    // end it goes away and scrolling resumes). Left/Right move the highlight.
    // borealis sends a D-pad press as both BUTTON_<dir> and BUTTON_NAV_<dir>
    // (the stick only as NAV), so only the NAV buttons are registered.
    auto vertical = [this](int delta) {
        if (focusIndex >= 0 && moveFocus(delta))
            return true;
        clearElementFocus();
        this->scrollBy(160.0f * delta);
        return true;
    };
    // (Unless the user bound Left/Right to a reviewer action.)
    auto horizontal = [this](brls::ControllerButton button, int delta) {
        if (focusList.empty() || !controls::actionsFor(controls::activeLayout(), button).empty())
            return false;
        moveFocus(delta);
        return true;
    };
    this->registerAction("", brls::BUTTON_NAV_UP, [vertical](brls::View*) { return vertical(-1); }, true, true);
    this->registerAction("", brls::BUTTON_NAV_DOWN, [vertical](brls::View*) { return vertical(1); }, true, true);
    this->registerAction(
        "", brls::BUTTON_NAV_LEFT, [horizontal](brls::View*) { return horizontal(brls::BUTTON_LEFT, -1); }, true, true);
    this->registerAction(
        "", brls::BUTTON_NAV_RIGHT, [horizontal](brls::View*) { return horizontal(brls::BUTTON_RIGHT, 1); }, true, true);
    // With a highlight, A taps it and B removes it; otherwise both go on to
    // the reviewer (show answer / grade, back).
    this->registerAction(
        "", brls::BUTTON_A, [this](brls::View*) { return activateFocused(); }, true);
    this->registerAction(
        "", brls::BUTTON_B, [this](brls::View*) {
            if (focusIndex < 0)
                return false;
            clearElementFocus();
            return true;
        },
        true);

    // Touch: drag to scroll, tap to activate links (or flip the card).
    // Any direction: vertical movement scrolls; a long, mostly straight pan
    // is also reported as a swipe (vertical ones only on cards that don't
    // scroll, where dragging can't mean scrolling).
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound*) {
            if (whiteboard)
            {
                if (!strokeOpen)
                    strokes.push_back({ { status.startPosition.x - this->getX(),
                                          status.startPosition.y - this->getY() } });
                strokes.back().push_back({ status.position.x - this->getX(), status.position.y - this->getY() });
                strokeOpen = status.state != brls::GestureState::END && status.state != brls::GestureState::FAILED &&
                             status.state != brls::GestureState::INTERRUPTED;
                return;
            }
            if (status.state == brls::GestureState::STAY || status.state == brls::GestureState::END)
                this->scrollBy(-status.delta.y);
            if (status.state != brls::GestureState::END || !onSwipe)
                return;
            float dx = status.position.x - status.startPosition.x;
            float dy = status.position.y - status.startPosition.y;
            constexpr float MIN_SWIPE = 150;
            if (std::fabs(dx) >= MIN_SWIPE && std::fabs(dx) > 2 * std::fabs(dy))
                onSwipe(dx < 0 ? Swipe::Left : Swipe::Right);
            else if (std::fabs(dy) >= MIN_SWIPE && std::fabs(dy) > 2 * std::fabs(dx) && maxScroll() <= 0)
                onSwipe(dy < 0 ? Swipe::Up : Swipe::Down);
        },
        brls::PanAxis::ANY));
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
        clearElementFocus();
    }
}

void CardView::scrollToAnswerAfterLayout() { scrollToAnswer = true; }

void CardView::setOcclusions(std::vector<Occlusion> shapes) { occlusions = std::move(shapes); }

void CardView::setMasksHidden(bool hidden) { masksHidden = hidden; }

void CardView::setMediaDir(const std::string& dir) { container->mediaDir = dir; }

void CardView::setOnLink(std::function<void(const std::string&)> callback) { onLink = std::move(callback); }

void CardView::setOnTap(std::function<void()> callback) { onTap = std::move(callback); }

void CardView::setOnElementTap(std::function<bool(int, float, float)> callback) { onElementTap = std::move(callback); }

void CardView::setZoom(float value)
{
    zoom        = std::clamp(value, 0.6f, 3.0f);
    needsLayout = true;
}

void CardView::setScreenScale(float value)
{
    if (value == screenScale)
        return;
    screenScale = value;
    needsLayout = true;
}

float CardView::maxScroll() const
{
    if (!document)
        return 0;
    return std::max(0.0f, (float)document->height() * scale() - viewHeight);
}

void CardView::scrollBy(float dy) { scroll = std::clamp(scroll + dy, 0.0f, maxScroll()); }

float CardView::cssWidth()
{
    if (layoutWidth > 0)
        return layoutWidth;
    return getWidth() > 0 ? getWidth() / scale() : 960.0f;
}

float CardView::cssHeight() { return (viewHeight > 0 ? viewHeight : getHeight()) / scale(); }

void CardView::scrollToCss(float y) { scroll = std::clamp(y * scale(), 0.0f, maxScroll()); }

namespace
{

template <class Names>
std::string cssName(const Names& names, int value)
{
    return value >= 0 && value < (int)names.size() ? std::string(names[value]) : std::string();
}

std::string cssColor(const web_color& c)
{
    char text[64];
    if (c.alpha == 255)
        std::snprintf(text, sizeof text, "rgb(%d, %d, %d)", c.red, c.green, c.blue);
    else
        std::snprintf(text, sizeof text, "rgba(%d, %d, %d, %.3g)", c.red, c.green, c.blue, c.alpha / 255.0);
    return text;
}

// What an element covers on the page, in document coordinates: its border
// box, or for an inline element (a <span>, a link inside text) the union of
// its line fragments; get_placement() has no size for those.
bool renderedBox(const element::ptr& el, float& x, float& y, float& width, float& height)
{
    auto ri = el->get_render_item();
    if (!ri)
        return false;
    float left = 0, top = 0, right = 0, bottom = 0;
    bool any = false;
    ri->get_rendering_boxes([&](const position& p) {
        float w = (float)p.width, h = (float)p.height;
        if (w <= 0 && h <= 0)
            return;
        float l = (float)p.x, t = (float)p.y, r = l + w, b = t + h;
        if (!any)
            left = l, top = t, right = r, bottom = b;
        left   = std::min(left, l);
        top    = std::min(top, t);
        right  = std::max(right, r);
        bottom = std::max(bottom, b);
        any    = true;
    });
    if (any)
        x = left, y = top, width = right - left, height = bottom - top;
    return any;
}

std::string cssPixels(float value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%gpx", value);
    return text;
}

// Night mode makes the card's text light. Where the card paints a light
// background of its own (a highlight, a code block), that text would be light
// on light (as it is in Anki and AnkiDroid): text there still in night mode's
// colour is made dark. Colours the card sets itself are kept.
void darkenTextOnLightBackgrounds(const document::ptr& doc)
{
    element::ptr root  = doc ? doc->root() : nullptr;
    element::ptr night = root ? root->select_one(".nightMode") : nullptr;
    if (!night)
        return;
    auto luminance = [](web_color c) { return (0.2126 * c.red + 0.7152 * c.green + 0.0722 * c.blue) / 255.0; };
    web_color nightText = night->css().get_color();
    if (luminance(nightText) < 0.5)
        return;
    std::function<void(const element::ptr&, bool)> visit = [&](const element::ptr& el, bool onLight) {
        web_color bg = el->css().get_bg().m_color;
        if (bg.alpha >= 128)
            onLight = luminance(bg) > 0.6;
        if (onLight && el->css().get_color() == nightText)
            el->css_w().set_color(web_color(32, 32, 32));
        for (const auto& child : el->children())
            visit(child, onLight);
    };
    visit(night, false);
}

} // namespace

CardScript::Layout CardView::measure(const std::string& page)
{
    CardScript::Layout boxes;
    if (!container->vg)
        container->vg = brls::Application::getNVGContext(); // not drawn yet
    if (!container->vg)
        return boxes;
    container->fontFaces = parseFontFaces(page);
    document::ptr doc    = document::createFromString(page, container.get());
    if (!doc)
        return boxes;
    darkenTextOnLightBackgrounds(doc);
    doc->render(cssWidth());

    std::function<void(const element::ptr&)> visit = [&](const element::ptr& el) {
        if (const char* serial = el->get_attr("data-ankinx-node"))
        {
            CardScript::Box box;
            if (auto ri = el->get_render_item())
            {
                // get_placement() is the content box.
                position p       = el->get_placement();
                float padX       = ri->padding_left() + ri->padding_right();
                float padY       = ri->padding_top() + ri->padding_bottom();
                box.clientLeft   = ri->border_left();
                box.clientTop    = ri->border_top();
                box.x            = p.x - ri->padding_left() - ri->border_left();
                box.y            = p.y - ri->padding_top() - ri->border_top();
                box.clientWidth  = p.width + padX;
                box.clientHeight = p.height + padY;
                box.width        = box.clientWidth + ri->border_left() + ri->border_right();
                box.height       = box.clientHeight + ri->border_top() + ri->border_bottom();
                // Inline elements: what their line fragments cover.
                renderedBox(el, box.x, box.y, box.width, box.height);
            }
            const css_properties& css = el->css();
            box.style = {
                { "display", cssName(style_display_strings, css.get_display()) },
                { "visibility", cssName(visibility_strings, css.get_visibility()) },
                { "position", cssName(element_position_strings, css.get_position()) },
                { "float", cssName(element_float_strings, css.get_float()) },
                { "text-align", cssName(text_align_strings, css.get_text_align()) },
                { "overflow", cssName(overflow_strings, css.get_overflow()) },
                { "font-size", cssPixels(css.get_font_size()) },
                { "color", cssColor(css.get_color()) },
                { "background-color", cssColor(css.get_bg().m_color) },
            };
            boxes[std::atoi(serial)] = std::move(box);
        }
        for (const auto& child : el->children())
            visit(child);
    };
    if (auto root = doc->root())
        visit(root);
    return boxes;
}

void CardView::layout(float width)
{
    auto start = std::chrono::steady_clock::now();
    container->clearImages();
    container->fontFaces = parseFontFaces(html);
    layoutWidth = width / scale();
    document    = document::createFromString(html, container.get());
    darkenTextOnLightBackgrounds(document);
    if (document)
        document->render(layoutWidth);
    needsLayout = false;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    brls::Logger::info("card layout: {} ms", ms);

    if (scrollToAnswer && document)
    {
        scrollToAnswer = false;
        if (auto root = document->root())
            if (auto hr = root->select_one("#answer"))
                scroll = std::clamp((float)hr->get_placement().y * scale() - 20.0f, 0.0f, maxScroll());
    }

    // Keep the highlight on the same element (scripts re-render the card).
    focusList = focusables();
    if (focusIndex >= 0)
    {
        int found = -1;
        for (size_t i = 0; i < focusList.size() && focusSerial; i++)
            if (const char* s = focusList[i]->get_attr("data-ankinx-node"); s && std::atoi(s) == focusSerial)
                found = (int)i;
        if (found < 0 && !focusList.empty())
            found = std::min(focusIndex, (int)focusList.size() - 1);
        focusIndex = found;
        if (found < 0)
            focusSerial = 0;
    }
}

namespace
{

// Placement in document coordinates, as plain floats.
struct Box
{
    float x, y, width, height;
};

Box placement(const element::ptr& el)
{
    Box box;
    if (renderedBox(el, box.x, box.y, box.width, box.height))
        return box;
    position p = el->get_placement();
    return { (float)p.x, (float)p.y, (float)p.width, (float)p.height };
}

bool isFocusable(const element::ptr& el)
{
    const char* tagName = el->get_tagName();
    if (!tagName || !*tagName)
        return false;
    std::string tag = tagName;
    const char* type = el->get_attr("type", "");
    bool candidate   = el->get_attr("data-ankinx-click") || el->get_attr("onclick") ||
                     (tag == "a" && el->get_attr("href")) || tag == "button" || tag == "select" ||
                     tag == "textarea" || tag == "label" || tag == "summary" ||
                     (tag == "input" && strcasecmp(type, "hidden") != 0);
    if (!candidate || el->css().get_visibility() != visibility_visible)
        return false;
    Box p = placement(el);
    return p.width > 0 && p.height > 0;
}

// Outermost focusable elements, in document order (a label, not also the
// checkbox inside it).
void collectFocusable(const element::ptr& el, std::vector<element::ptr>& out)
{
    if (isFocusable(el))
    {
        out.push_back(el);
        return;
    }
    for (const auto& child : el->children())
        collectFocusable(child, out);
}

} // namespace

std::vector<litehtml::element::ptr> CardView::focusables() const
{
    std::vector<element::ptr> out;
    if (document && document->root())
        collectFocusable(document->root(), out);
    return out;
}

bool CardView::moveFocus(int delta)
{
    if (focusList.empty())
        return false;
    int next;
    if (focusIndex < 0)
    {
        // Start at the first element on screen (or the last, going back).
        float top = scroll / scale(), bottom = (scroll + viewHeight) / scale();
        next      = delta > 0 ? 0 : (int)focusList.size() - 1;
        for (size_t i = 0; i < focusList.size(); i++)
        {
            size_t k   = delta > 0 ? i : focusList.size() - 1 - i;
            Box p = placement(focusList[k]);
            if (p.y + p.height > top && p.y < bottom)
            {
                next = (int)k;
                break;
            }
        }
    }
    else
    {
        next = focusIndex + delta;
        if (next < 0 || next >= (int)focusList.size())
            return false;
    }
    focusIndex          = next;
    const char* serial  = focusList[next]->get_attr("data-ankinx-node");
    focusSerial         = serial ? std::atoi(serial) : 0;

    // Scroll it into view, with a margin.
    Box p    = placement(focusList[next]);
    float top     = p.y * scale() - 24.0f;
    float bottom  = (p.y + p.height) * scale() + 24.0f;
    if (top < scroll || bottom - top > viewHeight)
        scroll = top;
    else if (bottom > scroll + viewHeight)
        scroll = bottom - viewHeight;
    scroll = std::clamp(scroll, 0.0f, maxScroll());
    return true;
}

void CardView::clearElementFocus()
{
    focusIndex  = -1;
    focusSerial = 0;
}

bool CardView::activateFocused()
{
    if (focusIndex < 0 || focusIndex >= (int)focusList.size())
        return false;
    Box p = placement(focusList[focusIndex]);
    tapAt(p.x + p.width / 2.0f, p.y + p.height / 2.0f);
    return true;
}

void CardView::drawFocus(NVGcontext* vg)
{
    if (focusIndex < 0 || focusIndex >= (int)focusList.size())
        return;
    Box p  = placement(focusList[focusIndex]);
    float pad   = 4.0f / scale();
    nvgBeginPath(vg);
    nvgRoundedRect(vg, p.x - pad, p.y - pad, p.width + 2 * pad, p.height + 2 * pad, 6.0f / scale());
    nvgStrokeColor(vg, nvgRGB(13, 182, 213)); // borealis' highlight colour
    nvgStrokeWidth(vg, 3.0f / scale());
    nvgStroke(vg);
}

void CardView::drawWhiteboard(NVGcontext* vg, float x, float y)
{
    if (!whiteboard || strokes.empty())
        return;
    bool dark = brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;
    nvgSave(vg);
    nvgScissor(vg, x, y, this->getWidth(), this->getHeight());
    nvgStrokeColor(vg, dark ? nvgRGBA(240, 240, 240, 230) : nvgRGBA(30, 30, 30, 230));
    nvgStrokeWidth(vg, 4.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    for (const auto& stroke : strokes)
    {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + stroke[0].x, y + stroke[0].y);
        for (size_t i = 1; i < stroke.size(); i++)
            nvgLineTo(vg, x + stroke[i].x, y + stroke[i].y);
        nvgStroke(vg);
    }
    nvgRestore(vg);
}

void CardView::handleTap(brls::Point point)
{
    if (!document || whiteboard)
        return;
    // Screen point -> document coordinates.
    float x = (point.x - this->getX()) / scale(), y = (point.y - this->getY() + scroll) / scale();
    adjustTouch(x, y);
    tapAt(x, y);
}

// A finger tap that misses every tappable element but lands close to one goes
// to the nearest, as phone browsers do ("touch adjustment"): template words
// and buttons are small for a finger, and a near miss would flip the card.
void CardView::adjustTouch(float& x, float& y) const
{
    const float slop = 32.0f / scale(); // screen pixels, in document units
    float best       = slop;
    const element::ptr* nearest = nullptr;
    for (const element::ptr& el : focusList)
    {
        Box p    = placement(el);
        float dx = std::max({ p.x - x, 0.0f, x - (p.x + p.width) });
        float dy = std::max({ p.y - y, 0.0f, y - (p.y + p.height) });
        if (dx == 0 && dy == 0)
            return; // on one already
        float d = std::hypot(dx, dy);
        if (d < best)
        {
            best    = d;
            nearest = &el;
        }
    }
    if (!nearest)
        return;
    Box p = placement(*nearest);
    x     = std::clamp(x, p.x + 1, p.x + std::max(1.0f, p.width - 1));
    y     = std::clamp(y, p.y + 1, p.y + std::max(1.0f, p.height - 1));
}

void CardView::tapAt(float docX, float docY)
{
    if (!document)
        return;
    auto redraw   = [](const position&) {}; // the whole view is redrawn every frame anyway
    linkActivated = false;
    // litehtml acts on the element found by the last mouse-over; touch has no
    // hover, so move the pointer there first.
    document->on_mouse_over(docX, docY, docX, docY, redraw);
    // Card scripts first: the page runs its handlers and then the element's
    // default action (a link's included), as a browser would. The innermost
    // element with a node serial is the target.
    if (onElementTap)
        for (auto el = std::const_pointer_cast<litehtml::element>(document->get_over_element()); el; el = el->parent())
            if (const char* serial = el->get_attr("data-ankinx-node"))
            {
                if (onElementTap(std::atoi(serial), docX, docY))
                    return;
                break;
            }
    document->on_lbutton_down(docX, docY, docX, docY, redraw);
    document->on_lbutton_up(docX, docY, docX, docY, redraw); // may call on_anchor_click
    if (linkActivated)
        return;
    // Our links (replay, hints) can lose their <a> in broken card HTML, as in
    // `<div style=... {{Back}}</div>` without the tag's ">": the field's play
    // link then becomes attributes of the div, href included. Anki still plays
    // when that div is clicked (its onclick moves there too), so do the same.
    for (auto el = document->get_over_element(); el; el = el->parent())
        if (const char* href = el->get_attr("href"))
        {
            std::string url = href;
            if (url.rfind("playsound:", 0) == 0 || url.rfind("anki-hint:", 0) == 0)
            {
                if (onLink)
                    onLink(url);
                return;
            }
            break;
        }
    if (onTap)
        onTap();
}

void CardView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*)
{
    container->vg = vg;
    viewHeight    = height;
    if (needsLayout || std::fabs(width / scale() - layoutWidth) > 0.5f)
        layout(width);
    if (!document)
        return;
    scroll = std::clamp(scroll, 0.0f, maxScroll());

    nvgSave(vg);
    nvgScissor(vg, x, y, width, height);
    nvgTranslate(vg, x, y - scroll);
    nvgScale(vg, scale(), scale());
    position clip(0, scroll / scale(), width / scale(), height / scale());
    document->draw(0, 0, 0, &clip);
    drawFocus(vg);
    nvgRestore(vg);
    drawWhiteboard(vg, x, y);

    // Scroll indicator when the card is taller than the screen.
    float total = (float)document->height() * scale();
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
