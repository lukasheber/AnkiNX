#include "forest/pixel_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace
{

// a * b / 255, rounded
inline int mul(int a, int b)
{
    int t = a * b + 128;
    return (t + (t >> 8)) >> 8;
}

bool real(double v) { return std::isfinite(v); }

// ---------- CSS colours ----------

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

uint8_t byte(double v) { return (uint8_t)std::lround(std::clamp(v, 0.0, 255.0)); }

// The numbers inside "fn(...)", separated by commas, spaces or a slash; a
// trailing % is reported per number.
bool numbers(const std::string& s, size_t from, std::vector<double>& out, std::vector<bool>& pct)
{
    const char* p   = s.c_str() + from;
    const char* end = s.c_str() + s.size();
    while (p < end)
    {
        while (p < end && (*p == ' ' || *p == ',' || *p == '/' || *p == '\t'))
            p++;
        if (p < end && *p == ')')
            return true;
        char* next;
        double v = std::strtod(p, &next);
        if (next == p)
            return false;
        p = next;
        bool percent = p < end && *p == '%';
        if (percent)
            p++;
        else if (p + 3 <= end && std::strncmp(p, "deg", 3) == 0)
            p += 3;
        out.push_back(v);
        pct.push_back(percent);
    }
    return false;
}

double hue(double p, double q, double t)
{
    if (t < 0)
        t += 1;
    if (t > 1)
        t -= 1;
    if (t < 1.0 / 6)
        return p + (q - p) * 6 * t;
    if (t < 0.5)
        return q;
    if (t < 2.0 / 3)
        return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

bool parseColour(const std::string& css, uint8_t out[4])
{
    std::string s;
    for (char c : css)
        if (c != ' ' || !s.empty())
            s += (char)std::tolower((unsigned char)c);
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    if (s.empty())
        return false;
    if (s[0] == '#')
    {
        size_t n = s.size() - 1;
        int d[8];
        for (size_t i = 0; i < n && i < 8; i++)
            if ((d[i] = hexDigit(s[i + 1])) < 0)
                return false;
        if (n == 3 || n == 4)
        {
            for (size_t i = 0; i < 4; i++)
                out[i] = i < n ? (uint8_t)(d[i] * 17) : 255;
            return true;
        }
        if (n == 6 || n == 8)
        {
            for (size_t i = 0; i < 4; i++)
                out[i] = i * 2 < n ? (uint8_t)(d[i * 2] * 16 + d[i * 2 + 1]) : 255;
            return true;
        }
        return false;
    }
    size_t open = s.find('(');
    if (open != std::string::npos)
    {
        std::string fn = s.substr(0, open);
        std::vector<double> v;
        std::vector<bool> pct;
        if (!numbers(s, open + 1, v, pct) || v.size() < 3)
            return false;
        double a = 1;
        if (v.size() >= 4)
            a = pct[3] ? v[3] / 100 : v[3];
        if (fn == "rgb" || fn == "rgba")
        {
            for (int i = 0; i < 3; i++)
                out[i] = byte(pct[i] ? v[i] * 2.55 : v[i]);
        }
        else if (fn == "hsl" || fn == "hsla")
        {
            double hh = std::fmod(std::fmod(v[0], 360) + 360, 360) / 360, sat = v[1] / 100, l = v[2] / 100;
            sat = std::clamp(sat, 0.0, 1.0);
            l   = std::clamp(l, 0.0, 1.0);
            double q = l < 0.5 ? l * (1 + sat) : l + sat - l * sat, p = 2 * l - q;
            out[0] = byte(hue(p, q, hh + 1.0 / 3) * 255);
            out[1] = byte(hue(p, q, hh) * 255);
            out[2] = byte(hue(p, q, hh - 1.0 / 3) * 255);
        }
        else
            return false;
        out[3] = byte(a * 255);
        return true;
    }
    static const std::unordered_map<std::string, uint32_t> names = {
        { "transparent", 0x00000000 }, { "black", 0x000000ff }, { "white", 0xffffffff },
        { "red", 0xff0000ff },         { "green", 0x008000ff }, { "blue", 0x0000ffff },
        { "yellow", 0xffff00ff },      { "gray", 0x808080ff },  { "grey", 0x808080ff },
        { "orange", 0xffa500ff },
    };
    auto it = names.find(s);
    if (it == names.end())
        return false;
    for (int i = 0; i < 4; i++)
        out[i] = (uint8_t)(it->second >> (24 - 8 * i));
    return true;
}

PixelCanvas::Transform times(const PixelCanvas::Transform& m, const PixelCanvas::Transform& n)
{
    // m then n, as canvas' transform() multiplies: current * new
    return { m.a * n.a + m.c * n.b,       m.b * n.a + m.d * n.b,       m.a * n.c + m.c * n.d,
             m.b * n.c + m.d * n.d,       m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f };
}

bool invert(const PixelCanvas::Transform& m, PixelCanvas::Transform& out)
{
    double det = m.a * m.d - m.b * m.c;
    if (det == 0 || !real(det))
        return false;
    out = { m.d / det, -m.b / det, -m.c / det, m.a / det, (m.c * m.f - m.d * m.e) / det, (m.b * m.e - m.a * m.f) / det };
    return true;
}

// The first pixel whose centre is at or after `v`.
inline int pixelAt(double v) { return (int)std::ceil(v - 0.5); }

} // namespace

PixelCanvas::PixelCanvas(int width, int height) { resize(width, height); }

void PixelCanvas::resize(int width, int height)
{
    w = std::max(0, width);
    h = std::max(0, height);
    px.assign((size_t)w * h * 4, 0);
    st = State();
    stack.clear();
}

bool PixelCanvas::setFill(const std::string& css)
{
    // The scenes set the same few hundred colours over and over.
    static std::unordered_map<std::string, uint32_t> cache;
    auto it = cache.find(css);
    if (it != cache.end())
    {
        std::memcpy(st.fill, &it->second, 4);
        return true;
    }
    uint8_t c[4];
    if (!parseColour(css, c))
        return false;
    if (cache.size() > 8192)
        cache.clear();
    uint32_t packed;
    std::memcpy(&packed, c, 4);
    cache.emplace(css, packed);
    std::memcpy(st.fill, c, 4);
    return true;
}

void PixelCanvas::setAlpha(double alpha)
{
    if (real(alpha) && alpha >= 0 && alpha <= 1)
        st.alpha = alpha;
}

bool PixelCanvas::setOp(const std::string& name)
{
    static const std::pair<const char*, Op> ops[] = {
        { "source-over", Op::SourceOver },   { "lighter", Op::Lighter },
        { "source-atop", Op::SourceAtop },   { "source-in", Op::SourceIn },
        { "destination-out", Op::DestinationOut }, { "destination-in", Op::DestinationIn },
        { "destination-over", Op::DestinationOver }, { "copy", Op::Copy },
    };
    for (const auto& [n, op] : ops)
        if (name == n)
        {
            st.op = op;
            return true;
        }
    return false;
}

void PixelCanvas::save() { stack.push_back(st); }

void PixelCanvas::restore()
{
    if (stack.empty())
        return;
    st = stack.back();
    stack.pop_back();
}

void PixelCanvas::setTransform(const Transform& t)
{
    if (real(t.a) && real(t.b) && real(t.c) && real(t.d) && real(t.e) && real(t.f))
        st.t = t;
}

void PixelCanvas::transform(const Transform& t) { setTransform(times(st.t, t)); }

// ---------- coverage ----------

void PixelCanvas::coverRect(double x, double y, double rw, double rh, std::vector<Span>& out) const
{
    if (!real(x) || !real(y) || !real(rw) || !real(rh) || rw == 0 || rh == 0)
        return;
    const Transform& t = st.t;
    if (t.b == 0 && t.c == 0)
    {
        double x0 = t.a * x + t.e, x1 = t.a * (x + rw) + t.e;
        double y0 = t.d * y + t.f, y1 = t.d * (y + rh) + t.f;
        if (x0 > x1)
            std::swap(x0, x1);
        if (y0 > y1)
            std::swap(y0, y1);
        int px0 = std::max(0, pixelAt(x0)), px1 = std::min(w, pixelAt(x1));
        int py0 = std::max(0, pixelAt(y0)), py1 = std::min(h, pixelAt(y1));
        if (px0 >= px1)
            return;
        for (int r = py0; r < py1; r++)
            out.push_back({ r, px0, px1 });
        return;
    }
    std::vector<double> pts = { x, y, x + rw, y, x + rw, y + rh, x, y + rh };
    for (size_t i = 0; i < pts.size(); i += 2)
    {
        double ux = pts[i], uy = pts[i + 1];
        pts[i]     = t.a * ux + t.c * uy + t.e;
        pts[i + 1] = t.b * ux + t.d * uy + t.f;
    }
    coverPolygon(pts, { 0 }, out);
}

// Nonzero fill of closed polygons (device coordinates): `pts` is x, y pairs,
// `starts` the index (in points) where each one begins.
void PixelCanvas::coverPolygon(const std::vector<double>& pts, const std::vector<size_t>& starts,
                               std::vector<Span>& out) const
{
    struct Edge
    {
        double x0, y0, x1, y1;
        int dir;
    };
    std::vector<Edge> edges;
    size_t count = pts.size() / 2;
    double top = INFINITY, bottom = -INFINITY;
    for (size_t k = 0; k < starts.size(); k++)
    {
        size_t from = starts[k], to = k + 1 < starts.size() ? starts[k + 1] : count;
        for (size_t i = from; i < to; i++)
        {
            size_t j  = i + 1 < to ? i + 1 : from;
            double x0 = pts[i * 2], y0 = pts[i * 2 + 1], x1 = pts[j * 2], y1 = pts[j * 2 + 1];
            if (!real(x0) || !real(y0) || !real(x1) || !real(y1) || y0 == y1)
                continue;
            int dir = 1;
            if (y0 > y1)
            {
                std::swap(x0, x1);
                std::swap(y0, y1);
                dir = -1;
            }
            edges.push_back({ x0, y0, x1, y1, dir });
            top    = std::min(top, y0);
            bottom = std::max(bottom, y1);
        }
    }
    if (edges.empty())
        return;
    int r0 = std::max(0, pixelAt(top)), r1 = std::min(h, pixelAt(bottom));
    std::vector<std::pair<double, int>> cross;
    for (int r = r0; r < r1; r++)
    {
        double yc = r + 0.5;
        cross.clear();
        for (const Edge& e : edges)
            if (yc >= e.y0 && yc < e.y1)
                cross.push_back({ e.x0 + (yc - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.dir });
        std::sort(cross.begin(), cross.end());
        int wind = 0;
        for (size_t i = 0; i + 1 < cross.size(); i++)
        {
            wind += cross[i].second;
            if (wind == 0)
                continue;
            int x0 = std::max(0, pixelAt(cross[i].first)), x1 = std::min(w, pixelAt(cross[i + 1].first));
            if (x0 < x1)
                out.push_back({ r, x0, x1 });
        }
    }
}

void PixelCanvas::cover(const double* ops, size_t n, std::vector<Span>& out) const
{
    const Transform& t = st.t;
    std::vector<double> pts;
    std::vector<size_t> starts;
    auto add = [&](double x, double y) {
        pts.push_back(t.a * x + t.c * y + t.e);
        pts.push_back(t.b * x + t.d * y + t.f);
    };
    for (size_t i = 0; i < n;)
    {
        switch ((int)ops[i])
        {
        case RECT:
            if (i + 5 > n)
                return;
            // (each rectangle on its own: the scenes' paths of many never overlap them)
            coverRect(ops[i + 1], ops[i + 2], ops[i + 3], ops[i + 4], out);
            i += 5;
            break;
        case MOVE:
            if (i + 3 > n)
                return;
            starts.push_back(pts.size() / 2);
            add(ops[i + 1], ops[i + 2]);
            i += 3;
            break;
        case LINE:
            if (i + 3 > n)
                return;
            if (starts.empty())
                starts.push_back(0);
            add(ops[i + 1], ops[i + 2]);
            i += 3;
            break;
        default:
            i += 1;
            break;
        }
    }
    if (!pts.empty())
        coverPolygon(pts, starts, out);
}

// ---------- painting ----------

void PixelCanvas::blend(uint8_t* d, const uint8_t* s, uint8_t clip) const
{
    uint8_t c[4];
    if (clip != 255)
    {
        for (int i = 0; i < 4; i++)
            c[i] = (uint8_t)mul(s[i], clip);
        s = c;
    }
    int sa = s[3], da = d[3];
    switch (st.op)
    {
    case Op::SourceOver:
        for (int i = 0; i < 4; i++)
            d[i] = (uint8_t)(s[i] + mul(d[i], 255 - sa));
        break;
    case Op::Lighter:
        for (int i = 0; i < 4; i++)
            d[i] = (uint8_t)std::min(255, s[i] + d[i]);
        break;
    case Op::SourceAtop:
        for (int i = 0; i < 3; i++)
            d[i] = (uint8_t)(mul(s[i], da) + mul(d[i], 255 - sa));
        break;
    case Op::SourceIn:
        for (int i = 0; i < 4; i++)
            d[i] = (uint8_t)mul(s[i], da);
        break;
    case Op::DestinationOut:
        for (int i = 0; i < 4; i++)
            d[i] = (uint8_t)mul(d[i], 255 - sa);
        break;
    case Op::DestinationIn:
        for (int i = 0; i < 4; i++)
            d[i] = (uint8_t)mul(d[i], sa);
        break;
    case Op::DestinationOver:
        for (int i = 0; i < 4; i++)
            d[i] = (uint8_t)(d[i] + mul(s[i], 255 - da));
        break;
    case Op::Copy:
        std::memcpy(d, s, 4);
        break;
    }
}

void PixelCanvas::paint(const std::vector<Span>& list)
{
    int a = (int)std::lround(st.fill[3] * st.alpha);
    uint8_t s[4] = { (uint8_t)mul(st.fill[0], a), (uint8_t)mul(st.fill[1], a), (uint8_t)mul(st.fill[2], a),
                     (uint8_t)a };
    if (a == 0 && (st.op == Op::SourceOver || st.op == Op::Lighter || st.op == Op::SourceAtop ||
                   st.op == Op::DestinationOut || st.op == Op::DestinationOver))
        return;
    const uint8_t* clip = st.clip ? st.clip->data() : nullptr;
    bool opaque         = a == 255 && (st.op == Op::SourceOver || st.op == Op::Copy) && !clip;
    uint32_t packed;
    std::memcpy(&packed, s, 4);
    for (const Span& sp : list)
    {
        uint8_t* row = px.data() + ((size_t)sp.y * w) * 4;
        if (opaque)
        {
            for (int x = sp.x0; x < sp.x1; x++)
                std::memcpy(row + x * 4, &packed, 4);
            continue;
        }
        for (int x = sp.x0; x < sp.x1; x++)
        {
            uint8_t m = clip ? clip[(size_t)sp.y * w + x] : 255;
            if (m)
                blend(row + x * 4, s, m);
        }
    }
}

void PixelCanvas::fillRect(double x, double y, double rw, double rh)
{
    spans.clear();
    coverRect(x, y, rw, rh, spans);
    paint(spans);
}

void PixelCanvas::clearRect(double x, double y, double rw, double rh)
{
    spans.clear();
    coverRect(x, y, rw, rh, spans);
    const uint8_t* clip = st.clip ? st.clip->data() : nullptr;
    for (const Span& sp : spans)
    {
        uint8_t* row = px.data() + ((size_t)sp.y * w) * 4;
        if (!clip)
        {
            std::memset(row + sp.x0 * 4, 0, (size_t)(sp.x1 - sp.x0) * 4);
            continue;
        }
        for (int x = sp.x0; x < sp.x1; x++)
            if (clip[(size_t)sp.y * w + x])
                std::memset(row + x * 4, 0, 4);
    }
}

void PixelCanvas::fillPath(const double* ops, size_t n)
{
    spans.clear();
    cover(ops, n, spans);
    paint(spans);
}

void PixelCanvas::clipPath(const double* ops, size_t n)
{
    spans.clear();
    cover(ops, n, spans);
    auto mask = std::make_shared<std::vector<uint8_t>>((size_t)w * h, 0);
    const uint8_t* old = st.clip ? st.clip->data() : nullptr;
    for (const Span& sp : spans)
        for (int x = sp.x0; x < sp.x1; x++)
        {
            size_t i  = (size_t)sp.y * w + x;
            (*mask)[i] = old ? old[i] : 255;
        }
    st.clip = mask;
}

void PixelCanvas::drawImage(const PixelCanvas& src, double sx, double sy, double sw, double sh, double dx, double dy,
                            double dw, double dh)
{
    if (!real(sx) || !real(sy) || !real(sw) || !real(sh) || !real(dx) || !real(dy) || !real(dw) ||
        !real(dh) || sw == 0 || sh == 0 || dw == 0 || dh == 0 || src.w == 0 || src.h == 0)
        return;
    // (both rectangles normalised, as the spec has it: a negative size never flips)
    if (sw < 0)
        sx += sw, sw = -sw;
    if (sh < 0)
        sy += sh, sh = -sh;
    if (dw < 0)
        dx += dw, dw = -dw;
    if (dh < 0)
        dy += dh, dh = -dh;
    // drawing a canvas onto itself reads it as it was
    std::vector<uint8_t> copy;
    const uint8_t* sp = src.px.data();
    if (&src == this)
    {
        copy = px;
        sp   = copy.data();
    }
    Transform inv;
    if (!invert(st.t, inv))
        return;
    const Transform& t = st.t;
    double xs[4] = { dx, dx + dw, dx + dw, dx }, ys[4] = { dy, dy, dy + dh, dy + dh };
    double minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
    for (int i = 0; i < 4; i++)
    {
        double X = t.a * xs[i] + t.c * ys[i] + t.e, Y = t.b * xs[i] + t.d * ys[i] + t.f;
        minX = std::min(minX, X), maxX = std::max(maxX, X), minY = std::min(minY, Y), maxY = std::max(maxY, Y);
    }
    int x0 = std::max(0, pixelAt(minX)), x1 = std::min(w, pixelAt(maxX));
    int y0 = std::max(0, pixelAt(minY)), y1 = std::min(h, pixelAt(maxY));
    if (x0 >= x1 || y0 >= y1)
        return;
    int alpha           = (int)std::lround(st.alpha * 255);
    const uint8_t* clip = st.clip ? st.clip->data() : nullptr;
    double kx = sw / dw, ky = sh / dh;
    int sxMin = std::max(0, (int)std::floor(sx)), syMin = std::max(0, (int)std::floor(sy));
    int sxMax = std::min(src.w, (int)std::ceil(sx + sw)), syMax = std::min(src.h, (int)std::ceil(sy + sh));
    bool axis = t.b == 0 && t.c == 0;
    // (axis-aligned: each column's source column worked out once)
    std::vector<int> cols;
    if (axis)
    {
        cols.resize(x1 - x0);
        for (int x = x0; x < x1; x++)
        {
            double u = inv.a * (x + 0.5) + inv.e;
            cols[x - x0] = (u >= dx && u < dx + dw) ? (int)std::floor(sx + (u - dx) * kx) : -1;
        }
    }
    for (int y = y0; y < y1; y++)
    {
        uint8_t* row = px.data() + ((size_t)y * w) * 4;
        int srow     = -1;
        if (axis)
        {
            double v = inv.d * (y + 0.5) + inv.f;
            if (v < dy || v >= dy + dh)
                continue;
            srow = (int)std::floor(sy + (v - dy) * ky);
            if (srow < syMin || srow >= syMax)
                continue;
        }
        for (int x = x0; x < x1; x++)
        {
            int scol, sr = srow;
            if (axis)
                scol = cols[x - x0];
            else
            {
                double u = inv.a * (x + 0.5) + inv.c * (y + 0.5) + inv.e;
                double v = inv.b * (x + 0.5) + inv.d * (y + 0.5) + inv.f;
                if (u < dx || u >= dx + dw || v < dy || v >= dy + dh)
                    continue;
                scol = (int)std::floor(sx + (u - dx) * kx);
                sr   = (int)std::floor(sy + (v - dy) * ky);
                if (sr < syMin || sr >= syMax)
                    continue;
            }
            if (scol < sxMin || scol >= sxMax)
                continue;
            const uint8_t* s = sp + ((size_t)sr * src.w + scol) * 4;
            if (s[3] == 0 && (st.op == Op::SourceOver || st.op == Op::Lighter || st.op == Op::SourceAtop))
                continue;
            uint8_t m = clip ? clip[(size_t)y * w + x] : 255;
            if (!m)
                continue;
            uint8_t* d = row + x * 4;
            if (alpha == 255 && m == 255 && st.op == Op::SourceOver && s[3] == 255)
            {
                std::memcpy(d, s, 4);
                continue;
            }
            uint8_t c[4];
            for (int i = 0; i < 4; i++)
                c[i] = (uint8_t)mul(s[i], alpha);
            blend(d, c, m);
        }
    }
}

void PixelCanvas::getImageData(int x, int y, int iw, int ih, uint8_t* out) const
{
    for (int r = 0; r < ih; r++)
        for (int c = 0; c < iw; c++)
        {
            uint8_t* o = out + ((size_t)r * iw + c) * 4;
            int X = x + c, Y = y + r;
            if (X < 0 || Y < 0 || X >= w || Y >= h)
            {
                std::memset(o, 0, 4);
                continue;
            }
            const uint8_t* p = px.data() + ((size_t)Y * w + X) * 4;
            int a            = p[3];
            if (a == 255)
                std::memcpy(o, p, 4);
            else if (a == 0)
                std::memset(o, 0, 4);
            else
            {
                for (int i = 0; i < 3; i++)
                    o[i] = (uint8_t)std::min(255, (p[i] * 255 + a / 2) / a);
                o[3] = (uint8_t)a;
            }
        }
}

void PixelCanvas::putImageData(const uint8_t* in, int iw, int ih, int dx, int dy, int dirtyX, int dirtyY, int dirtyW,
                               int dirtyH)
{
    if (dirtyW < 0)
        dirtyX += dirtyW, dirtyW = -dirtyW;
    if (dirtyH < 0)
        dirtyY += dirtyH, dirtyH = -dirtyH;
    int c0 = std::max(0, dirtyX), c1 = std::min(iw, dirtyX + dirtyW);
    int r0 = std::max(0, dirtyY), r1 = std::min(ih, dirtyY + dirtyH);
    for (int r = r0; r < r1; r++)
    {
        int Y = dy + r;
        if (Y < 0 || Y >= h)
            continue;
        for (int c = c0; c < c1; c++)
        {
            int X = dx + c;
            if (X < 0 || X >= w)
                continue;
            const uint8_t* s = in + ((size_t)r * iw + c) * 4;
            uint8_t* d       = px.data() + ((size_t)Y * w + X) * 4;
            int a            = s[3];
            for (int i = 0; i < 3; i++)
                d[i] = (uint8_t)mul(s[i], a);
            d[3] = (uint8_t)a;
        }
    }
}
