#pragma once

#include <string>

// The HTML document a card side is laid out in (reviewer, browser preview):
// AnkiNX's base styles (close to Anki's reviewer), the note type's CSS, night
// mode, and the body with inline SVG and ruby prepared for litehtml.
namespace card_document
{

// Night mode follows the Switch's dark theme.
bool nightMode();

// Night-mode styles, after the note type's CSS (`.card.nightMode` beats `.card`).
extern const char* NIGHT_CSS;

std::string build(const std::string& body, const std::string& css, const std::string& bodyClass);

} // namespace card_document
