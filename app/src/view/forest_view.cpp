#include "view/forest_view.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#include "forest/forest_script.hpp"
#include "forest/forest_thread.hpp"

using Clock = std::chrono::steady_clock;

namespace
{

// ~15 frames a second while something moves fast, else 10 (web/core.js)
constexpr auto BUSY_FRAME = std::chrono::milliseconds(66), CALM_FRAME = std::chrono::milliseconds(100);
// Not drawn on screen for this long: another screen is on top, so wait.
constexpr int64_t UNSEEN_MS = 500;
// How often the frame times go to the log, and busy() is asked again.
constexpr auto REPORT_EVERY = std::chrono::seconds(10), BUSY_EVERY = std::chrono::seconds(1);
// The theme's background over the forest behind the deck list.
constexpr float DIM = 0.4f;
// Frames in a row that throw before the forest gives up.
constexpr int MAX_FAILURES = 5;

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

bool readFile(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

} // namespace

struct ForestView::Worker
{
    std::string data;
    std::atomic<bool> stop{ false };
    std::atomic<int64_t> seen{ 0 }; // when the view last drew, nowMs()
    std::mutex lock;
    ForestScript* script = nullptr; // (for cancel(), under lock)
    std::vector<uint8_t> frame;     // the latest frame, under lock
    int width = 0, height = 0;
    bool fresh = false;
    struct Request
    {
        std::string name, args;
        std::function<void(const std::string&)> done;
    };
    std::vector<Request> requests; // under lock
    // the view's onEvent, called on the UI thread
    std::function<void(const std::string&, const std::string&)> onEvent;
    ForestThread thread;

    void run();
    void answer(ForestScript& forest);
};

void ForestView::Worker::answer(ForestScript& forest)
{
    std::vector<Request> now;
    {
        std::lock_guard<std::mutex> l(lock);
        now.swap(requests);
    }
    for (auto& r : now)
    {
        std::string out = forest.query(r.name, r.args);
        brls::sync([done = std::move(r.done), out] { done(out); });
    }
}

void ForestView::Worker::run()
{
    std::string bundle;
    if (!readFile(BRLS_ASSET("forest/forest.js"), bundle))
    {
        brls::Logger::error("forest: no forest.js");
        return;
    }
    ForestScript::log = [](int level, const std::string& msg) {
        if (level >= 2)
            brls::Logger::error("forest: {}", msg);
        else
            brls::Logger::info("forest: {}", msg);
    };
    auto t0 = Clock::now();
    auto* forest = new ForestScript(bundle);
    forest->onEvent = [this](const std::string& name, const std::string& value) {
        if (onEvent)
            brls::sync([cb = onEvent, name, value] { cb(name, value); });
    };
    {
        std::lock_guard<std::mutex> l(lock);
        script = forest;
    }
    double load = msSince(t0);
    t0          = Clock::now();
    bool ok     = !stop && forest->build(data, SCENE_W, SCENE_H);
    brls::Logger::info("forest: loaded in {:.0f} ms, built in {:.0f} ms{}", load, msSince(t0), ok ? "" : " (failed)");

    auto started = Clock::now(), reported = started, busyAsked = started;
    bool busy    = ok && forest->busy();
    int frames = 0, failures = 0;
    double total = 0, worst = 0;
    while (ok && !stop)
    {
        answer(*forest);
        if (nowMs() - seen > UNSEEN_MS)
        {
            std::this_thread::sleep_for(CALM_FRAME);
            continue;
        }
        auto f0 = Clock::now();
        // (one frame that throws must not stop the forest for good: web/core.js)
        if (!forest->frame(std::chrono::duration<double>(f0 - started).count()))
        {
            ok = ++failures < MAX_FAILURES;
            std::this_thread::sleep_for(CALM_FRAME);
            continue;
        }
        failures = 0;
        {
            std::lock_guard<std::mutex> l(lock);
            width  = forest->width();
            height = forest->height();
            frame.assign(forest->pixels(), forest->pixels() + (size_t)width * height * 4);
            fresh = true;
        }
        double one = msSince(f0);
        frames++;
        total += one;
        worst = std::max(worst, one);
        if (Clock::now() - busyAsked > BUSY_EVERY)
        {
            busy      = forest->busy();
            busyAsked = Clock::now();
        }
        if (Clock::now() - reported > REPORT_EVERY)
        {
            brls::Logger::info("forest: {} frames, {:.1f} ms average, {:.1f} ms longest, {}, JS {:.1f} MB", frames,
                               total / frames, worst, busy ? "busy" : "calm", forest->memoryUsed() / 1048576.0);
            frames = 0, total = 0, worst = 0;
            reported = Clock::now();
        }
        auto wait = (busy ? BUSY_FRAME : CALM_FRAME) - (Clock::now() - f0);
        if (wait > Clock::duration::zero())
            std::this_thread::sleep_for(wait);
    }
    if (!ok && !stop)
        brls::Logger::error("forest: stopped: {}", forest->error());
    {
        std::lock_guard<std::mutex> l(lock);
        script = nullptr;
    }
    delete forest;
}


ForestView::ForestView(bool dim)
    : dim(dim)
{
    // behind the rest of its parent, covering all of it
    this->setFocusable(false);
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(0);
    this->setPositionLeft(0);
    this->setWidthPercentage(100);
    this->setHeightPercentage(100);
}

ForestView::~ForestView()
{
    *alive = false;
    show("");
    if (image)
        nvgDeleteImage(brls::Application::getNVGContext(), image);
}

void ForestView::share(const ForestView& other)
{
    show("");
    worker    = other.worker;
    data      = other.data;
    owner     = false;
    readySent = false;
}

void ForestView::show(const std::string& next)
{
    if (next == data)
        return;
    data = next;
    if (worker && !owner)
    {
        worker.reset();
        owner = true;
    }
    if (worker)
    {
        worker->stop = true;
        {
            std::lock_guard<std::mutex> l(worker->lock);
            if (worker->script)
                worker->script->cancel();
        }
        worker->thread.join();
        worker.reset();
    }
    readySent = false;
    if (next.empty())
        return;
    worker       = std::make_shared<Worker>();
    worker->data = next;
    if (onEvent)
        worker->onEvent = [token = std::weak_ptr<bool>(alive), cb = onEvent](const std::string& name, const std::string& value) {
            if (auto t = token.lock(); t && *t)
                cb(name, value);
        };
    worker->seen = nowMs();
    Worker* w = worker.get();
    if (!worker->thread.start([w] { w->run(); }))
        brls::Logger::error("forest: no thread");
}

void ForestView::query(const std::string& name, const std::string& args, std::function<void(const std::string&)> done)
{
    if (!worker)
        return;
    std::weak_ptr<bool> token = alive;
    std::lock_guard<std::mutex> l(worker->lock);
    worker->requests.push_back({ name, args, [token, done = std::move(done)](const std::string& out) {
                                     if (auto t = token.lock(); t && *t)
                                         done(out);
                                 } });
}

void ForestView::sceneToScreen(float sx, float sy, float& x, float& y) const
{
    x = sceneX + sx * sceneK;
    y = sceneY + sy * sceneK;
}

void ForestView::screenToScene(float x, float y, float& sx, float& sy) const
{
    sx = (x - sceneX) / sceneK;
    sy = (y - sceneY) / sceneK;
}

void ForestView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                      brls::FrameContext* ctx)
{
    if (!worker)
        return;
    worker->seen = nowMs();
    {
        std::lock_guard<std::mutex> l(worker->lock);
        if (worker->fresh && !worker->frame.empty())
        {
            if (image && (imageW != worker->width || imageH != worker->height))
            {
                nvgDeleteImage(vg, image);
                image = 0;
            }
            if (!image)
            {
                image  = nvgCreateImageRGBA(vg, worker->width, worker->height,
                                            NVG_IMAGE_NEAREST | NVG_IMAGE_PREMULTIPLIED, worker->frame.data());
                imageW = worker->width;
                imageH = worker->height;
            }
            else
                nvgUpdateImage(vg, image, worker->frame.data());
            worker->fresh = false;
        }
    }
    if (!image)
        return;
    // Scaled to cover the view, centred.
    sceneK   = std::max(width / imageW, height / imageH);
    float dw = imageW * sceneK, dh = imageH * sceneK;
    sceneX   = x + (width - dw) / 2;
    sceneY   = y + (height - dh) / 2;
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillPaint(vg, nvgImagePattern(vg, sceneX, sceneY, dw, dh, 0, image, 1.0f));
    nvgFill(vg);
    if (dim)
    {
        NVGcolor shade = ctx->theme["brls/background"];
        shade.a        = DIM;
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, shade);
        nvgFill(vg);
    }
    if (!readySent)
    {
        readySent = true;
        if (onReady)
            brls::sync([cb = onReady, token = std::weak_ptr<bool>(alive)] {
                if (auto t = token.lock(); t && *t)
                    cb();
            });
    }
}
