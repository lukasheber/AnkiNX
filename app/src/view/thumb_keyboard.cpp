#include "view/thumb_keyboard.hpp"

#include "util/kana.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <map>

using namespace brls::literals;

namespace
{

using Key  = ThumbKeyboard::Key;
using Kind = ThumbKeyboard::Kind;
using Row  = std::vector<Key>;
using Rows = std::vector<Row>;

// Sizes, in the 1280x720 screen's points (about 9 per millimetre handheld,
// 8 on the OLED model). Narrow blocks: holding the console, the Joy-Cons
// keep the thumbs away from the screen, so they reach only near its edges.
// (60 before: one key column, 65 points, narrower now)
const float KEY_W     = 47; // a block of 5 keys: 255 points, about 27 mm
const float GAP       = 5;
const float MARGIN    = 6; // from the screen's edges
const float PAD       = 8; // panel around a block of keys
const float SPLIT_H   = 64; // key height, split
const float STACKED_H = 52; // key height, one thumb (7 rows)
const int COLUMNS     = 5;

const brls::Time HOLD_US          = 380 * 1000; // holding a letter shows its accents
const brls::Time REPEAT_DELAY_US  = 450 * 1000; // holding Backspace
const brls::Time REPEAT_EVERY_US  = 60 * 1000;
const brls::Time CANCEL_GUARD_US  = 400 * 1000; // B on empty text cancels, unless it was just deleting
const int MOUSE                   = 1000; // finger id of the desktop preview's mouse

Row letters(const char* s)
{
    Row row;
    for (; *s; s++)
        row.push_back({ Kind::Char, std::string(1, *s), std::string(1, (char)std::toupper((unsigned char)*s)) });
    return row;
}

Row chars(std::initializer_list<const char*> list)
{
    Row row;
    for (const char* c : list)
        row.push_back({ Kind::Char, c, c });
    return row;
}

Key special(Kind kind, int span = 1) { return { kind, "", "", span }; }

Row operator+(Row a, const Row& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// Split: a block of 5 keys by 4 rows under each thumb. Keys keep their place
// between letters and symbols (Backspace, Space, OK, the layer key).
const Rows& splitLeft(bool symbols)
{
    static const Rows LETTERS = {
        letters("qwert"),
        letters("asdfg"),
        letters("zxcvb"),
        { special(Kind::Shift), special(Kind::Layer), special(Kind::Space, 3) },
    };
    static const Rows SYMBOLS = {
        chars({ "1", "2", "3", "4", "5" }),
        chars({ "@", "#", "&", "*", "+" }),
        chars({ "(", ")", "/", ":", ";" }),
        chars({ "=" }) + Row{ special(Kind::Layer), special(Kind::Space, 3) },
    };
    return symbols ? SYMBOLS : LETTERS;
}

const Rows& splitRight(bool symbols)
{
    static const Rows LETTERS = {
        letters("yuiop"),
        letters("hjkl") + Row{ special(Kind::Backspace) },
        letters("nm") + chars({ ",", ".", "'" }),
        chars({ "-" }) + Row{ special(Kind::Space, 3), special(Kind::Enter) },
    };
    static const Rows SYMBOLS = {
        chars({ "6", "7", "8", "9", "0" }),
        chars({ "?", "!", "\"", "_" }) + Row{ special(Kind::Backspace) },
        chars({ "%", "$", ",", ".", "'" }),
        chars({ "-" }) + Row{ special(Kind::Space, 3), special(Kind::Enter) },
    };
    return symbols ? SYMBOLS : LETTERS;
}

// One thumb: the two halves stacked, 5 keys by 7 rows.
const Rows& stacked(bool symbols)
{
    static const Rows LETTERS = {
        letters("qwert"),
        letters("asdfg"),
        letters("zxcvb"),
        letters("yuiop"),
        letters("hjkl") + Row{ special(Kind::Backspace) },
        letters("nm") + chars({ ",", ".", "'" }),
        { special(Kind::Shift), special(Kind::Layer), special(Kind::Space, 2), special(Kind::Enter) },
    };
    static const Rows SYMBOLS = {
        chars({ "1", "2", "3", "4", "5" }),
        chars({ "6", "7", "8", "9", "0" }),
        chars({ "@", "#", "&", "*", "+" }),
        chars({ "(", ")", "/", ":", ";" }),
        chars({ "?", "!", "\"", "_" }) + Row{ special(Kind::Backspace) },
        chars({ "%", "$", ",", ".", "'" }),
        chars({ "-" }) + Row{ special(Kind::Layer), special(Kind::Space, 2), special(Kind::Enter) },
    };
    return symbols ? SYMBOLS : LETTERS;
}

// What holding a key offers: accented letters of the languages written in
// the Latin alphabet, and a few marks. Lower case, then upper case.
struct Variants
{
    std::vector<std::string> lower, upper;
};

const std::map<std::string, Variants>& variantTable()
{
    static const std::map<std::string, Variants> TABLE = {
        { "a", { { "à", "á", "â", "ä", "ã", "å", "æ" }, { "À", "Á", "Â", "Ä", "Ã", "Å", "Æ" } } },
        { "c", { { "ç", "č", "ć" }, { "Ç", "Č", "Ć" } } },
        { "e", { { "é", "è", "ê", "ë", "ě" }, { "É", "È", "Ê", "Ë", "Ě" } } },
        { "i", { { "í", "ì", "î", "ï" }, { "Í", "Ì", "Î", "Ï" } } },
        { "l", { { "ł" }, { "Ł" } } },
        { "n", { { "ñ", "ń" }, { "Ñ", "Ń" } } },
        { "o", { { "ó", "ò", "ô", "ö", "õ", "ø", "œ" }, { "Ó", "Ò", "Ô", "Ö", "Õ", "Ø", "Œ" } } },
        { "s", { { "ß", "š", "ś" }, { "ß", "Š", "Ś" } } },
        { "u", { { "ú", "ù", "û", "ü" }, { "Ú", "Ù", "Û", "Ü" } } },
        { "y", { { "ý", "ÿ" }, { "Ý", "Ÿ" } } },
        { "z", { { "ž", "ź", "ż" }, { "Ž", "Ź", "Ż" } } },
        { "?", { { "¿" }, { "¿" } } },
        { "!", { { "¡" }, { "¡" } } },
        { "'", { { "’", "\"" }, { "’", "\"" } } },
        { "\"", { { "«", "»", "“", "”" }, { "«", "»", "“", "”" } } },
        { "-", { { "–", "—", "_" }, { "–", "—", "_" } } },
        { "$", { { "€", "£", "¥" }, { "€", "£", "¥" } } },
        { "+", { { "=", "±", "×", "÷" }, { "=", "±", "×", "÷" } } },
        { ".", { { "…" }, { "…" } } },
        { "0", { { "°" }, { "°" } } },
    };
    return TABLE;
}

const std::vector<std::string>* variantsOf(const Key& key, bool upper)
{
    if (key.kind != Kind::Char)
        return nullptr;
    auto it = variantTable().find(key.lower);
    if (it == variantTable().end())
        return nullptr;
    return upper ? &it->second.upper : &it->second.lower;
}

size_t characters(const std::string& s)
{
    return (size_t)std::count_if(s.begin(), s.end(), [](char c) { return ((unsigned char)c & 0xC0) != 0x80; });
}

bool dark() { return brls::Application::getThemeVariant() == brls::ThemeVariant::DARK; }

NVGcolor theme(const char* name) { return brls::Application::getTheme()[name]; }

NVGcolor withAlpha(NVGcolor c, float a)
{
    c.a = a;
    return c;
}

void roundedRect(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor fill)
{
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgFillColor(vg, fill);
    nvgFill(vg);
}

void label(NVGcontext* vg, float x, float y, const std::string& s, float size, NVGcolor color, int align)
{
    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    nvgFontSize(vg, size);
    nvgFillColor(vg, color);
    nvgTextAlign(vg, align);
    nvgText(vg, x, y, s.c_str(), nullptr);
}

// Every finger, and the mouse in the desktop preview, as it comes: borealis'
// own recognizers follow one touch at a time.
class FingerRecognizer : public brls::GestureRecognizer
{
  public:
    explicit FingerRecognizer(ThumbKeyboard* keyboard)
        : keyboard(keyboard)
    {
        state = brls::GestureState::UNSURE;
    }

    brls::GestureState recognitionLoop(brls::TouchState touch, brls::MouseState mouse, brls::View*,
                                       brls::Sound*) override
    {
        if (touch.phase != brls::TouchPhase::NONE)
            keyboard->touch(touch.fingerId, touch.phase, touch.position);
        else if (mouse.leftButton != brls::TouchPhase::NONE)
            keyboard->touch(MOUSE, mouse.leftButton, mouse.position);
        // Never START or END: those would interrupt the other fingers.
        return brls::GestureState::UNSURE;
    }

  private:
    ThumbKeyboard* keyboard;
};

} // namespace

void ThumbKeyboard::open(Layout layout, const std::string& prompt, const std::string& initial, int maxLength,
                         Callback onDone, bool kana)
{
    auto* keyboard = new ThumbKeyboard(layout, prompt, initial, maxLength, std::move(onDone), kana);
    // No fade in: the first touches would be lost while it plays.
    brls::Application::pushActivity(new brls::Activity(keyboard), brls::TransitionAnimation::NONE);
}

ThumbKeyboard::ThumbKeyboard(Layout layout, std::string prompt, std::string initial, int maxLength, Callback onDone,
                             bool kana)
    : layout(layout)
    , prompt(std::move(prompt))
    , text(std::move(initial))
    , maxLength(maxLength > 0 ? maxLength : 500)
    , kana(kana)
    , onDone(std::move(onDone))
{
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->addGestureRecognizer(new FingerRecognizer(this));

    // B deletes; on empty text it cancels, but not while held from deleting.
    this->registerAction("", brls::BUTTON_B, [this](brls::View*) {
        if (!text.empty())
            deleteLast();
        else if (brls::getCPUTimeUsec() - lastDelete > CANCEL_GUARD_US)
            cancel();
        return true;
    }, true, true);
    this->registerAction("", brls::BUTTON_Y, [this](brls::View*) {
        insert(" ");
        return true;
    }, true, true);
    this->registerAction("", brls::BUTTON_X, [this](brls::View*) {
        shift = shift == Shift::Off ? Shift::Once : shift == Shift::Once ? Shift::Lock : Shift::Off;
        return true;
    }, true);
    this->registerAction("", brls::BUTTON_START, [this](brls::View*) {
        submit();
        return true;
    }, true);
    this->registerAction("", brls::BUTTON_BACK, [this](brls::View*) {
        cancel();
        return true;
    }, true);
    this->registerAction("", brls::BUTTON_A, [this](brls::View*) {
        if (placed.empty())
            place();
        if (cursor < 0)
        {
            // The first A shows the key cursor, on G (the left hand's home).
            cursor = 0;
            for (size_t i = 0; i < placed.size(); i++)
                if (placed[i].key->lower == "g" || placed[i].key->lower == "5")
                    cursor = (int)i;
            return true;
        }
        Kind kind = placed[cursor].key->kind;
        if (kind == Kind::Shift || kind == Kind::Layer || kind == Kind::Backspace)
            press(cursor);
        else
        {
            Finger f{ -1 };
            f.key = cursor;
            commit(f);
        }
        return true;
    }, true, true);
    auto arrow = [this](brls::ControllerButton button, int dx, int dy) {
        this->registerAction("", button, [this, dx, dy](brls::View*) {
            moveCursor(dx, dy);
            return true;
        }, true, true);
    };
    arrow(brls::BUTTON_NAV_LEFT, -1, 0);
    arrow(brls::BUTTON_NAV_RIGHT, 1, 0);
    arrow(brls::BUTTON_NAV_UP, 0, -1);
    arrow(brls::BUTTON_NAV_DOWN, 0, 1);
}

// Where the keys, the text field and their panels go on this screen.
void ThumbKeyboard::place()
{
    placed.clear();
    panels.clear();
    float W = this->getWidth(), H = this->getHeight();
    float ox = this->getX(), oy = this->getY();
    float blockW = COLUMNS * KEY_W + (COLUMNS - 1) * GAP;

    auto block = [&](const Rows& rows, float left, float keyH) {
        float blockH = rows.size() * keyH + (rows.size() - 1) * GAP;
        float top    = oy + H - MARGIN - PAD - blockH;
        for (size_t r = 0; r < rows.size(); r++)
        {
            float x = left;
            for (const Key& key : rows[r])
            {
                float w = key.span * KEY_W + (key.span - 1) * GAP;
                placed.push_back({ &key, x, top + r * (keyH + GAP), w, keyH });
                x += w + GAP;
            }
        }
        panels.push_back(brls::Rect(left - PAD, top - PAD, blockW + 2 * PAD, blockH + 2 * PAD));
        return top;
    };

    // The text field beside the keys: its prompt, the field, the button hints.
    float fieldLeft, fieldRight, fieldTop;
    const float promptH = 26, fieldH = 60, hintH = 24, spacing = 8;
    const float areaH   = promptH + spacing + fieldH + spacing + hintH;
    if (layout == Layout::Split)
    {
        float left = ox + MARGIN + PAD;
        float top  = block(splitLeft(symbols), left, SPLIT_H);
        block(splitRight(symbols), ox + W - MARGIN - PAD - blockW, SPLIT_H);
        fieldLeft  = left + blockW + PAD + 2 * MARGIN + 12;
        fieldRight = ox + W - (fieldLeft - ox);
        fieldTop   = top;
    }
    else
    {
        float left = layout == Layout::RightThumb ? ox + W - MARGIN - PAD - blockW : ox + MARGIN + PAD;
        block(stacked(symbols), left, STACKED_H);
        if (layout == Layout::RightThumb)
        {
            fieldLeft  = ox + MARGIN + 12;
            fieldRight = left - PAD - 2 * MARGIN - 12;
        }
        else
        {
            fieldLeft  = left + blockW + PAD + 2 * MARGIN + 12;
            fieldRight = ox + W - MARGIN - 12;
        }
        fieldTop = oy + H - MARGIN - 12 - areaH;
    }
    float fieldW = fieldRight - fieldLeft;
    promptRect   = brls::Rect(fieldLeft, fieldTop, fieldW, promptH);
    fieldRect    = brls::Rect(fieldLeft, fieldTop + promptH + spacing, fieldW, fieldH);
    hintRect     = brls::Rect(fieldLeft, fieldRect.getMaxY() + spacing, fieldW, hintH);
    // down to the bottom edge, over the reviewer's buttons (which a tap
    // would not reach while the keyboard is open)
    panels.push_back(brls::Rect(fieldLeft - 12, fieldTop - 12, fieldW + 24, oy + H - MARGIN - (fieldTop - 12)));
}

// The key under a point; the gaps between keys go to the nearest one.
int ThumbKeyboard::keyAt(brls::Point p) const
{
    int best       = -1;
    float bestDist = GAP / 2 + 0.5f; // at most half a gap outside a key
    for (size_t i = 0; i < placed.size(); i++)
    {
        const Placed& k = placed[i];
        float dx        = std::max({ k.x - p.x, 0.0f, p.x - (k.x + k.w) });
        float dy        = std::max({ k.y - p.y, 0.0f, p.y - (k.y + k.h) });
        float d         = std::max(dx, dy);
        if (d < bestDist)
        {
            best     = (int)i;
            bestDist = d;
        }
    }
    return best;
}

void ThumbKeyboard::press(int index)
{
    if (closing || index < 0 || index >= (int)placed.size())
        return;
    switch (placed[index].key->kind)
    {
        case Kind::Shift:
            shift = shift == Shift::Off ? Shift::Once : shift == Shift::Once ? Shift::Lock : Shift::Off;
            break;
        case Kind::Layer: toggleLayer(); break;
        case Kind::Backspace: deleteLast(); break;
        default: break;
    }
}

void ThumbKeyboard::commit(Finger& f)
{
    if (f.spent)
        return;
    f.spent = true;
    if (closing || f.key < 0 || f.key >= (int)placed.size())
        return;
    const Key& key = *placed[f.key].key;
    switch (key.kind)
    {
        case Kind::Char:
            insert(shift != Shift::Off ? key.upper : key.lower);
            if (shift == Shift::Once)
                shift = Shift::Off;
            break;
        case Kind::Space: insert(" "); break;
        case Kind::Enter: submit(); break;
        default: break;
    }
}

void ThumbKeyboard::insert(const std::string& s)
{
    if (closing || characters(text) + characters(s) > (size_t)maxLength)
        return;
    if (!kana)
    {
        text += s;
        return;
    }
    // Hiragana: a letter converts the romaji typed so far as far as it can;
    // anything else (space, digits, punctuation) finishes it first.
    bool romaji = s.size() == 1 && (std::isalpha((unsigned char)s[0]) || s[0] == '-' || s[0] == '\'');
    if (romaji)
        text = kana::convertTail(text + s, false);
    else
        text = kana::convertTail(text, true) + s;
}

void ThumbKeyboard::deleteLast()
{
    if (closing || text.empty())
        return;
    // one character: its continuation bytes, then its first byte
    while (!text.empty() && ((unsigned char)text.back() & 0xC0) == 0x80)
        text.pop_back();
    if (!text.empty())
        text.pop_back();
    lastDelete = brls::getCPUTimeUsec();
}

void ThumbKeyboard::toggleLayer()
{
    symbols = !symbols;
    place();
    for (Finger& f : fingers)
        if (f.variants.empty())
            f.key = keyAt(f.position);
    if (cursor >= (int)placed.size())
        cursor = (int)placed.size() - 1;
}

// The nearest key that way, preferring those in line with this one (the gap
// between the halves is crossed like any other).
void ThumbKeyboard::moveCursor(int dx, int dy)
{
    if (placed.empty())
        place();
    if (cursor < 0)
    {
        cursor = 0;
        return;
    }
    const Placed& from = placed[cursor];
    float cx = from.x + from.w / 2, cy = from.y + from.h / 2;
    int best        = -1;
    float bestScore = 1e9f;
    for (size_t i = 0; i < placed.size(); i++)
    {
        const Placed& k = placed[i];
        float ax = k.x + k.w / 2 - cx, ay = k.y + k.h / 2 - cy;
        float along = ax * dx + ay * dy;
        if (along < 1)
            continue;
        float across = std::fabs(dx ? ay : ax);
        float score  = along + 2.5f * across;
        if (score < bestScore)
        {
            best      = (int)i;
            bestScore = score;
        }
    }
    if (best >= 0)
        cursor = best;
}

void ThumbKeyboard::openVariants(Finger& f)
{
    const auto* list = variantsOf(*placed[f.key].key, shift != Shift::Off);
    if (!list || list->empty())
        return;
    f.variants = *list;
    f.variant  = 0;
    // A row above the key, its first cell over the key, going towards the
    // middle of the screen; kept on screen.
    const Placed& k = placed[f.key];
    float cellW     = KEY_W;
    int n           = (int)f.variants.size();
    bool leftwards  = k.x + k.w / 2 > this->getX() + this->getWidth() / 2;
    float firstX    = k.x + k.w / 2 - cellW / 2;
    float rowLeft   = leftwards ? firstX - (n - 1) * cellW : firstX;
    float minX      = this->getX() + MARGIN;
    float maxX      = this->getX() + this->getWidth() - MARGIN - n * cellW;
    rowLeft         = std::clamp(rowLeft, minX, std::max(minX, maxX));
    f.popupX        = rowLeft;
    f.popupY        = std::max(this->getY() + MARGIN, k.y - k.h - GAP * 2);
    if (leftwards) // keep the variants in reading order, the first nearest the key
        std::reverse(f.variants.begin(), f.variants.end());
    pickVariant(f);
}

// The variant under the finger; none when it went back down below the key.
void ThumbKeyboard::pickVariant(Finger& f)
{
    const Placed& k = placed[f.key];
    if (f.position.y > k.y + k.h + k.h / 2)
    {
        f.variant = -1;
        return;
    }
    int n     = (int)f.variants.size();
    int i     = (int)std::floor((f.position.x - f.popupX) / KEY_W);
    f.variant = std::clamp(i, 0, n - 1);
}

void ThumbKeyboard::touch(int finger, brls::TouchPhase phase, brls::Point position)
{
    if (placed.empty())
        place();
    brls::Time now = brls::getCPUTimeUsec();
    auto it        = std::find_if(fingers.begin(), fingers.end(), [finger](const Finger& f) { return f.id == finger; });
    switch (phase)
    {
        case brls::TouchPhase::START:
        {
            if (it != fingers.end())
                fingers.erase(it);
            cursor = -1;
            // The other thumb's key types now, before this one: letters come
            // in the order they were touched even when the thumbs overlap.
            for (Finger& f : fingers)
                if (f.variants.empty())
                    commit(f);
            Finger f{ finger };
            f.position = position;
            f.key      = keyAt(position);
            f.down     = now;
            if (f.key >= 0 && !closing)
            {
                Kind kind = placed[f.key].key->kind;
                if (kind == Kind::Shift || kind == Kind::Layer || kind == Kind::Backspace)
                {
                    f.spent  = true;
                    f.repeat = now + REPEAT_DELAY_US;
                    press(f.key);
                    if (kind == Kind::Layer)
                        f.key = keyAt(position); // the same key on the other layer
                }
            }
            else
                f.spent = true;
            fingers.push_back(f);
            break;
        }
        case brls::TouchPhase::STAY:
        {
            if (it == fingers.end())
                break;
            Finger& f  = *it;
            f.position = position;
            if (!f.variants.empty())
                pickVariant(f);
            else if (!f.spent)
            {
                // Sliding onto another key types that one instead.
                int key = keyAt(position);
                if (key != f.key)
                {
                    f.key  = key;
                    f.down = now;
                }
            }
            else if (f.key >= 0 && keyAt(position) != f.key)
                f.key = -1; // slid off Backspace: stop deleting
            break;
        }
        case brls::TouchPhase::END:
        {
            if (it == fingers.end())
                break;
            Finger f = *it;
            fingers.erase(it);
            if (!f.variants.empty())
            {
                if (f.variant >= 0)
                {
                    insert(f.variants[f.variant]);
                    if (shift == Shift::Once)
                        shift = Shift::Off;
                }
            }
            else
                commit(f);
            if (closePending && fingers.empty())
                finishClose();
            break;
        }
        default: break;
    }
}

void ThumbKeyboard::submit() { closeWhenReleased(true); }

void ThumbKeyboard::cancel() { closeWhenReleased(false); }

void ThumbKeyboard::closeWhenReleased(bool confirmed)
{
    if (closing)
        return;
    closing       = true;
    closeConfirms = confirmed;
    if (fingers.empty())
        finishClose();
    else
        closePending = true;
}

void ThumbKeyboard::finishClose()
{
    closePending = false;
    Callback done = closeConfirms ? onDone : nullptr;
    // (a romaji ending such as a lone "n" is finished: ん)
    std::string typed = kana ? kana::convertTail(text, true) : text;
    // Faded out, so the view is deleted after this touch or button has been
    // handled (it may be on the stack below), and with inputs blocked.
    brls::Application::popActivity(brls::TransitionAnimation::FADE);
    if (done)
        done(typed);
}

void ThumbKeyboard::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                         brls::FrameContext* ctx)
{
    if (placed.empty())
        place();

    // Holding: accents after a moment on a letter; Backspace repeats.
    brls::Time now = brls::getCPUTimeUsec();
    for (Finger& f : fingers)
    {
        if (closing || f.key < 0)
            continue;
        Kind kind = placed[f.key].key->kind;
        if (!f.spent && f.variants.empty() && kind == Kind::Char && now - f.down > HOLD_US)
        {
            openVariants(f);
            if (f.variants.empty())
                f.down = now + 1000LL * 1000 * 3600; // nothing to offer: stop looking
        }
        else if (f.spent && kind == Kind::Backspace && now >= f.repeat)
        {
            deleteLast();
            f.repeat = now + REPEAT_EVERY_US;
        }
    }

    // A shade apart from the card behind (white in light mode, the
    // background's grey in dark mode).
    NVGcolor panel = dark() ? nvgRGB(24, 24, 28) : nvgRGB(222, 223, 228);
    for (const brls::Rect& r : panels)
        roundedRect(vg, r.getMinX(), r.getMinY(), r.getWidth(), r.getHeight(), 14, panel);

    for (size_t i = 0; i < placed.size(); i++)
    {
        bool pressed = std::any_of(fingers.begin(), fingers.end(), [i](const Finger& f) {
            return f.key == (int)i && f.variants.empty();
        });
        drawKey(vg, placed[i], pressed, cursor == (int)i);
    }
    drawField(vg);

    // Accents of a held letter, over the keys.
    for (const Finger& f : fingers)
    {
        if (f.variants.empty())
            continue;
        float n = (float)f.variants.size();
        float h = placed[f.key].h;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, f.popupX - 4, f.popupY - 4, n * KEY_W + 8, h + 8, 12);
        nvgFillColor(vg, dark() ? nvgRGB(30, 30, 34) : nvgRGB(250, 250, 252));
        nvgFill(vg);
        nvgStrokeColor(vg, theme("brls/highlight/color1"));
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
        for (size_t i = 0; i < f.variants.size(); i++)
        {
            float cx = f.popupX + i * KEY_W;
            if ((int)i == f.variant)
                roundedRect(vg, cx + 2, f.popupY + 2, KEY_W - 4, h - 4, 8, theme("brls/highlight/color1"));
            label(vg, cx + KEY_W / 2, f.popupY + h / 2, f.variants[i], 30,
                  (int)i == f.variant ? nvgRGB(255, 255, 255) : theme("brls/text"),
                  NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
    }
}

void ThumbKeyboard::drawKey(NVGcontext* vg, const Placed& p, bool pressed, bool isCursor)
{
    const Key& key = *p.key;
    bool upper     = shift != Shift::Off;
    NVGcolor ink  = theme("brls/text");
    NVGcolor fill;
    if (pressed)
        fill = theme("brls/highlight/color1");
    else if (key.kind == Kind::Enter)
        fill = theme("brls/button/primary_enabled_background");
    else if (key.kind == Kind::Char || key.kind == Kind::Space)
        fill = dark() ? nvgRGB(86, 86, 93) : nvgRGB(255, 255, 255);
    else if (key.kind == Kind::Shift && shift != Shift::Off)
        fill = withAlpha(theme("brls/highlight/color1"), 0.55f);
    else
        fill = dark() ? nvgRGB(62, 62, 68) : nvgRGB(198, 200, 208);
    if (pressed)
        ink = nvgRGB(255, 255, 255);
    else if (key.kind == Kind::Enter)
        ink = theme("brls/button/primary_enabled_text");

    roundedRect(vg, p.x, p.y, p.w, p.h, 8, fill);
    if (!dark() && !pressed) // a bottom edge, for white keys on a light panel
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, p.x + 0.5f, p.y + 0.5f, p.w - 1, p.h - 1, 8);
        nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 40));
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
    if (isCursor)
    {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, p.x - 2, p.y - 2, p.w + 4, p.h + 4, 10);
        nvgStrokeColor(vg, theme("brls/highlight/color1"));
        nvgStrokeWidth(vg, 4);
        nvgStroke(vg);
    }

    float cx = p.x + p.w / 2, cy = p.y + p.h / 2;
    nvgStrokeColor(vg, ink);
    nvgFillColor(vg, ink);
    nvgStrokeWidth(vg, 2.5f);
    nvgLineJoin(vg, NVG_ROUND);
    nvgLineCap(vg, NVG_ROUND);
    switch (key.kind)
    {
        case Kind::Char:
        {
            label(vg, cx, cy, upper ? key.upper : key.lower, 30, ink, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            // The first accent, small in the corner: holding offers more.
            const auto* list = variantsOf(key, upper);
            if (list && !list->empty())
                label(vg, p.x + p.w - 7, p.y + 5, (*list)[0], 15, withAlpha(ink, 0.5f),
                      NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            break;
        }
        case Kind::Shift:
        {
            const float s = 12;
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx, cy - s - 2);
            nvgLineTo(vg, cx + s, cy - 2);
            nvgLineTo(vg, cx + s / 2, cy - 2);
            nvgLineTo(vg, cx + s / 2, cy + s - 4);
            nvgLineTo(vg, cx - s / 2, cy + s - 4);
            nvgLineTo(vg, cx - s / 2, cy - 2);
            nvgLineTo(vg, cx - s, cy - 2);
            nvgClosePath(vg);
            if (shift != Shift::Off)
                nvgFill(vg);
            else
                nvgStroke(vg);
            if (shift == Shift::Lock) // caps lock: a bar under the arrow
            {
                nvgBeginPath(vg);
                nvgRect(vg, cx - s / 2, cy + s, s, 3);
                nvgFill(vg);
            }
            break;
        }
        case Kind::Backspace:
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx - 17, cy);
            nvgLineTo(vg, cx - 8, cy - 11);
            nvgLineTo(vg, cx + 15, cy - 11);
            nvgLineTo(vg, cx + 15, cy + 11);
            nvgLineTo(vg, cx - 8, cy + 11);
            nvgClosePath(vg);
            nvgMoveTo(vg, cx - 2, cy - 5);
            nvgLineTo(vg, cx + 8, cy + 5);
            nvgMoveTo(vg, cx + 8, cy - 5);
            nvgLineTo(vg, cx - 2, cy + 5);
            nvgStroke(vg);
            break;
        case Kind::Space:
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx - p.w * 0.22f, cy + 4);
            nvgLineTo(vg, cx - p.w * 0.22f, cy + 10);
            nvgLineTo(vg, cx + p.w * 0.22f, cy + 10);
            nvgLineTo(vg, cx + p.w * 0.22f, cy + 4);
            nvgStrokeColor(vg, withAlpha(ink, 0.6f));
            nvgStroke(vg);
            break;
        case Kind::Layer:
            label(vg, cx, cy, symbols ? "abc" : "123", 22, ink, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            break;
        case Kind::Enter: label(vg, cx, cy, "OK", 24, ink, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE); break;
    }
}

void ThumbKeyboard::drawField(NVGcontext* vg)
{
    NVGcolor ink = theme("brls/text");
    NVGcolor dim = theme("brls/text_disabled");

    // The prompt, cut at the field's width.
    nvgSave(vg);
    nvgIntersectScissor(vg, promptRect.getMinX(), promptRect.getMinY(), promptRect.getWidth(),
                        promptRect.getHeight());
    label(vg, promptRect.getMinX() + 4, promptRect.getMidY(), prompt, 18, dim, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
    // Hiragana mode, at the prompt's end (over it when the prompt is long).
    if (kana)
        label(vg, promptRect.getMaxX() - 4, promptRect.getMidY(), "ankinx/keyboard/hiragana"_i18n, 18,
              theme("brls/highlight/color1"), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

    const brls::Rect& r = fieldRect;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, r.getMinX(), r.getMinY(), r.getWidth(), r.getHeight(), 10);
    nvgFillColor(vg, dark() ? nvgRGB(46, 46, 52) : nvgRGB(255, 255, 255));
    nvgFill(vg);
    nvgStrokeColor(vg, theme("brls/highlight/color1"));
    nvgStrokeWidth(vg, 2);
    nvgStroke(vg);

    // The text, its end in view when it is longer than the field.
    const float pad = 16, size = 28;
    float inner     = r.getWidth() - 2 * pad;
    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    nvgFontSize(vg, size);
    float w     = text.empty() ? 0 : nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, nullptr);
    float start = r.getMinX() + pad + std::min(0.0f, inner - 4 - w);
    nvgSave(vg);
    nvgIntersectScissor(vg, r.getMinX() + 4, r.getMinY(), r.getWidth() - 8, r.getHeight());
    label(vg, start, r.getMidY(), text, size, ink, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
    if ((brls::getCPUTimeUsec() / 530000) % 2 == 0) // caret
    {
        nvgBeginPath(vg);
        nvgRect(vg, start + w + 2, r.getMidY() - size / 2, 2, size);
        nvgFillColor(vg, theme("brls/highlight/color1"));
        nvgFill(vg);
    }

    std::string hints = brls::Hint::getKeyIcon(brls::BUTTON_B, true) + " " + "ankinx/keyboard/delete"_i18n + "   " +
                        brls::Hint::getKeyIcon(brls::BUTTON_Y, true) + " " + "ankinx/keyboard/space"_i18n + "   " +
                        brls::Hint::getKeyIcon(brls::BUTTON_START, true) + " " + "ankinx/keyboard/done"_i18n +
                        "   " + brls::Hint::getKeyIcon(brls::BUTTON_BACK, true) + " " +
                        "ankinx/keyboard/cancel"_i18n;
    nvgSave(vg);
    nvgIntersectScissor(vg, hintRect.getMinX(), hintRect.getMinY(), hintRect.getWidth(), hintRect.getHeight());
    label(vg, hintRect.getMidX(), hintRect.getMidY(), hints, 17, dim, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
}
