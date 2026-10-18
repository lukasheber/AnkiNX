#include "util/controls.hpp"

#include <array>
#include <map>

#include "util/settings.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

using brls::ControllerButton;

namespace controls
{
namespace
{

constexpr ControllerButton UNBOUND = brls::_BUTTON_MAX;
constexpr size_t ACTIONS           = (size_t)Action::_Count;
using Bindings                     = std::array<ControllerButton, ACTIONS>;

// Defaults (docs/ux-plan.md). Order follows enum Action.
const Bindings STANDARD = {
    brls::BUTTON_A,    // ShowAnswer
    brls::BUTTON_LT,   // Again: ZL
    brls::BUTTON_LB,   // Hard: L
    brls::BUTTON_RB,   // Good: R
    brls::BUTTON_RT,   // Easy: ZR
    brls::BUTTON_BACK, // Undo: −
    brls::BUTTON_Y,    // ReplayAudio
    brls::BUTTON_X,    // Hints
    brls::BUTTON_LSB,  // ZoomOut
    brls::BUTTON_RSB,  // ZoomIn
};
// One Joy-Con sideways: SL / Y / A / SR grade in left-to-right order; X is
// hints on the question side and undo on the answer side.
const Bindings ONE_JOYCON = {
    brls::BUTTON_A,   // ShowAnswer
    brls::BUTTON_LB,  // Again: SL
    brls::BUTTON_Y,   // Hard
    brls::BUTTON_A,   // Good
    brls::BUTTON_RB,  // Easy: SR
    brls::BUTTON_X,   // Undo
    brls::BUTTON_LSB, // ReplayAudio: stick press
    brls::BUTTON_X,   // Hints
    UNBOUND,          // ZoomOut (pinch on the touchscreen)
    UNBOUND,          // ZoomIn
};

const char* ACTION_KEYS[ACTIONS] = { "show_answer", "again", "hard", "good", "easy",
                                     "undo", "replay", "hints", "zoom_out", "zoom_in" };

const std::map<ControllerButton, std::string> BUTTON_NAMES = {
    { brls::BUTTON_A, "A" },        { brls::BUTTON_X, "X" },         { brls::BUTTON_Y, "Y" },
    { brls::BUTTON_LB, "L" },       { brls::BUTTON_RB, "R" },        { brls::BUTTON_LT, "ZL" },
    { brls::BUTTON_RT, "ZR" },      { brls::BUTTON_BACK, "MINUS" },  { brls::BUTTON_LSB, "LSTICK" },
    { brls::BUTTON_RSB, "RSTICK" }, { brls::BUTTON_LEFT, "LEFT" },   { brls::BUTTON_RIGHT, "RIGHT" },
};

const char* layoutKey(Layout layout) { return layout == Layout::Standard ? "standard" : "one_joycon"; }

ControllerButton buttonNamed(const std::string& name)
{
    for (const auto& [button, n] : BUTTON_NAMES)
        if (n == name)
            return button;
    return UNBOUND;
}

anki::json stored() { return settings::get("controls").is_object() ? settings::get("controls") : anki::json::object(); }

Bindings bindingsFor(Layout layout)
{
    Bindings b      = layout == Layout::Standard ? STANDARD : ONE_JOYCON;
    anki::json user = stored()[layoutKey(layout)];
    if (user.is_object())
        for (size_t i = 0; i < ACTIONS; i++)
            if (user[ACTION_KEYS[i]].is_string())
                b[i] = buttonNamed(user[ACTION_KEYS[i]].get<std::string>());
    return b;
}

void storeBindings(Layout layout, const Bindings& b)
{
    anki::json all = stored();
    anki::json map = anki::json::object();
    for (size_t i = 0; i < ACTIONS; i++)
        map[ACTION_KEYS[i]] = b[i] == UNBOUND ? "" : BUTTON_NAMES.at(b[i]);
    all[layoutKey(layout)] = map;
    settings::set("controls", all);
}

} // namespace

std::vector<Action> allActions()
{
    std::vector<Action> out;
    for (size_t i = 0; i < ACTIONS; i++)
        out.push_back((Action)i);
    return out;
}

std::string actionName(Action action) { return std::string("ankinx/controls/") + ACTION_KEYS[(size_t)action]; }

bool oneJoyConDetected()
{
#ifdef __SWITCH__
    bool single = false, full = false;
    if (hidGetNpadStyleSet(HidNpadIdType_Handheld) & HidNpadStyleTag_NpadHandheld)
        full = true;
    for (int id = HidNpadIdType_No1; id <= HidNpadIdType_No8; id++)
    {
        u32 style = hidGetNpadStyleSet((HidNpadIdType)id);
        if (style & (HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadJoyDual))
            full = true;
        if (style & (HidNpadStyleTag_NpadJoyLeft | HidNpadStyleTag_NpadJoyRight))
            single = true;
    }
    return single && !full;
#else
    return false;
#endif
}

LayoutChoice layoutChoice()
{
    std::string choice = stored().value("layout", "automatic");
    return choice == "standard" ? LayoutChoice::Standard
         : choice == "one_joycon" ? LayoutChoice::OneJoyCon
                                  : LayoutChoice::Automatic;
}

void setLayoutChoice(LayoutChoice choice)
{
    anki::json all = stored();
    all["layout"]  = choice == LayoutChoice::Standard  ? "standard"
                   : choice == LayoutChoice::OneJoyCon ? "one_joycon"
                                                       : "automatic";
    settings::set("controls", all);
}

Layout activeLayout()
{
    switch (layoutChoice())
    {
        case LayoutChoice::Standard: return Layout::Standard;
        case LayoutChoice::OneJoyCon: return Layout::OneJoyCon;
        default: return oneJoyConDetected() ? Layout::OneJoyCon : Layout::Standard;
    }
}

ControllerButton binding(Layout layout, Action action) { return bindingsFor(layout)[(size_t)action]; }

Action setBinding(Layout layout, Action action, ControllerButton button)
{
    Bindings b             = bindingsFor(layout);
    ControllerButton old   = b[(size_t)action];
    Action swapped         = Action::_Count;
    for (size_t i = 0; i < ACTIONS; i++)
        if (b[i] == button && (Action)i != action)
        {
            b[i]    = old;
            swapped = (Action)i;
        }
    b[(size_t)action] = button;
    storeBindings(layout, b);
    return swapped;
}

void resetBindings(Layout layout)
{
    anki::json all = stored();
    all.erase(layoutKey(layout));
    settings::set("controls", all);
}

std::vector<Action> actionsFor(Layout layout, ControllerButton button)
{
    std::vector<Action> out;
    Bindings b = bindingsFor(layout);
    for (size_t i = 0; i < ACTIONS; i++)
        if (b[i] == button)
            out.push_back((Action)i);
    return out;
}

std::vector<ControllerButton> bindableButtons()
{
    std::vector<ControllerButton> out;
    for (const auto& [button, _] : BUTTON_NAMES)
        out.push_back(button);
    return out;
}

std::string glyph(ControllerButton button)
{
    return button == UNBOUND ? "—" : brls::Hint::getKeyIcon(button, true);
}

} // namespace controls
