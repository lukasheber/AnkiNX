#pragma once

#include <borealis.hpp>
#include <functional>
#include <litehtml.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "script/card_script.hpp"

class NvgContainer;

// One Image Occlusion mask, as Anki's reviewer draws it
// (ts/routes/image-occlusion/review.ts). Positions and sizes are fractions of
// the image; drawn over the <img> whose src ends in "#ankinx-io".
struct Occlusion
{
    enum class State
    {
        Active,    // the cloze being asked (question side)
        Inactive,  // other clozes: drawn only with "occlude inactive"
        Highlight, // the cloze being asked, answer side: outline only
    };
    enum class Kind
    {
        Rect,
        Ellipse,
        Polygon,
        Text,
    };
    State state = State::Active;
    Kind kind   = Kind::Rect;
    bool occludeInactive = false;
    float left = 0, top = 0, width = 0, height = 0, rx = 0, ry = 0;
    float angle = 0; // degrees, about (left, top)
    std::vector<std::pair<float, float>> points;
    std::string text;
    float scale    = 1;
    float fontSize = 0; // fraction of the image height; 0: Anki's 40px
    std::string fill;   // "#rrggbb", or empty for the default
};

// Renders a card's HTML/CSS (litehtml layout, NanoVG drawing).
// Card templates' JavaScript runs in a CardScript page (the reviewer's);
// this view draws that page's body and lays it out for the page's layout
// questions (measure()).
class CardView : public brls::View
{
  public:
    // Image Occlusion masks for the current document (empty for other cards).
    void setOcclusions(std::vector<Occlusion> shapes);
    // "Toggle masks" (answer side): hide all but text labels, or show again.
    void setMasksHidden(bool hidden);
    bool getMasksHidden() const { return masksHidden; }

    CardView();
    ~CardView() override;

    static brls::View* create();

    // Replace the displayed document. Layout happens on the next frame.
    // keepScroll: stay at the current position (e.g. after revealing a hint).
    void setHtml(const std::string& html, bool keepScroll = false);
    // Scroll so the answer separator (<hr id="answer">) is near the top.
    void scrollToAnswerAfterLayout();
    // Folder that relative <img src> paths resolve against.
    void setMediaDir(const std::string& dir);
    // Called for activated links, e.g. "playsound:q:0".
    void setOnLink(std::function<void(const std::string&)> callback);
    // Called when the card is tapped outside any link; `across` is where,
    // from 0 at the card's left edge to 1 at its right.
    void setOnTap(std::function<void(float across)> callback);
    // Called first for a tap on an element tagged data-ankinx-node (card
    // scripts), with the point in CSS pixels on the page; returning true
    // means the page handled it (links and onTap are left alone).
    void setOnElementTap(std::function<bool(int serial, float x, float y)> callback);
    // Lay out a document as draw() would, without showing it: boxes and
    // computed styles of its elements tagged data-ankinx-node.
    CardScript::Layout measure(const std::string& html);
    // The page as card scripts see it, in CSS pixels.
    float cssWidth();
    float cssHeight();
    float cssScroll() const { return scroll / scale(); }
    float pixelRatio() const { return scale(); }
    void scrollToCss(float y);
    // Swipes (touch): horizontal ones always, vertical ones on cards that
    // fit without scrolling.
    enum class Swipe
    {
        Left,
        Right,
        Up,
        Down,
    };
    void setOnSwipe(std::function<void(Swipe)> callback) { onSwipe = std::move(callback); }

    // Controller access to what a tap could activate (links, buttons, form
    // controls, labels, elements with script click handlers): Left/Right, and
    // Up/Down once one is highlighted, move a highlight between them.
    bool hasFocusedElement() const { return focusIndex >= 0; }
    // Tap the highlighted element. Returns false if there is none.
    bool activateFocused();
    void clearElementFocus();

    // Whiteboard (touch): drawing over the card. While on, dragging draws
    // instead of scrolling and taps do nothing; off hides the drawing.
    void setWhiteboard(bool on) { whiteboard = on; }
    bool getWhiteboard() const { return whiteboard; }
    void clearWhiteboard() { strokes.clear(); }

    void scrollBy(float dy);
    void setZoom(float zoom);
    float getZoom() const { return zoom; }
    // The screen's own factor, applied on top of the zoom (larger card text
    // on a TV); not saved with the deck's zoom.
    void setScreenScale(float value);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    friend class NvgContainer;

    void layout(float width);
    float maxScroll() const;
    void handleTap(brls::Point point);
    void tapAt(float docX, float docY);
    void adjustTouch(float& x, float& y) const;
    std::vector<litehtml::element::ptr> focusables() const;
    // Move the highlight by `delta` elements; false at either end.
    bool moveFocus(int delta);
    void drawFocus(NVGcontext* vg);

    std::unique_ptr<NvgContainer> container;
    litehtml::document::ptr document;
    std::string html;
    bool needsLayout         = false;
    bool scrollToAnswer      = false;
    bool linkActivated       = false;
    float layoutWidth        = -1;
    float scroll             = 0;
    float zoom               = 1.3f;
    float screenScale        = 1.0f;
    float scale() const { return zoom * screenScale; }
    float viewHeight         = 0;
    std::function<void(const std::string&)> onLink;
    std::function<void(float across)> onTap;
    std::function<bool(int, float, float)> onElementTap;
    std::function<void(Swipe)> onSwipe;
    std::vector<Occlusion> occlusions;
    bool masksHidden = false;
    // The highlighted element: its index in focusables(), and its script
    // serial (0 if none) to find it again after a script changes the card.
    int focusIndex  = -1;
    int focusSerial = 0;
    std::vector<litehtml::element::ptr> focusList; // focusables() after layout
    bool whiteboard = false;
    // Strokes in view coordinates (the drawing stays put when the card scrolls).
    std::vector<std::vector<brls::Point>> strokes;
    bool strokeOpen = false;
    void drawWhiteboard(NVGcontext* vg, float x, float y);
};
