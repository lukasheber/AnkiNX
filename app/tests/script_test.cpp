// Host test for card scripts (script/dom + script/card_script): patterns
// common in shared-deck templates. Built with the desktop preview; run by
// app/test_scripts.sh. Exits non-zero on the first failure.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

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
    check(CardScript::wanted("<b onclick=\"x()\">a</b>") && CardScript::wanted("<script>1</script>") &&
              !CardScript::wanted("<p>plain, with \"on\" = text</p>"),
          "scripts detected only where present");

    std::printf("%s\n", failures ? "SCRIPT TESTS FAILED" : "all script tests passed");
    return failures ? 1 : 0;
}
