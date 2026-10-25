#pragma once

#include <borealis.hpp>
#include <functional>
#include <memory>
#include <string>

// Memory Forest (app/forest): a forest thread builds the scene and draws its
// frames with ForestScript; this view puts the latest frame on screen,
// scaled up without smoothing to cover the view. Behind the deck list it is
// dimmed towards the theme's background so the list stays readable.
//
// Frames are only drawn while the view is: with another screen on top the
// forest thread waits. Build and frame times go to the log.
class ForestView : public brls::View
{
  public:
    // `dim`: behind other views (the deck list), not on its own.
    explicit ForestView(bool dim);
    ~ForestView() override;

    // The forest to draw: its data as the scenes take it (forest::grow), or
    // "" for none: the view draws nothing and the thread stops. The same data
    // again changes nothing.
    void show(const std::string& data);
    // Draw the scene `other` is drawing (already built, not built again);
    // `other` keeps it: show() and going away here only let go of it.
    void share(const ForestView& other);
    // Ask the scene (ForestScript::query) once it is built; `done` gets the
    // JSON answer on the UI thread, unless this view is gone by then.
    void query(const std::string& name, const std::string& args, std::function<void(const std::string&)> done);
    // Called once the scene is on screen.
    std::function<void()> onReady;

    // The canvas the scene is drawn on, and where it is on screen (it covers
    // the view, centred, cut at the edges if their shapes differ).
    static constexpr int SCENE_W = 320, SCENE_H = 180;
    void sceneToScreen(float sx, float sy, float& x, float& y) const;
    void screenToScene(float x, float y, float& sx, float& sy) const;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    struct Worker;

  private:
    bool dim;
    bool owner = true; // (not when shared from another view)
    std::shared_ptr<Worker> worker;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::string data;
    int image = 0, imageW = 0, imageH = 0;
    bool readySent = false;
    // the scene on screen: its top left and scale
    float sceneX = 0, sceneY = 0, sceneK = 1;
};
