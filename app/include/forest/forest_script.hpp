#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Memory Forest's scenes (app/forest), run in QuickJS: resources/forest/
// forest.js holds the add-on's scene scripts behind a small browser layer
// whose canvases are PixelCanvas. build() lays a forest out and draws what
// stays still; frame() draws one moment of it into pixels().
//
// Not thread-safe: make it, and use it, on one thread (the app's forest
// thread). Errors are kept in error() and never throw.
class ForestScript
{
  public:
    // `bundle`: resources/forest/forest.js.
    explicit ForestScript(const std::string& bundle);
    ~ForestScript();

    // The forest's data, as the add-on's page gets it (JSON), drawn on a
    // canvas width x height pixels.
    bool build(const std::string& data, int width, int height);
    // The forest at `seconds` (any steady clock).
    bool frame(double seconds);
    // Something in the scene moves fast (rain, a strike): worth ~15 frames
    // a second rather than 10.
    bool busy();
    // One of the runner's questions (app/forest/src/runner.js QUERIES): pick
    // {x, y}, trees, mark {ago}. JSON in and out; "null" if it failed.
    std::string query(const std::string& name, const std::string& args);

    // The last frame: premultiplied RGBA, width() x height().
    const uint8_t* pixels() const;
    int width() const;
    int height() const;
    // Bytes QuickJS has allocated (the canvases' pixels are not counted).
    size_t memoryUsed() const;
    const std::string& error() const;
    // Stop what is running now and anything after (from any thread): for
    // closing, without waiting out a build.
    void cancel();

    static std::function<void(int level, const std::string&)> log;

    struct Native;

  private:
    std::unique_ptr<Native> native;
};
