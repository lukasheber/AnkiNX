#include "view/forest_heatmap.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>

#include "forest/forest_script.hpp"
#include "forest/forest_thread.hpp"

namespace
{

// Timeline's months: ten, as the add-on shows (calendar.js MONTHS_SHOWN), at twice its size on the screen
constexpr int MONTHS = 10;
// room left of the weeks for the weekdays, and over them for the months
constexpr float DAYS_ROOM = 56, MONTHS_ROOM = 30;
// the heatmap's share of the panel's height, the rest for the chosen day's tooltip
constexpr float SHARE = 0.62f;

NVGcolor text() { return brls::Application::getTheme()["brls/text"]; }

NVGcolor dim()
{
    NVGcolor c = text();
    c.a        = 0.6f;
    return c;
}

void label(NVGcontext* vg, float x, float y, const std::string& s, float size, NVGcolor color, int align)
{
    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    nvgFontSize(vg, size);
    nvgFillColor(vg, color);
    nvgTextAlign(vg, align);
    nvgText(vg, x, y, s.c_str(), nullptr);
}

} // namespace

struct ForestHeatmap::Worker
{
    std::string data;
    std::mutex lock;
    std::condition_variable wake;
    std::deque<std::function<void(ForestScript&)>> jobs;
    bool stop            = false;
    ForestScript* script = nullptr; // (for cancel(), under lock)
    ForestThread thread;

    void post(std::function<void(ForestScript&)> job)
    {
        {
            std::lock_guard<std::mutex> l(lock);
            jobs.push_back(std::move(job));
        }
        wake.notify_one();
    }

    void run()
    {
        std::ifstream in(BRLS_ASSET("forest/forest.js"), std::ios::binary);
        std::string bundle((std::istreambuf_iterator<char>(in)), {});
        ForestScript forest(bundle);
        {
            std::lock_guard<std::mutex> l(lock);
            script = &forest;
        }
        if (!forest.load(data))
            brls::Logger::error("forest heatmap: {}", forest.error());
        while (true)
        {
            std::function<void(ForestScript&)> job;
            {
                std::unique_lock<std::mutex> l(lock);
                wake.wait(l, [this] { return stop || !jobs.empty(); });
                if (stop)
                    break;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            job(forest);
        }
        std::lock_guard<std::mutex> l(lock);
        script = nullptr;
    }
};

ForestHeatmap::ForestHeatmap() = default;

ForestHeatmap::~ForestHeatmap()
{
    *alive = false;
    if (worker)
    {
        {
            std::lock_guard<std::mutex> l(worker->lock);
            worker->stop = true;
            if (worker->script)
                worker->script->cancel();
        }
        worker->wake.notify_one();
        worker->thread.join();
    }
    NVGcontext* vg = brls::Application::getNVGContext();
    if (image)
        nvgDeleteImage(vg, image);
    if (above)
        nvgDeleteImage(vg, above);
}

void ForestHeatmap::setData(const std::string& data)
{
    if (worker || data.empty())
        return;
    worker       = std::make_shared<Worker>();
    worker->data = data;
    Worker* w    = worker.get();
    if (!worker->thread.start([w] { w->run(); }))
        brls::Logger::error("forest heatmap: no thread");
    ask(0);
    chooseDay(0);
}

// The heatmap `months` back or on: drawn on the forest thread, put on screen here.
void ForestHeatmap::ask(int months)
{
    if (!worker)
        return;
    asking            = true;
    NVGcolor bg       = brls::Application::getTheme()["brls/background"];
    bool dark         = brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;
    anki::json args   = { { "page", months }, { "months", MONTHS },
                          { "bg", { (int)(bg.r * 255), (int)(bg.g * 255), (int)(bg.b * 255) } }, { "dark", dark } };
    std::weak_ptr<bool> token = alive;
    worker->post([this, token, args](ForestScript& forest) {
        anki::json out = anki::json::parse(forest.query("heatmap", args.dump()), nullptr, false);
        if (!out.is_object())
            brls::Logger::warning("forest heatmap: nothing to draw: {}", forest.error());
        std::vector<uint8_t> days, marks;
        int w = 0, h = 0, w2 = 0, h2 = 0;
        if (out.is_object())
        {
            forest.canvasPixels("heatmap", days, w, h);
            forest.canvasPixels("heatmapAbove", marks, w2, h2);
        }
        brls::sync([this, token, out, days, marks, w, h] {
            if (auto t = token.lock(); !t || !*t)
                return;
            asking = false;
            if (!out.is_object() || days.empty())
                return;
            NVGcontext* vg = brls::Application::getNVGContext();
            if (image && (imageW != w || imageH != h))
            {
                nvgDeleteImage(vg, image);
                nvgDeleteImage(vg, above);
                image = above = 0;
            }
            int flags = NVG_IMAGE_NEAREST | NVG_IMAGE_PREMULTIPLIED;
            if (!image)
            {
                image = nvgCreateImageRGBA(vg, w, h, flags, days.data());
                above = marks.size() == days.size() ? nvgCreateImageRGBA(vg, w, h, flags, marks.data()) : 0;
            }
            else
            {
                nvgUpdateImage(vg, image, days.data());
                if (above && marks.size() == days.size())
                    nvgUpdateImage(vg, above, marks.data());
            }
            imageW = w;
            imageH = h;
            layout = out;
        });
    });
}

void ForestHeatmap::chooseDay(int o)
{
    chosen = o;
    if (!worker)
        return;
    std::weak_ptr<bool> token = alive;
    worker->post([this, token, o](ForestScript& forest) {
        anki::json day = anki::json::parse(forest.query("heatmapDay", anki::json({ { "o", o } }).dump()), nullptr, false);
        brls::sync([this, token, day, o] {
            if (auto t = token.lock(); !t || !*t || o != chosen)
                return;
            tip.clear();
            daySearch = day.is_object() ? day.value("search", "") : "";
            if (day.is_object())
                for (const auto& line : day["lines"])
                    tip.push_back(line.get<std::string>());
        });
    });
}

void ForestHeatmap::move(int dx, int dy)
{
    if (layout.is_null() || asking)
        return;
    int o = chosen + dx * 7 + dy;
    int from = layout.value("from", 0), to = layout.value("to", 0);
    if (o < from || o > to)
    {
        page += o < from ? -1 : 1;
        ask(page);
    }
    chooseDay(o);
}

void ForestHeatmap::tap(float x, float y)
{
    if (layout.is_null())
        return;
    float px = (x - ox) / k, py = (y - oy) / k;
    int tile = layout.value("tile", 9);
    for (const auto& c : layout["cells"])
    {
        float cx = c[1].get<float>(), cy = c[2].get<float>();
        if (px >= cx && px < cx + tile && py >= cy && py < cy + tile)
        {
            chooseDay(c[0].get<int>());
            return;
        }
    }
}

std::string ForestHeatmap::search() const { return daySearch; }

void ForestHeatmap::draw(NVGcontext* vg, float x, float y, float width, float height)
{
    if (layout.is_null() || !image)
    {
        label(vg, x + width / 2, y + 40, "…", 24, dim(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        return;
    }
    float W = layout.value("w", 1.0f), H = layout.value("h", 1.0f);
    // whole pixels: the add-on's tiles are pixel art
    k  = std::max(1.0f, std::floor(std::min((width - DAYS_ROOM) / W, (height * SHARE - MONTHS_ROOM) / H)));
    ox = x + DAYS_ROOM + std::floor((width - DAYS_ROOM - W * k) / 2);
    oy = y + MONTHS_ROOM;
    nvgBeginPath(vg);
    nvgRect(vg, ox, oy, W * k, H * k);
    nvgFillPaint(vg, nvgImagePattern(vg, ox, oy, W * k, H * k, 0, image, 1.0f));
    nvgFill(vg);
    if (above)
    {
        nvgBeginPath(vg);
        nvgRect(vg, ox, oy, W * k, H * k);
        nvgFillPaint(vg, nvgImagePattern(vg, ox, oy, W * k, H * k, 0, above, 1.0f));
        nvgFill(vg);
    }
    float tile = layout.value("tile", 9.0f);
    for (const auto& m : layout["months"])
        label(vg, ox + m["x"].get<float>() * k, oy - 6, m["label"].get<std::string>(), 16, dim(),
              NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
    for (const auto& d : layout["weekdays"])
        label(vg, ox - 10, oy + (d["y"].get<float>() + tile / 2) * k, d["label"].get<std::string>(), 15, dim(),
              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    for (const auto& b : layout["brackets"])
    {
        float cx = ox + (b["x0"].get<float>() + b["x1"].get<float>()) / 2 * k;
        label(vg, cx, oy + H * k + 4, b["text"].get<std::string>(), 15, b.value("live", false) ? text() : dim(),
              NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    }
    // the chosen day, ringed
    for (const auto& c : layout["cells"])
    {
        if (c[0].get<int>() != chosen)
            continue;
        float cx = ox + c[1].get<float>() * k, cy = oy + c[2].get<float>() * k;
        nvgBeginPath(vg);
        nvgRect(vg, cx - k - 1, cy - k - 1, (tile + 2) * k + 2, (tile + 2) * k + 2);
        nvgStrokeColor(vg, nvgRGB(66, 165, 245));
        nvgStrokeWidth(vg, 3);
        nvgStroke(vg);
        break;
    }
    // its tooltip, under the heatmap
    float ty = oy + H * k + 34;
    for (size_t i = 0; i < tip.size(); i++)
    {
        label(vg, x + width / 2, ty, tip[i], i ? 18 : 21, i ? dim() : text(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        ty += i ? 26 : 30;
    }
}
