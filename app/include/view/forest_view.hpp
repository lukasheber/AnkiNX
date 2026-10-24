#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>

// Memory Forest behind the deck list (app/forest): a forest thread builds the
// scene and draws its frames with ForestScript; this view puts the latest
// frame on screen, scaled up without smoothing over the whole view, and
// dimmed towards the theme's background so the list stays readable.
//
// Frames are only drawn while the view is: with another screen on top the
// forest thread waits. Build and frame times go to the log.
class ForestView : public brls::View
{
  public:
    ForestView();
    ~ForestView() override;

    // A sample forest by its preset key (resources/forest/samples), or "" for
    // none: the view draws nothing and the thread stops.
    void show(const std::string& scenery);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    struct Worker;

  private:
    std::shared_ptr<Worker> worker;
    std::string scenery;
    int image = 0, imageW = 0, imageH = 0;
};
