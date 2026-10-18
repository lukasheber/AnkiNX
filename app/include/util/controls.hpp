#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

// Reviewer controls: which button does what, per controller layout, with
// user bindings stored in settings.json (see docs/ux-plan.md, section 1).
// B (back) and + (exit) are fixed; everything else can be rebound.
namespace controls
{

enum class Action
{
    ShowAnswer, // also "Good" on the answer side, as before
    Again,
    Hard,
    Good,
    Easy,
    Undo,
    ReplayAudio,
    Hints, // and "Toggle masks" on Image Occlusion answers
    ZoomOut,
    ZoomIn,
    _Count,
};

// Pro Controller, two Joy-Cons or handheld; or one Joy-Con held sideways
// (no ZL/L/R/ZR: borealis reports SL/SR as L/R and the stick press as LSB).
enum class Layout
{
    Standard,
    OneJoyCon,
};

enum class LayoutChoice
{
    Automatic,
    Standard,
    OneJoyCon,
};

std::vector<Action> allActions();
// i18n key of the action's name.
std::string actionName(Action action);

// The layout in use: the setting, or what is connected when Automatic.
Layout activeLayout();
LayoutChoice layoutChoice();
void setLayoutChoice(LayoutChoice choice);
// True when only single Joy-Cons are connected.
bool oneJoyConDetected();

// The system's "Change Grip/Order" screen, allowing one player with one
// sideways Joy-Con. Detached Joy-Cons stay one dual controller until they
// are assigned there, as games do it.
void showControllerSupport();
// Watch the console: when the Joy-Cons come off it (handheld to detached)
// with no Pro Controller connected, open showControllerSupport once.
void watchControllers();

brls::ControllerButton binding(Layout layout, Action action);
// Bind `button`; an action that had it gets this action's old button.
// Returns the action that was swapped, or _Count.
Action setBinding(Layout layout, Action action, brls::ControllerButton button);
void resetBindings(Layout layout);
// Actions bound to `button`, in the order the reviewer tries them.
std::vector<Action> actionsFor(Layout layout, brls::ControllerButton button);
// Buttons that can be bound (not B, +, or the D-pad's up/down, which scroll).
std::vector<brls::ControllerButton> bindableButtons();
// The button's glyph from the Switch's symbol font.
std::string glyph(brls::ControllerButton button);

} // namespace controls
