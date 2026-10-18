#include "util/card_document.hpp"

#include <borealis.hpp>

#include "anki/api.hpp"
#include "util/inline_svg.hpp"
#include "util/ruby.hpp"

namespace card_document
{
namespace
{

// Reviewer defaults, close to Anki's own reviewer stylesheet. The note type's
// CSS comes after and can override all of this.
const char* BASE_CSS = R"CSS(
body { margin: 20px; overflow-wrap: break-word; word-wrap: break-word; }
img { max-width: 100%; height: auto; }
hr#answer { border: none; border-top: 1px solid #888888; margin: 16px 0; }
.replay-button { text-decoration: none; color: #3b82f6; padding: 0 4px; }
a.typeans-box { display: block; text-decoration: none; color: inherit; border: 1px solid #888888;
                border-radius: 6px; padding: 8px 12px; margin: 12px auto; max-width: 90%; }
.typeans-prompt { color: #888888; }
#typeans { line-height: 1.75; }
code#typeans { white-space: pre-wrap; }
.typeGood { background: #afa; color: black; }
.typeBad { background: #faa; color: black; }
.typeMissed { background: #ccc; color: black; }
button { display: inline-block; border: 1px solid #999999; border-radius: 6px; padding: 4px 12px;
         margin: 4px; background-color: #e9e9e9; color: #222222; }
rt { font-size: 0.5em; }
.ankinx-ruby { display: inline-block; text-align: center; text-indent: 0; }
.ankinx-ruby > rt { display: block; line-height: 1.2; }
.ankinx-ruby > rb { display: block; }
)CSS";

} // namespace

const char* NIGHT_CSS = R"CSS(
.card.nightMode { color: #e6e6e6; background-color: #2c2c2c; }
.nightMode a { color: #8ab4f8; }
.nightMode button { background-color: #3a3a3a; color: #e6e6e6; border-color: #666666; }
)CSS";

bool nightMode() { return brls::Application::getThemeVariant() == brls::ThemeVariant::DARK; }

std::string build(const std::string& body, const std::string& css, const std::string& bodyClass)
{
    std::string html = "<html><head><style>";
    html += BASE_CSS;
    html += "</style><style>";
    html += css;
    html += "</style>";
    if (nightMode())
        html += std::string("<style>") + NIGHT_CSS + "</style>";
    html += "</head><body class=\"" + bodyClass + "\">";
    html += layoutRuby(inlineSvgToImages(body, anki::dataDir() + "/cache/svg"));
    html += "</body></html>";
    return html;
}

} // namespace card_document
