// Host test for card scripts (script/dom + script/card_script): patterns
// common in shared-deck templates. Built with the desktop preview; run by
// app/test_scripts.sh. Exits non-zero on the first failure.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "script/card_script.hpp"

namespace
{

std::string prelude;
int failures = 0;

void check(bool ok, const char* what, const std::string& detail = "")
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
    {
        std::printf("     %s\n", detail.c_str());
        failures++;
    }
}

bool has(const std::string& html, const std::string& part) { return html.find(part) != std::string::npos; }

// The data-ankinx-node serial of the element whose markup contains `marker`.
int serialOf(const std::string& html, const std::string& marker)
{
    size_t at = html.find(marker);
    if (at == std::string::npos)
        return -1;
    size_t s = html.find("data-ankinx-node=\"", at);
    return s == std::string::npos ? -1 : std::atoi(html.c_str() + s + 18);
}

std::string readFixture(const std::string& name)
{
    std::ifstream in("tests/fixtures/" + name);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
}

// Minimal Anki template rendering: {{#f}}..{{/f}} sections and {{f}}.
std::string render(std::string tmpl, const std::vector<std::pair<std::string, std::string>>& fields)
{
    for (const auto& [name, value] : fields)
    {
        std::string open = "{{#" + name + "}}", close = "{{/" + name + "}}";
        for (size_t at = tmpl.find(open); at != std::string::npos; at = tmpl.find(open, at))
        {
            size_t end = tmpl.find(close, at);
            if (value.empty())
                tmpl.erase(at, end + close.size() - at);
            else
            {
                tmpl.erase(end, close.size());
                tmpl.erase(at, open.size());
            }
        }
        replaceAll(tmpl, "{{" + name + "}}", value);
    }
    return tmpl;
}

} // namespace

int main(int argc, char** argv)
{
    std::ifstream in(argc > 1 ? argv[1] : "resources/script/prelude.js");
    prelude = std::string(std::istreambuf_iterator<char>(in), {});
    if (prelude.empty())
    {
        std::printf("FAIL cannot read prelude.js\n");
        return 1;
    }
    CardScript::log = [](int, const std::string& msg) { std::printf("     log: %s\n", msg.c_str()); };

    {
        CardScript s(prelude, ".");
        s.show("<div id=\"a\" style=\"display: none\">secret</div>"
               "<button id=\"btn\" onclick=\"document.getElementById('a').style.display='block'\">show</button>",
               "card card1");
        std::string html = s.html();
        check(has(html, "<div id=\"qa\""), "card wrapped in div#qa like Anki", html);
        bool handled = s.click(serialOf(html, "id=\"btn\""));
        html         = s.html();
        check(handled && has(html, "style=\"display: block\""), "onclick toggles an element", html);
    }
    {
        CardScript s(prelude, ".");
        s.show("<div id=\"r\"></div><script>document.getElementById('r').textContent = 'hi ' + "
               "document.body.classList.contains('nightMode');</script>",
               "card nightMode");
        check(has(s.html(), ">hi true<"), "script edits the card on load; body classes visible", s.html());
    }
    {
        CardScript s(prelude, ".");
        s.show("<script>window.choice = 7; sessionStorage.setItem('k', 'v');</script>Q", "card");
        s.show("<div id=\"c\"></div><script>document.getElementById('c').textContent = window.choice + "
               "sessionStorage.getItem('k');</script>",
               "card");
        check(has(s.html(), ">7v<"), "globals and sessionStorage carry from question to answer", s.html());
    }
    {
        CardScript s(prelude, ".");
        s.show("<p class=\"x\">a</p><p class=\"x\">b</p><script>document.querySelectorAll('.x')"
               ".forEach(e => e.classList.add('y')); document.querySelector('p').dataset.fooBar = '1';</script>",
               "card");
        std::string html = s.html();
        check(has(html, "class=\"x y\" data-foo-bar=\"1\"") && has(html, ">b<"), "querySelectorAll, classList, dataset",
              html);
    }
    {
        CardScript s(prelude, ".");
        s.show("<ul id=\"l\"></ul><span id=\"t\">tap</span><script>"
               "const l = document.getElementById('l');"
               "['b','a','c'].sort().forEach(x => { const li = document.createElement('li'); li.textContent = x; "
               "l.appendChild(li); });"
               "document.getElementById('t').addEventListener('click', e => { "
               "e.target.innerHTML = '<b>done</b>'; });</script>",
               "card");
        std::string html = s.html();
        check(has(html, "<li") && html.find(">a<") < html.find(">c<"), "createElement and appendChild", html);
        s.click(serialOf(html, "id=\"t\""));
        check(has(s.html(), "<b"), "addEventListener click and innerHTML", s.html());
    }
    {
        CardScript s(prelude, ".");
        auto start = std::chrono::steady_clock::now();
        s.show("<script>while (true) {}</script><div id=\"z\">still here</div>", "card");
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        check(ms < 2000 && has(s.html(), "still here") && !s.lastError().empty(),
              "an endless loop is stopped and the card still shows", std::to_string(ms) + " ms");
    }
    {
        CardScript s(prelude, ".");
        s.show("<div id=\"t\">0</div><script>setTimeout(() => document.getElementById('t').textContent = '1', "
               "300);</script>",
               "card");
        s.html();
        auto timers = s.takeTimers();
        check(timers.size() == 1 && timers[0].second == 300, "setTimeout is handed to the app");
        if (!timers.empty())
            s.fireTimer(timers[0].first);
        bool dirty       = s.dirty(); // before html(), which clears it
        std::string html = s.html();
        check(dirty && has(html, ">1<"), "the timer callback runs when fired", html);
    }
    {
        // Anki's Image Occlusion template.
        CardScript s(prelude, ".");
        s.show("<div id=\"err\"></div><div id=\"image-occlusion-container\"><img src=\"x.png\"></div>"
               "<script>try { anki.imageOcclusion.setup(); } catch (exc) { "
               "document.getElementById('err').innerHTML = 'Error loading image occlusion'; }</script>",
               "card");
        // (The message is in the script's source, so look at #err itself.)
        std::string html = s.html();
        check(has(html, "id=\"err\" data-ankinx-node=\"3\"></div>"),
              "Image Occlusion template finds anki.imageOcclusion", html);
    }
    {
        CardScript s(prelude, ".");
        s.show("<div id=\"d\"></div><script>document.addEventListener('DOMContentLoaded', () => "
               "document.getElementById('d').textContent = 'loaded'); onShownHook.push(() => "
               "document.getElementById('d').textContent += '+shown');</script>",
               "card");
        check(has(s.html(), ">loaded+shown<"), "DOMContentLoaded and onShownHook", s.html());
    }
    {
        // A user's multiple-choice note type (jQuery, anki-persistence).
        std::vector<std::pair<std::string, std::string>> fields = {
            { "Question", "Capital of France?" }, { "option_1 (A)", "Berlin" }, { "option_2 (B)", "Paris" },
            { "option_3 (C)", "Rome" },           { "option_4 (D)", "" },       { "option_5 (E)", "" },
            { "option_6 (F)", "" },               { "option_7 (G)", "" },       { "option_8 (H)", "" },
            { "Ans", "B" },                       { "Explanation", "Paris is the capital." },
        };
        std::string front = render(readFixture("mcq_front.html"), fields);
        std::string back  = render(readFixture("mcq_back.html"), fields);
        check(!front.empty() && !back.empty(), "MCQ fixtures load");

        CardScript s(prelude, ".");
        s.show(front, "card card1");
        std::string html = s.html();
        check(s.lastError().empty(), "MCQ front runs without errors", s.lastError());
        check(has(html, ">(A)&nbsp;</span>Berlin"), "MCQ front prefixes option letters", html);
        check(!has(html, "id=\"D\""), "MCQ front has only the filled options");

        bool handled = s.click(serialOf(html, "class=\"container\" id=\"B\""));
        html         = s.html();
        size_t b     = html.find("id=\"B\"");
        check(handled && has(html, "class=\"container check\" id=\"B\""), "MCQ tap marks the option", html);
        check(b != std::string::npos && html.find("checked", b) < html.find("</div>", b),
              "MCQ tap checks the option's checkbox", html.substr(b == std::string::npos ? 0 : b, 200));

        auto timers = s.takeTimers();
        for (const auto& timer : timers)
            s.fireTimer(timer.first);
        auto commands = s.takeCommands();
        check(commands.size() == 1 && commands[0] == "ans", "MCQ tap flips to the answer via pycmd",
              std::to_string(commands.size()));

        s.show(back, "card card1");
        html = s.html();
        check(s.lastError().empty(), "MCQ back runs without errors", s.lastError());
        size_t right = html.find("id=\"B\"");
        check(right != std::string::npos && has(html, "class=\"option true\"") &&
                  html.find("class=\"option true\"") > right && has(html.substr(right, 400), "<svg"),
              "MCQ back marks the right answer", html);
        size_t extra = html.find("id=\"extra-header\"");
        int header   = serialOf(html, "id=\"extra-header\"");
        s.click(header);
        html = s.html();
        check(extra != std::string::npos && has(html, "id=\"extra\" class=\"content\"") &&
                  has(html.substr(html.find("id=\"extra\"") - 60, 120), "display: none"),
              "MCQ explanation toggles", html.substr(html.find("id=\"extra\"") - 60, 200));

        // Tapping the "Auto submit" label unticks its switch: a tap then
        // only marks the option.
        CardScript t(prelude, ".");
        t.show(front, "card card1");
        html = t.html();
        t.click(serialOf(html, "for=\"Auto_submit\""));
        html     = t.html();
        size_t a = html.find("id=\"Auto_submit\"");
        check(a != std::string::npos && html.find("checked", a) > html.find(">", a), "MCQ label toggles its checkbox",
              html.substr(a == std::string::npos ? 0 : a, 80));
        t.click(serialOf(html, "class=\"container\" id=\"A\""));
        for (const auto& timer : t.takeTimers())
            t.fireTimer(timer.first);
        html = t.html();
        check(t.takeCommands().empty() && has(html, "class=\"container check\" id=\"A\""),
              "MCQ without auto submit stays on the question");
        t.click(serialOf(html, "for=\"Auto_submit\"")); // restore the stored setting
    }
    {
        CardScript s(prelude, ".");
        s.show("<input type=\"radio\" name=\"r\" id=\"x\" checked><input type=\"radio\" name=\"r\" id=\"y\">"
               "<input type=\"checkbox\" id=\"c\" onclick=\"event.preventDefault()\">",
               "card");
        std::string html = s.html();
        s.click(serialOf(html, "id=\"y\""));
        s.click(serialOf(html, "id=\"c\""));
        html = s.html();
        check(has(html, "id=\"x\" data") && has(html, "id=\"y\" checked") && has(html, "id=\"c\" onclick"),
              "radio groups and cancelled checkbox clicks", html);
    }
    {
        CardScript s(prelude, ".");
        s.show("<p id=\"u\"></p><script>document.getElementById('u').textContent = 'à é سلام';</script>", "card");
        check(has(s.html(), ">à&nbsp;é سلام<"), "non-ASCII text survives serialisation", s.html());
    }
    {
        CardScript s(prelude, ".");
        s.show("<div id=\"i\"></div><script>document.getElementById('i').innerHTML = \"<svg "
               "xmlns:xlink='http://www.w3.org/1999/xlink' viewBox='0 0 9 9' xml:space='preserve'>"
               "<linearGradient id='g'></linearGradient><use xlink:href='#g'/></svg>\";"
               "document.getElementById('i').firstChild.setAttribute('preserveAspectRatio', 'none');</script>",
               "card");
        std::string html = s.html();
        check(has(html, "xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewBox=\"0 0 9 9\" xml:space=\"preserve\"") &&
                  has(html, "<linearGradient") && has(html, "xlink:href=\"#g\"") &&
                  has(html, "preserveAspectRatio=\"none\""),
              "SVG names keep their case and namespace prefixes", html);
    }
    {
        CardScript s(prelude, ".");
        s.show("<div id=\"a\">a</div><div id=\"b\">b</div><div id=\"c\">c</div><script>"
               "document.getElementById('a').addEventListener('click', () => {});"
               "document.getElementById('b').onclick = () => {};"
               "document.body.addEventListener('click', () => {});</script>",
               "card");
        std::string html = s.html();
        auto marked      = [&](const char* id) {
            size_t at = html.find(std::string("id=\"") + id + "\"");
            return at != std::string::npos && html.find("data-ankinx-click", at) < html.find('>', at);
        };
        check(marked("a") && marked("b") && !marked("c") && !has(html, "<body data-ankinx-click"),
              "elements with click handlers are marked for the controller", html);
    }
    check(CardScript::wanted("<b onclick=\"x()\">a</b>") && CardScript::wanted("<script>1</script>") &&
              !CardScript::wanted("<p>plain, with \"on\" = text</p>"),
          "scripts detected only where present");

    std::printf("%s\n", failures ? "SCRIPT TESTS FAILED" : "all script tests passed");
    return failures ? 1 : 0;
}
