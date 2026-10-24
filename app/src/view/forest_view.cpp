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

#ifdef __SWITCH__
#include <switch.h>
#else
#include <pthread.h>
#endif

using Clock = std::chrono::steady_clock;

namespace
{

// The scene's size: Memory Forest draws on a canvas 320 pixels across
// (web/core.js BASE_W) and scales it up, here 4 times to the 1280 x 720 screen.
constexpr int SCENE_W = 320, SCENE_H = 180;
// ~15 frames a second while something moves fast, else 10 (web/core.js)
constexpr auto BUSY_FRAME = std::chrono::milliseconds(66), CALM_FRAME = std::chrono::milliseconds(100);
// Not drawn on screen for this long: another screen is on top, so wait.
constexpr int64_t UNSEEN_MS = 500;
// How often the frame times go to the log, and busy() is asked again.
constexpr auto REPORT_EVERY = std::chrono::seconds(10), BUSY_EVERY = std::chrono::seconds(1);
// The theme's background over the forest, so the deck list stays readable.
constexpr float DIM = 0.55f;
// Frames in a row that throw before the forest gives up.
constexpr int MAX_FAILURES = 5;
// QuickJS needs room for the scenes' recursion (forest_script.cpp STACK_LIMIT).
constexpr size_t STACK_SIZE = 1u << 20;

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
    std::string scenery;
    std::atomic<bool> stop{ false };
    std::atomic<int64_t> seen{ 0 }; // when the view last drew, nowMs()
    std::mutex lock;
    ForestScript* script = nullptr; // (for cancel(), under lock)
    std::vector<uint8_t> frame;     // the latest frame, under lock
    int width = 0, height = 0;
    bool fresh = false;
#ifdef __SWITCH__
    Thread thread;
#else
    pthread_t thread;
#endif
    bool started = false;

    void run();
    void start();
    void join();
};

void ForestView::Worker::run()
{
    std::string bundle, data;
    if (!readFile(BRLS_ASSET("forest/forest.js"), bundle) ||
        !readFile(std::string(BRLS_ASSET("forest/samples/")) + scenery + ".json", data))
    {
        brls::Logger::error("forest: no bundle or sample for {}", scenery);
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
    {
        std::lock_guard<std::mutex> l(lock);
        script = forest;
    }
    double load = msSince(t0);
    t0          = Clock::now();
    bool ok     = !stop && forest->build(data, SCENE_W, SCENE_H);
    brls::Logger::info("forest: {} loaded in {:.0f} ms, built in {:.0f} ms{}", scenery, load, msSince(t0),
                       ok ? "" : " (failed)");

    auto started = Clock::now(), reported = started, busyAsked = started;
    bool busy    = ok && forest->busy();
    int frames = 0, failures = 0;
    double total = 0, worst = 0;
    while (ok && !stop)
    {
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

#ifdef __SWITCH__
void ForestView::Worker::start()
{
    // On core 2: the UI's is 0, and newlib threads start on the process' default core.
    auto entry = [](void* arg) { ((Worker*)arg)->run(); };
    if (R_SUCCEEDED(threadCreate(&thread, entry, this, nullptr, STACK_SIZE, 0x2C, 2)) &&
        R_SUCCEEDED(threadStart(&thread)))
        started = true;
    else
        brls::Logger::error("forest: no thread");
}

void ForestView::Worker::join()
{
    if (!started)
        return;
    threadWaitForExit(&thread);
    threadClose(&thread);
    started = false;
}
#else
void ForestView::Worker::start()
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_SIZE);
    auto entry = [](void* arg) -> void* {
        ((Worker*)arg)->run();
        return nullptr;
    };
    started = pthread_create(&thread, &attr, entry, this) == 0;
    pthread_attr_destroy(&attr);
    if (!started)
        brls::Logger::error("forest: no thread");
}

void ForestView::Worker::join()
{
    if (!started)
        return;
    pthread_join(thread, nullptr);
    started = false;
}
#endif

ForestView::ForestView()
{
    this->setFocusable(false);
    this->setPositionType(brls::PositionType::ABSOLUTE);
    this->setPositionTop(0);
    this->setPositionLeft(0);
    this->setWidthPercentage(100);
    this->setHeightPercentage(100);
}

ForestView::~ForestView()
{
    show("");
    if (image)
        nvgDeleteImage(brls::Application::getNVGContext(), image);
}

void ForestView::show(const std::string& key)
{
    if (key == scenery)
        return;
    scenery = key;
    if (worker)
    {
        worker->stop = true;
        {
            std::lock_guard<std::mutex> l(worker->lock);
            if (worker->script)
                worker->script->cancel();
        }
        worker->join();
        worker.reset();
    }
    if (key.empty())
        return;
    worker          = std::make_shared<Worker>();
    worker->scenery = key;
    worker->seen    = nowMs();
    worker->start();
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
    // Scaled to cover the view, centred: whole pixels where the view allows.
    float k  = std::max(width / imageW, height / imageH);
    float dw = imageW * k, dh = imageH * k, dx = x + (width - dw) / 2, dy = y + (height - dh) / 2;
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillPaint(vg, nvgImagePattern(vg, dx, dy, dw, dh, 0, image, 1.0f));
    nvgFill(vg);
    NVGcolor dim = ctx->theme["brls/background"];
    dim.a        = DIM;
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, dim);
    nvgFill(vg);
}
