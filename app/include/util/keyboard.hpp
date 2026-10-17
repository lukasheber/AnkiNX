#pragma once

#include <functional>
#include <string>

// The system keyboard (swkbd library applet on the Switch). `onText` runs with
// the entered text unless the user cancels (on the Switch, before returning;
// the desktop preview's input dialog may call it later).
//
// Above 32 characters, swkbd always uses its "box" layout, which hides header
// text (libnx swkbd.h, SwkbdTextDrawType). So the prompt is passed as guide
// text, which the box shows while empty, and callers show the field name on
// their own screen too.
namespace keyboard
{

using Callback = std::function<void(const std::string&)>;

void text(const std::string& prompt, const std::string& initial, int maxLength, Callback onText);
void password(const std::string& prompt, int maxLength, Callback onText);

} // namespace keyboard
