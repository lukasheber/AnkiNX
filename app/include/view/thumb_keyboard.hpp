#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

// A touch keyboard for typing while holding the console in both hands
// (Settings > Study > Keyboard for typed answers): QWERTY split in two
// halves at the bottom corners, one under each thumb, or the two halves
// stacked at one edge for one thumb. Latin letters, digits and punctuation;
// holding a letter offers its accented forms (é, ñ, ü...); no prediction.
//
// With `kana`, letters are typed as romaji and become hiragana as they are
// typed (util/kana: "ka" か, "nn" ん; capitals give katakana).
//
// It opens over the current screen, which stays visible (the card is still
// readable between the halves). Each finger is followed on its own, so the
// thumbs can overlap: a key types when its finger lifts, or as soon as the
// other thumb touches a key, which keeps the letters in the order pressed.
// Buttons work too: the D-pad and A pick keys, B deletes, Y types a space,
// + confirms and - cancels.
class ThumbKeyboard : public brls::View
{
  public:
    enum class Layout
    {
        Split,
        RightThumb,
        LeftThumb,
    };
    using Callback = std::function<void(const std::string&)>;

    // onDone runs with the text after Enter or +, not when cancelled (as the
    // system keyboard). maxLength counts characters.
    static void open(Layout layout, const std::string& prompt, const std::string& initial, int maxLength,
                     Callback onDone, bool kana = false);

    ThumbKeyboard(Layout layout, std::string prompt, std::string initial, int maxLength, Callback onDone,
                  bool kana = false);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;
    bool isTranslucent() override { return true; }
    brls::View* getDefaultFocus() override { return this; }
    void onLayout() override { placed.clear(); }

    // One finger's touch (or the mouse, in the desktop preview).
    void touch(int finger, brls::TouchPhase phase, brls::Point position);

    enum class Kind
    {
        Char,
        Shift,
        Layer,
        Space,
        Backspace,
        Enter,
    };
    struct Key
    {
        Kind kind;
        std::string lower, upper;
        int span = 1; // in key widths
    };

  private:
    struct Placed
    {
        const Key* key;
        float x, y, w, h;
    };
    struct Finger
    {
        int id;
        int key = -1;       // index in placed, or -1 off the keys
        bool spent = false; // its key has acted (on touch, or when the other thumb came)
        brls::Time down = 0; // when it came onto its key, for holding
        brls::Time repeat = 0; // next delete while holding Backspace
        brls::Point position;
        // holding a letter: its accented forms, shown above it
        std::vector<std::string> variants;
        int variant = -1; // the one under the finger, -1 for none
        float popupX = 0, popupY = 0;
    };
    enum class Shift
    {
        Off,
        Once,
        Lock,
    };

    void place();
    int keyAt(brls::Point p) const;
    // What a key does: those that act on touch (Shift, Layer, Backspace) and
    // the others (letters, Space, Enter) as the finger lifts.
    void press(int index);
    void commit(Finger& f);
    void insert(const std::string& s);
    void deleteLast();
    void toggleLayer();
    void moveCursor(int dx, int dy);
    void openVariants(Finger& f);
    void pickVariant(Finger& f);
    void submit();
    void cancel();
    // Close once no finger is on the screen: borealis keeps a pointer to the
    // view each finger started on until it lifts.
    void closeWhenReleased(bool confirmed);
    void finishClose();

    void drawKey(NVGcontext* vg, const Placed& p, bool pressed, bool cursor);
    void drawField(NVGcontext* vg);

    Layout layout;
    std::string prompt, text;
    int maxLength;
    // Romaji typed as hiragana (see above).
    bool kana = false;
    Callback onDone;

    bool symbols = false;
    Shift shift  = Shift::Off;
    std::vector<Placed> placed;
    // backdrops behind the key blocks and the text field
    std::vector<brls::Rect> panels;
    brls::Rect fieldRect, hintRect, promptRect;
    std::vector<Finger> fingers;
    int cursor         = -1; // key picked with the D-pad (-1: hidden)
    bool closing       = false;
    bool closePending  = false;
    bool closeConfirms = false;
    brls::Time lastDelete = 0;
};
