// Host test for card scripts (script/card_script + resources/script/runtime.js):
// browser behaviour templates rely on, Anki's reviewer flow, and patterns
// common in shared-deck templates. Built with the desktop preview; run by
// app/test_scripts.sh. Exits non-zero if anything failed.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "script/card_script.hpp"

namespace
{

using Clock = std::chrono::steady_clock;

std::string runtime;
std::string media;
std::string logged; // page messages, one per line
int failures = 0;

void check(bool ok, const char* what, const std::string& detail = "")
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
    {
        std::printf("     %s\n", detail.substr(0, 1500).c_str());
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

// The start tag just before `marker` (content), e.g. tagBefore(html, ">b</li>").
std::string tagBefore(const std::string& html, const std::string& marker)
{
    size_t at = html.find(marker);
    if (at == std::string::npos)
        return "<missing>";
    size_t start = html.rfind('<', at);
    return html.substr(start, at + 1 - start);
}

// The serial of the element whose content starts with `marker`.
int serialBefore(const std::string& html, const std::string& marker) { return serialOf(tagBefore(html, marker), "<"); }

// The content of the element whose start tag contains `marker`.
std::string innerOf(const std::string& html, const std::string& marker)
{
    size_t at = html.find(marker);
    if (at == std::string::npos)
        return "<missing>";
    size_t start = html.find('>', at) + 1;
    return html.substr(start, html.find("</", start) - start);
}

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void writeFile(const std::string& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }

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

// Let `ms` of page time pass, running timers when due, as the reviewer does.
void runFor(CardScript& s, int ms)
{
    auto end = Clock::now() + std::chrono::milliseconds(ms);
    for (int next = s.nextTimer(); next >= 0; next = s.nextTimer())
    {
        if (Clock::now() + std::chrono::milliseconds(next) > end)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(next));
        s.runTimers();
    }
}

// A page showing one side (the question unless `answer`).
std::string show(CardScript& s, const std::string& html, bool answer = false, const std::string& bodyClass = "card")
{
    s.show(answer, html, bodyClass);
    return s.html();
}

// A text the page writes into <div id="out"> (run with eval()).
std::string out(CardScript& s, const std::string& expression)
{
    s.eval("document.getElementById('out').textContent = String(" + expression + ");");
    return innerOf(s.html(), "id=\"out\"");
}

void basics()
{
    {
        CardScript s(runtime, media);
        std::string html = show(s, "<div id=\"a\" style=\"display: none\">secret</div>"
                                   "<button id=\"btn\" onclick=\"document.getElementById('a').style.display='block'\">show</button>",
                                false, "card card1");
        check(has(html, "<div id=\"qa\""), "card inside div#qa like Anki", html);
        bool handled = s.tap(serialOf(html, "id=\"btn\""), 10, 10);
        html         = s.html();
        check(handled && has(html, "style=\"display: block;\""), "onclick toggles an element", html);
        check(s.lastError().empty(), "no errors so far", s.lastError());
    }
    {
        CardScript s(runtime, media);
        std::string html = show(s, "<div id=\"r\"></div><script>document.getElementById('r').textContent = 'hi ' + "
                                   "document.body.classList.contains('nightMode');</script>",
                                false, "card nightMode");
        check(has(html, ">hi true<"), "script edits the card on load; body classes visible", html);
        check(s.bodyClass() == "card nightMode", "body class from the reviewer", s.bodyClass());
    }
    {
        CardScript s(runtime, media);
        show(s, "<script>window.choice = 7; sessionStorage.setItem('k', 'v');</script>Q");
        std::string html = show(s, "<div id=\"c\"></div><script>document.getElementById('c').textContent = window.choice + "
                                   "sessionStorage.getItem('k');</script>",
                                true);
        check(has(html, ">7v<"), "globals and sessionStorage carry from question to answer", html);
    }
    {
        CardScript s(runtime, media);
        std::string html = show(s, "<p class=\"x\">a</p><p class=\"x\">b</p><script>document.querySelectorAll('.x')"
                                   ".forEach(e => e.classList.add('y')); document.querySelector('p').dataset.fooBar = '1';</script>");
        check(has(tagBefore(html, ">a</p>"), "class=\"x y\"") && has(tagBefore(html, ">a</p>"), "data-foo-bar=\"1\"") &&
                  has(tagBefore(html, ">b</p>"), "class=\"x y\""),
              "querySelectorAll, classList, dataset", html);
    }
    {
        CardScript s(runtime, media);
        std::string html = show(s, "<ul id=\"l\"></ul><span id=\"t\">tap</span><script>"
                                   "const l = document.getElementById('l');"
                                   "['b','a','c'].sort().forEach(x => { const li = document.createElement('li'); li.textContent = x; "
                                   "l.appendChild(li); });"
                                   "document.getElementById('t').addEventListener('click', e => { "
                                   "e.target.innerHTML = '<b>done</b>'; });</script>");
        check(has(html, "<li") && html.find(">a<") < html.find(">c<"), "createElement and appendChild", html);
        s.tap(serialOf(html, "id=\"t\""), 1, 1);
        check(has(s.html(), "<b"), "addEventListener click and innerHTML", s.html());
    }
    {
        CardScript s(runtime, media);
        auto start       = Clock::now();
        std::string html = show(s, "<script>while (true) {}</script><div id=\"z\">still here</div>");
        auto ms          = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        check(ms < 2000 && has(html, "still here") && !s.lastError().empty(), "an endless loop is stopped and the card still shows",
              std::to_string(ms) + " ms; " + s.lastError());
        html = show(s, "<div id=\"y\"></div><script>document.getElementById('y').textContent = 'answer ok';</script>", true);
        check(has(html, "answer ok"), "the answer side still works after a stopped script", html);
    }
    {
        CardScript s(runtime, media);
        show(s, "<div id=\"t\">0</div><script>setTimeout(() => document.getElementById('t').textContent = '1', 300);</script>");
        int next = s.nextTimer();
        check(next > 200 && next <= 300, "setTimeout is handed to the app", std::to_string(next));
        runFor(s, 400);
        bool dirty       = s.dirty();
        std::string html = s.html();
        check(dirty && has(html, ">1<"), "the timer callback runs when due", html);
        check(s.nextTimer() == -1, "no timers left");
    }
    {
        // Anki's Image Occlusion template.
        CardScript s(runtime, media);
        std::string html = show(s, "<div id=\"err\"></div><div id=\"image-occlusion-container\"><img src=\"x.png\"></div>"
                                   "<script>try { anki.imageOcclusion.setup(); } catch (exc) { "
                                   "document.getElementById('err').innerHTML = 'Error loading image occlusion'; }</script>");
        check(innerOf(html, "id=\"err\"").empty(), "Image Occlusion template finds anki.imageOcclusion", html);
    }
    {
        CardScript s(runtime, media);
        std::string html = show(s, "<div id=\"d\"></div><script>document.addEventListener('DOMContentLoaded', () => "
                                   "document.getElementById('d').textContent = 'loaded'); onShownHook.push(() => "
                                   "document.getElementById('d').textContent += '+shown'); window.addEventListener('load', () => "
                                   "document.getElementById('d').textContent += '+load');</script>");
        check(has(html, ">loaded+shown+load<"), "DOMContentLoaded, onShownHook, load in order", html);
        html = show(s, "<div id=\"e\"></div><script>window.onload = () => document.getElementById('e').textContent += 'A';</script>", true);
        check(innerOf(html, "id=\"e\"") == "A", "each side's load handlers fire once", html);
    }
    {
        CardScript s(runtime, media);
        std::string html = show(s, "<input type=\"radio\" name=\"r\" id=\"x\" checked><input type=\"radio\" name=\"r\" id=\"y\">"
                                   "<input type=\"checkbox\" id=\"c\" onclick=\"event.preventDefault()\">");
        s.tap(serialOf(html, "id=\"y\""), 1, 1);
        s.tap(serialOf(html, "id=\"c\""), 1, 1);
        html = s.html();
        check(has(html, "id=\"x\" data") && has(html, "id=\"y\" checked") && has(html, "id=\"c\" onclick"),
              "radio groups and cancelled checkbox clicks", html);
    }
    {
        CardScript s(runtime, media);
        std::string html = show(s, "<p id=\"u\"></p><script>document.getElementById('u').textContent = 'à\u00a0é سلام';</script>");
        check(has(html, ">à&nbsp;é سلام<"), "non-ASCII text survives serialisation", html);
    }
    {
        CardScript s(runtime, media);
        std::string html = show(s, "<div id=\"i\"></div><script>document.getElementById('i').innerHTML = \"<svg "
                                   "xmlns:xlink='http://www.w3.org/1999/xlink' viewBox='0 0 9 9' xml:space='preserve'>"
                                   "<linearGradient id='g'></linearGradient><use xlink:href='#g'/></svg>\";"
                                   "document.getElementById('i').firstChild.setAttribute('preserveAspectRatio', 'none');</script>");
        check(has(html, "xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewBox=\"0 0 9 9\" xml:space=\"preserve\"") &&
                  has(html, "<linearGradient") && has(html, "xlink:href=\"#g\"") && has(html, "preserveAspectRatio=\"none\""),
              "SVG names keep their case and namespace prefixes", html);
    }
    {
        CardScript s(runtime, media);
        std::string html = show(s, "<div id=\"a\">a</div><div id=\"b\">b</div><div id=\"c\">c</div><script>"
                                   "document.getElementById('a').addEventListener('click', () => {});"
                                   "document.getElementById('b').onclick = () => {};"
                                   "document.body.addEventListener('click', () => {});</script>");
        auto marked = [&](const char* id) {
            size_t at = html.find(std::string("id=\"") + id + "\"");
            return at != std::string::npos && html.find("data-ankinx-click", at) < html.find('>', at);
        };
        check(marked("a") && marked("b") && !marked("c"), "elements with click handlers are marked for the controller", html);
        check(!s.tap(serialOf(html, "id=\"c\""), 1, 1), "a tap only a page-wide listener sees, doing nothing, flips the card");
    }
    check(CardScript::wanted("<b onclick=\"x()\">a</b>") && CardScript::wanted("<script>1</script>") &&
              !CardScript::wanted("<p>plain, with \"on\" = text</p>"),
          "scripts detected only where present");
}

void dom()
{
    CardScript s(runtime, media);
    show(s, "<div id=\"out\"></div><div id=\"box\"><span id=\"in\">x</span></div>"
            "<script>let shared = 5; class Thing { get v() { return 'class'; } }</script>"
            "<script>document.getElementById('out').dataset.v = shared + new Thing().v;</script>");
    check(has(s.html(), "data-v=\"5class\""), "top-level let and class are shared between scripts", s.html());
    check(out(s, "[typeof Node, typeof HTMLElement, typeof Event, typeof MutationObserver, typeof TreeWalker, typeof Range,"
                 " typeof DOMParser, typeof URL, typeof fetch, typeof XMLHttpRequest].join()") ==
              "function,function,function,function,function,function,function,function,function,function",
          "DOM and web classes exist");
    check(out(s, "(() => { const log = []; const box = document.getElementById('box'), inner = document.getElementById('in');"
                 "document.addEventListener('click', e => log.push('doc-capture:' + e.eventPhase), true);"
                 "box.addEventListener('click', function (e) { log.push('box:' + (this === box) + (e.currentTarget === box)); });"
                 "inner.addEventListener('click', e => log.push('target:' + e.eventPhase));"
                 "document.addEventListener('click', e => log.push('doc-bubble:' + e.eventPhase));"
                 "inner.dispatchEvent(new MouseEvent('click', { bubbles: true }));"
                 "box.addEventListener('ping', e => { log.push('stopped'); e.stopPropagation(); });"
                 "document.addEventListener('ping', () => log.push('not here'));"
                 "inner.dispatchEvent(new CustomEvent('ping', { bubbles: true, detail: 1 }));"
                 "return log.join(); })()") == "doc-capture:1,target:2,box:truetrue,doc-bubble:3,stopped",
          "capture, target and bubble phases; this is the current target; stopPropagation");
    check(out(s, "(() => { let n = 0; const b = document.getElementById('box'); const f = () => n++;"
                 "b.addEventListener('x', f, { once: true }); b.dispatchEvent(new Event('x')); b.dispatchEvent(new Event('x'));"
                 "const ac = new AbortController(); b.addEventListener('y', f, { signal: ac.signal }); ac.abort();"
                 "b.dispatchEvent(new Event('y')); const e = new Event('z', { cancelable: true });"
                 "b.addEventListener('z', ev => ev.preventDefault(), { passive: true });"
                 "return n + ':' + b.dispatchEvent(e); })()") == "1:true",
          "once, signal and passive listener options");
    check(out(s, "(() => { const b = document.createElement('button'); b.setAttribute('onclick', 'this.textContent = tagName + typeof event; return false');"
                 "document.body.appendChild(b); const ok = b.dispatchEvent(new MouseEvent('click', { cancelable: true }));"
                 "const t = b.textContent; b.remove(); return t + ':' + ok; })()") == "BUTTONobject:false",
          "on… attributes: scope chain, event argument, return false cancels");
    check(out(s, "(() => { const d = document.createElement('div'); d.style.backgroundColor = 'red'; d.style.setProperty('--x', '1');"
                 "d.style.cssText += 'margin-top: 2px'; d.style.backgroundColor = ''; return d.getAttribute('style') + '|' + d.style.marginTop; })()") ==
              "--x: 1; margin-top: 2px;|2px",
          "element.style as CSSOM");
    s.eval("window.records = 0; new MutationObserver(r => window.records += r.length).observe(document.getElementById('box'), "
           "{ childList: true }); document.getElementById('box').append('!');");
    check(out(s, "window.records") == "1", "MutationObserver callbacks run");
    check(out(s, "(() => { const w = document.createTreeWalker(document.getElementById('box')); let n = 0; while (w.nextNode()) n++;"
                 "const t = document.createElement('template'); t.innerHTML = '<i>t</i>';"
                 "return n + t.content.firstChild.outerHTML + new DOMParser().parseFromString('<p>z</p>', 'text/html').body.textContent; })()") ==
              "3&lt;i&gt;t&lt;/i&gt;z",
          "TreeWalker, template, DOMParser");
    check(out(s, "(() => { const u = new URL('../b/c.png?x=1#f', 'http://127.0.0.1/a/d/e.html'); u.searchParams.append('y', 'a b');"
                 "return u.href + ' ' + new URL('_img.png', location.href).href; })()") ==
              "http://127.0.0.1/a/b/c.png?x=1&amp;y=a+b#f http://127.0.0.1/_img.png",
          "URL and URLSearchParams");
    s.eval("window.order = []; Promise.resolve().then(() => order.push('micro')); setTimeout(() => order.push('task'), 0);"
           "queueMicrotask(() => order.push('micro2')); order.push('sync');");
    check(out(s, "order.join()") == "sync,micro,micro2,task", "microtasks before tasks; zero-delay timers run before drawing");
    s.eval("window.onerror = (m) => { window.caught = m; return true; }; setTimeout(() => { throw new Error('boom'); }, 0);");
    check(out(s, "window.caught") == "Uncaught Error: boom", "uncaught errors reach window.onerror");
    s.eval("document.cookie = 'a=1'; document.cookie = 'b=2; path=/'; localStorage.k = 'v';");
    check(out(s, "document.cookie + '|' + localStorage.getItem('k') + localStorage.length") == "a=1; b=2|v1", "cookies and localStorage");
    logged.clear();
    s.eval("console.info('n=%d', 3, $('#box'), { a: [1, { b: 2 }] }, document.body)");
    check(logged == "n=3 jQuery(1) [<div#box>] {a: [1, [Object]]} <body.card>\n", "console output is short, as a console shows it",
          logged);
    check(out(s, "[typeof structuredClone({a: [1]}).a, new TextEncoder().encode('é').length, crypto.randomUUID().length, "
                 "matchMedia('(prefers-color-scheme: dark)').matches].join()") == "object,2,36,false",
          "structuredClone, TextEncoder, crypto, matchMedia");
}

void jquery()
{
    CardScript s(runtime, media);
    std::string html = show(s, "<div id=\"out\"></div><ul id=\"l\"><li class=\"o\">a</li><li class=\"o\">b</li></ul>"
                               "<div id=\"h\" style=\"display: none\">hidden</div><script>"
                               "$(function () { $('#out').attr('data-ready', $.fn.jquery); });"
                               "$(document).on('click', '.o', function () { $(this).addClass('picked').siblings().removeClass('picked'); });"
                               "</script>");
    check(has(html, "data-ready=\"3.7.1\""), "the real jQuery 3.7.1; $(fn) runs before the first drawing", html);
    s.tap(serialBefore(html, ">b</li>"), 1, 1);
    html = s.html();
    check(has(tagBefore(html, ">b</li>"), "class=\"o picked\"") && !has(tagBefore(html, ">a</li>"), "picked"),
          "delegated jQuery handlers get taps", html);
    s.eval("$('#h').show(); $('#l li').first().hide();");
    html = s.html();
    check(!has(tagBefore(html, ">hidden</div>"), "none") && has(tagBefore(html, ">a</li>"), "display: none;"), "show() and hide()", html);
    s.eval("$('#l').append('<li id=\"n\">n</li><script>document.getElementById(\"n\").textContent = \"ran\";<\\/script>');");
    check(has(s.html(), ">ran<"), "scripts in markup that jQuery inserts run", s.html());
    s.eval("$('#h').fadeOut(100, () => $('#out').text('faded'));");
    runFor(s, 500);
    html = s.html();
    check(innerOf(html, "id=\"out\"") == "faded" && has(tagBefore(html, ">hidden</div>"), "display: none"),
          "jQuery animations finish through timers", html);
    s.eval("$.getJSON('_data.json', d => $('#out').text(d.x));");
    runFor(s, 100);
    check(innerOf(s.html(), "id=\"out\"") == "from media", "$.getJSON reads media files", s.html());
}

void scripts()
{
    CardScript s(runtime, media);
    std::string html = show(s, "<div id=\"out\"></div><script src=\"_lib.js\"></script>"
                               "<script>document.getElementById('out').textContent = libAnswer();</script>");
    check(innerOf(html, "id=\"out\"") == "42", "<script src> from the media folder runs before the next script", html);
    html = show(s, "<div id=\"out\"></div><script src=\"https://code.jquery.com/jquery-3.6.0.min.js\"></script>"
                   "<script src=\"_missing.js\" onerror=\"document.getElementById('out').textContent = 'error event'\"></script>",
                true);
    check(innerOf(html, "id=\"out\"") == "error event", "missing script files fire error; CDN jQuery uses the bundled one", html);
    check(out(s, "typeof $") == "function", "$ still there");

    CardScript t(runtime, media);
    html = show(t, "<div id=\"out\"></div><p id=\"w\"><script>document.write('<b id=\"written\">w</b>');</script></p>"
                   "<script>const el = document.createElement('script'); el.textContent = 'window.dyn = 1'; document.head.appendChild(el);"
                   "document.getElementById('out').textContent = window.dyn + ':' + document.currentScript.tagName;"
                   "document.getElementById('w').innerHTML += '<script>window.never = 1<\\/script>';</script>"
                   "<script type=\"text/template\">window.never = 2</script>");
    check(has(html, "<b id=\"written\""), "document.write() during a script", html);
    check(innerOf(html, "id=\"out\"") == "1:SCRIPT", "inserted scripts run at once; currentScript", html);
    check(out(t, "typeof window.never") == "undefined", "scripts from innerHTML and non-JS types don't run");

    CardScript u(runtime, media);
    html = show(u, "<div id=\"out\"></div><script type=\"module\">import { m } from './_mod.js';"
                   "document.getElementById('out').textContent = m;</script>");
    check(innerOf(html, "id=\"out\"") == "module", "module scripts import from the media folder", html);
    u.eval("fetch('_data.json').then(r => r.json()).then(d => document.getElementById('out').textContent = d.x);");
    check(innerOf(u.html(), "id=\"out\"") == "from media", "fetch() reads media files", u.html());
    u.eval("fetch('https://example.com/').catch(e => document.getElementById('out').textContent = e.name);");
    check(innerOf(u.html(), "id=\"out\"") == "TypeError", "no network: fetch() rejects", u.html());
    u.eval("document.head.insertAdjacentHTML('beforeend', '<style>.x { color: red }</style><link rel=\"stylesheet\" href=\"_style.css\">');");
    std::string css = u.extraCss();
    check(has(css, ".x { color: red }") && has(css, ".from-media"), "styles scripts add to <head>", css);
    u.eval("pycmd('ease3'); new Audio('_a.mp3').play(); alert('hi'); document.querySelector('#out').insertAdjacentHTML('afterend', "
           "'<a id=\"lnk\" href=\"playsound:q:0\">p</a>'); document.getElementById('lnk').click();");
    std::string commands;
    for (const auto& c : u.takeCommands())
        commands += c + ";";
    check(commands == "updateToolbar;ease3;play:_a.mp3;alert:hi;link:playsound:q:0;", "commands to the reviewer", commands);
}

void forms()
{
    CardScript s(runtime, media);
    std::string html = show(s, "<div id=\"out\"></div><label id=\"lab\"><input type=\"checkbox\" id=\"cb\"> tick</label>"
                               "<details id=\"det\"><summary id=\"sum\">more</summary>body</details>"
                               "<select id=\"sel\"><option value=\"a\">A</option><option value=\"b\" selected>B</option></select>"
                               "<form id=\"f\" onsubmit=\"document.getElementById('out').textContent = 'submitted:' + this.elements.t.value; return false\">"
                               "<input name=\"t\" id=\"t\" value=\"default\"><button id=\"go\">go</button></form>"
                               "<script>document.getElementById('cb').addEventListener('change', e => "
                               "document.getElementById('out').textContent = 'change:' + e.target.checked);</script>");
    s.tap(serialOf(html, "id=\"lab\""), 1, 1);
    html = s.html();
    check(innerOf(html, "id=\"out\"") == "change:true" && has(html, "id=\"cb\" checked"), "a tap on a label checks its box", html);
    s.tap(serialOf(html, "id=\"sum\""), 1, 1);
    check(has(tagBefore(s.html(), "><summary id=\"sum\""), "open"), "<summary> opens <details>", s.html());
    check(out(s, "document.getElementById('sel').value + document.getElementById('sel').selectedIndex") == "b1", "select value");
    s.eval("document.getElementById('t').value = 'typed'; document.getElementById('sel').value = 'a';");
    html = s.html();
    check(has(html, "value=\"typed\"") && has(tagBefore(html, ">A</option>"), "selected") && !has(tagBefore(html, ">B</option>"), "selected"),
          "control state is drawn", html);
    s.tap(serialOf(html, "id=\"go\""), 1, 1);
    check(innerOf(s.html(), "id=\"out\"") == "submitted:typed", "a submit button submits its form", s.html());
    check(out(s, "(() => { const t = document.getElementById('t'); t.focus(); return document.activeElement === t; })()") == "true",
          "focus() and activeElement");
}

// A pretend renderer: every element a 100 x 20 box, stacked.
CardScript::Host fakeHost(int& layouts, float& scrolled)
{
    CardScript::Host host;
    host.layout = [&layouts](const std::string& body, const std::string&, const std::string&) {
        layouts++;
        CardScript::Layout boxes;
        int row = 0;
        for (size_t at = body.find("data-ankinx-node=\""); at != std::string::npos; at = body.find("data-ankinx-node=\"", at + 1))
        {
            CardScript::Box b;
            b.x = 10;
            b.y = 20.0f * row++;
            b.width = b.clientWidth = 100;
            b.height = b.clientHeight = 20;
            b.style  = { { "display", "block" }, { "color", "rgb(1, 2, 3)" } };
            boxes[std::atoi(body.c_str() + at + 18)] = b;
        }
        return boxes;
    };
    host.viewport = [] { return CardScript::Viewport { 800, 600, 40, 1.5f }; };
    host.scrollTo = [&scrolled](float y) { scrolled = y; };
    return host;
}

void layout()
{
    int layouts    = 0;
    float scrolled = -1;
    CardScript s(runtime, media, fakeHost(layouts, scrolled));
    show(s, "<div id=\"out\"></div><p id=\"p\">p</p>");
    int before = layouts;
    check(out(s, "[document.getElementById('p').offsetHeight, document.getElementById('p').getBoundingClientRect().top,"
                 " getComputedStyle(document.getElementById('p')).color, innerWidth, devicePixelRatio, scrollY].join()") ==
              "20,0,rgb(1, 2, 3),800,1.5,40",
          "sizes, positions and computed style from the renderer");
    check(layouts > before, "layout queries lay the card out");
    before = layouts;
    s.eval("const p = document.getElementById('p'); p.offsetHeight; p.offsetWidth; p.getBoundingClientRect();");
    check(layouts == before + 1, "unchanged DOM: one layout for many queries", std::to_string(layouts - before));
    s.eval("document.body.append(document.createElement('div')); document.getElementById('p').offsetTop;");
    check(layouts == before + 2, "a change, then a query: laid out again", std::to_string(layouts - before));
    s.eval("document.getElementById('p').scrollIntoView();");
    check(scrolled >= 0, "scrollIntoView scrolls the card", std::to_string(scrolled));
    check(out(s, "$('#p').is(':visible') + ':' + $('#p').height()") == "true:20", "jQuery sees the layout");
}

void mcq()
{
    // A user's multiple-choice note type (jQuery, anki-persistence).
    std::vector<std::pair<std::string, std::string>> fields = {
        { "Question", "Capital of France?" }, { "option_1 (A)", "Berlin" }, { "option_2 (B)", "Paris" },
        { "option_3 (C)", "Rome" },           { "option_4 (D)", "" },       { "option_5 (E)", "" },
        { "option_6 (F)", "" },               { "option_7 (G)", "" },       { "option_8 (H)", "" },
        { "Ans", "B" },                       { "Explanation", "Paris is the capital." },
    };
    std::string front = render(readFile("tests/fixtures/mcq_front.html"), fields);
    std::string back  = render(readFile("tests/fixtures/mcq_back.html"), fields);
    check(!front.empty() && !back.empty(), "MCQ fixtures load");

    CardScript s(runtime, media);
    auto start       = Clock::now();
    std::string html = show(s, front, false, "card card1");
    auto ms          = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    std::printf("     MCQ front shown in %lld ms\n", (long long)ms);
    check(s.lastError().empty(), "MCQ front runs without errors", s.lastError());
    check(has(html, ">(A)&nbsp;</span>Berlin"), "MCQ front prefixes option letters", html);
    check(!has(html, "id=\"D\""), "MCQ front has only the filled options");

    bool handled = s.tap(serialOf(html, "class=\"container\" id=\"B\""), 1, 1);
    html         = s.html();
    size_t b     = html.find("id=\"B\"");
    check(handled && has(html, "class=\"container check\" id=\"B\""), "MCQ tap marks the option", html);
    check(b != std::string::npos && html.find("checked", b) < html.find("</div>", b), "MCQ tap checks the option's checkbox",
          html.substr(b == std::string::npos ? 0 : b, 200));

    runFor(s, 1500);
    std::vector<std::string> commands;
    for (const auto& c : s.takeCommands())
        if (c != "updateToolbar")
            commands.push_back(c);
    check(commands.size() == 1 && commands[0] == "ans", "MCQ tap flips to the answer via pycmd", std::to_string(commands.size()));

    html = show(s, back, true, "card card1");
    check(s.lastError().empty(), "MCQ back runs without errors", s.lastError());
    size_t right = html.find("id=\"B\"");
    check(right != std::string::npos && has(html, "class=\"option true\"") && html.find("class=\"option true\"") > right &&
              has(html.substr(right, 400), "<svg"),
          "MCQ back marks the right answer", html);
    s.tap(serialOf(html, "id=\"extra-header\""), 1, 1);
    runFor(s, 500);
    html = s.html();
    size_t extra = html.find("id=\"extra\"");
    check(extra != std::string::npos && has(html.substr(extra, 120), "display: none"), "MCQ explanation toggles",
          html.substr(extra == std::string::npos ? 0 : extra, 200));

    // Tapping the "Auto submit" label unticks its switch: a tap then only
    // marks the option.
    CardScript t(runtime, media);
    html = show(t, front, false, "card card1");
    t.tap(serialOf(html, "for=\"Auto_submit\""), 1, 1);
    html     = t.html();
    size_t a = html.find("id=\"Auto_submit\"");
    check(a != std::string::npos && html.find("checked", a) > html.find(">", a), "MCQ label toggles its checkbox",
          html.substr(a == std::string::npos ? 0 : a, 80));
    t.tap(serialOf(html, "class=\"container\" id=\"A\""), 1, 1);
    runFor(t, 1500);
    html = t.html();
    std::string sent;
    for (const auto& c : t.takeCommands())
        sent += c + ";";
    size_t optionA = html.find("id=\"A\"");
    check(!has(sent, "ans;") && has(html, "class=\"container check\" id=\"A\""), "MCQ without auto submit stays on the question",
          sent + " " + html.substr(optionA < 40 ? 0 : optionA - 40, 300));
    t.tap(serialOf(html, "for=\"Auto_submit\""), 1, 1); // restore the stored setting
}

void timing()
{
    for (int i = 0; i < 3; i++)
    {
        auto start = Clock::now();
        CardScript s(runtime, media);
        auto made = Clock::now();
        show(s, "<div id=\"out\"></div><script>document.getElementById('out').textContent = 'x';</script>");
        auto shown = Clock::now();
        s.eval("$('#out').text('y');");
        auto withJQuery = Clock::now();
        auto ms         = [](auto a, auto b) { return (long long)std::chrono::duration_cast<std::chrono::microseconds>(b - a).count() / 1000.0; };
        std::printf("     card %d: page %.1f ms, side %.1f ms, first jQuery use %.1f ms\n", i + 1, (double)ms(start, made),
                    (double)ms(made, shown), (double)ms(shown, withJQuery));
    }
}

// Scheduling states as the backend sends them (anki_switch_core/src/scheduling.rs):
// Anki's own test input (ts/reviewer/lib.test.ts), keys in another order.
const char* REVIEW_STATES = R"({"context": {"deckName": "hello", "seed": "123"}, "states": {
  "easy": {"normal": {"review": {"scheduledDays": 6, "elapsedDays": 0, "easeFactor": 2, "lapses": 4, "leeched": false}}},
  "current": {"customData": "{\"v\":\"v3.20.0\",\"seed\":2104}",
              "normal": {"review": {"elapsedDays": 2, "scheduledDays": 1, "easeFactor": 1.850000023841858, "lapses": 4, "leeched": false}}},
  "again": {"normal": {"relearning": {"learning": {"remainingSteps": 1, "scheduledSecs": 600},
            "review": {"scheduledDays": 1, "elapsedDays": 0, "easeFactor": 1.649999976158142, "lapses": 5, "leeched": false}}}},
  "hard": {"normal": {"review": {"scheduledDays": 2, "elapsedDays": 0, "easeFactor": 1.7000000476837158, "lapses": 4, "leeched": false}}},
  "good": {"normal": {"review": {"scheduledDays": 4, "elapsedDays": 0, "easeFactor": 1.850000023841858, "lapses": 4, "leeched": false}}}}})";

// What a transform that changes nothing hands back: protobuf's field order,
// zero values written out, and current's custom data in each answer's.
const char* REVIEW_UNCHANGED =
    R"({"current":{"normal":{"review":{"scheduledDays":1,"elapsedDays":2,"easeFactor":1.850000023841858,"lapses":4,"leeched":false}},)"
    R"("customData":"{\"v\":\"v3.20.0\",\"seed\":2104}"},)"
    R"("again":{"normal":{"relearning":{"review":{"scheduledDays":1,"elapsedDays":0,"easeFactor":1.649999976158142,"lapses":5,"leeched":false},)"
    R"("learning":{"remainingSteps":1,"scheduledSecs":600,"elapsedSecs":0}}},"customData":"{\"v\":\"v3.20.0\",\"seed\":2104}"},)"
    R"("hard":{"normal":{"review":{"scheduledDays":2,"elapsedDays":0,"easeFactor":1.7000000476837158,"lapses":4,"leeched":false}},)"
    R"("customData":"{\"v\":\"v3.20.0\",\"seed\":2104}"},)"
    R"("good":{"normal":{"review":{"scheduledDays":4,"elapsedDays":0,"easeFactor":1.850000023841858,"lapses":4,"leeched":false}},)"
    R"("customData":"{\"v\":\"v3.20.0\",\"seed\":2104}"},)"
    R"("easy":{"normal":{"review":{"scheduledDays":6,"elapsedDays":0,"easeFactor":2,"lapses":4,"leeched":false}},)"
    R"("customData":"{\"v\":\"v3.20.0\",\"seed\":2104}"}})";

// A new card's: learning steps, Easy graduating it.
const char* NEW_STATES = R"({"context": {"deckName": "Spanish::Verbs", "seed": "18446744073709551615", "desiredRetention": 0.8999999761581421},
  "states": {"current": {"normal": {"new": {"position": 3}}, "customData": ""},
  "again": {"normal": {"learning": {"remainingSteps": 2, "scheduledSecs": 60, "elapsedSecs": 0}}},
  "hard": {"normal": {"learning": {"remainingSteps": 2, "scheduledSecs": 330, "elapsedSecs": 0}}},
  "good": {"normal": {"learning": {"remainingSteps": 1, "scheduledSecs": 600, "elapsedSecs": 0}}},
  "easy": {"normal": {"review": {"scheduledDays": 4, "elapsedDays": 0, "easeFactor": 2.5, "lapses": 0, "leeched": false}}}}})";

// The example in Anki's manual (Deck Options > Advanced > Custom scheduling).
const char* MANUAL_EXAMPLE = R"(// print the existing states
console.log(JSON.stringify(states, null, 4));

// load the debugger if the web inspector is open
debugger;

// if the hard button is a learning step, make it
// a 123 minute delay
if (states.hard.normal?.learning) {
    states.hard.normal.learning.scheduledSecs = 123 * 60;
}

// apply the same change in a rescheduling filtered deck
if (states.hard.filtered?.rescheduling?.originalState?.learning) {
    states.hard.filtered.rescheduling.originalState.learning.scheduledSecs = 123 * 60;
}

// increase ease factor by 0.2 when Easy used on a review
if (states.good.normal?.review) {
    states.easy.normal.review.easeFactor = states.good.normal.review.easeFactor + 0.2;
}
// a comment on the last line)";

// Custom scheduling (scheduling.js): Anki's mutateNextCardStates on the
// states the reviewer gives, as Anki's reviewer runs the collection's code.
void scheduling()
{
    std::string states = REVIEW_STATES, handedKey, handed;
    CardScript::Host host;
    host.schedulingStates    = [&] { return states; };
    host.setSchedulingStates = [&](const std::string& key, const std::string& json) {
        handedKey = key;
        handed    = json;
    };
    CardScript s(runtime, media, host);
    // The code as the reviewer runs it: true if it started and said it finished.
    auto run = [&](const std::string& code) {
        handed.clear();
        handedKey.clear();
        logged.clear();
        bool started  = s.mutateStates("k42", code);
        bool finished = false;
        for (const auto& c : s.takeCommands())
            finished = finished || c == "statesMutated";
        return started && finished;
    };

    check(run("") && handedKey == "k42" && handed == REVIEW_UNCHANGED,
          "states handed back with the key, as protobuf-es writes them; custom data in each answer", handed);
    check(run("customData.easy = { foo: 'hello world' };") &&
              has(handed, R"("easeFactor":2,"lapses":4,"leeched":false}},"customData":"{\"foo\":\"hello world\"}"})") &&
              has(handed, R"("hard":{"normal":{"review":{"scheduledDays":2,"elapsedDays":0,"easeFactor":1.7000000476837158,"lapses":4,"leeched":false}},"customData":"{\"v\":\"v3.20.0\",\"seed\":2104}"})"),
          "custom data changed for one answer", handed);
    check(run("states.good.normal.review.scheduledDays = 10;") &&
              has(handed, R"("good":{"normal":{"review":{"scheduledDays":10,)"),
          "an interval changed", handed);
    check(run("if (customData.easy.seed === 2104 && ctx.deckName === 'hello' &&"
              "    states.again.normal.relearning.learning.remainingSteps === 1)"
              "  states.again.normal = { review: states.again.normal.relearning.review };") &&
              has(handed, R"("again":{"normal":{"review":{"scheduledDays":1,"elapsedDays":0,"easeFactor":1.649999976158142,"lapses":5,)"),
          "the code reads states, custom data and context; a state's kind changed", handed);
    check(run(MANUAL_EXAMPLE) && has(handed, R"("easy":{"normal":{"review":{"scheduledDays":6,"elapsedDays":0,"easeFactor":2.05)") &&
              has(logged, "\"easeFactor\": 1.850000023841858"),
          "the manual's example on a review card (and its console output)", handed + "\n" + logged);

    // What protobuf-es reads, and what it refuses (the states then stay).
    check(run("states.good.normal.review = { scheduled_days: '3', easeFactor: 'NaN', memoryState: null };") &&
              has(handed, R"("good":{"normal":{"review":{"scheduledDays":3,"elapsedDays":0,"easeFactor":"NaN","lapses":0,"leeched":false}},)"),
          "proto names, numbers as strings, NaN, null for an unset message", handed);
    for (const char* refused : {
             "states.good.normal.review.scheduledDays = 1.5;",
             "states.good.normal.review.scheduledDays = -1;",
             "states.good.normal.review.scheduledDays = undefined;",
             "states.good.normal.review.scheduledDays = NaN;",
             "states.good.normal.review.scheduledDays = ' 4';",
             "states.good.normal.review.leeched = 0;",
             "states.good.normal.review.easeFactor = 1e39;",
             "states.good.normal.review.memoryState = undefined;",
             "states.good.normal.learning = {};",
             "states.good.normal = null;",
             "states.good.bogus = 1;",
             "delete states.again;",
             "customData.good = 1n;",
             "throw new Error('boom');",
         })
        check(run(refused) && handed.empty() && has(logged, "custom scheduling: "),
              (std::string("refused, states unchanged: ") + refused).c_str(), handed + logged);
    check(!run("states.good = {") && handed.empty(), "a syntax error: the code doesn't start");

    // The context as a protobuf-es message has it.
    states = NEW_STATES;
    check(run("console.log([ctx.deckName, typeof ctx.seed, ctx.seed === 18446744073709551615n, ctx.decay,"
              "             ctx.desiredRetention, JSON.stringify(ctx)].join('|'));") &&
              has(logged, R"(Spanish::Verbs|bigint|true||0.8999999761581421|{"deckName":"Spanish::Verbs","seed":"18446744073709551615","desiredRetention":0.8999999761581421})"),
          "context: deck name, seed as a BigInt, desired retention, JSON", logged);
    check(run(MANUAL_EXAMPLE) &&
              has(handed, R"("hard":{"normal":{"learning":{"remainingSteps":2,"scheduledSecs":7380,"elapsedSecs":0}},"customData":"{}"})") &&
              has(handed, R"("easy":{"normal":{"review":{"scheduledDays":4,"elapsedDays":0,"easeFactor":2.5,)"),
          "the manual's example on a new card: Hard becomes a 123-minute step", handed);

    // Code that waits: the states come when it is done.
    check(!run("await new Promise(r => setTimeout(r, 50)); states.good.normal.learning.scheduledSecs = 900;") && handed.empty(),
          "code waiting for a timer hasn't handed states back yet");
    runFor(s, 300);
    bool finished = false;
    for (const auto& c : s.takeCommands())
        finished = finished || c == "statesMutated";
    check(finished && has(handed, R"("good":{"normal":{"learning":{"remainingSteps":1,"scheduledSecs":900,)"),
          "then it does, after the timer", handed);

    // The question put on the page as it is, for code that reads it: none of it runs.
    s.place("<div id=\"deck\" deck_name=\"Verbs\"></div><script>globalThis.ran = 1</script>"
            "<img src=\"missing.png\" onerror=\"globalThis.ran = 2\"><b onclick=\"globalThis.ran = 3\">x</b>",
            "card card2");
    check(run("console.log('ran:' + typeof ran + ':' + document.getElementById('deck').getAttribute('deck_name') + ':' +"
              "            document.body.className);") &&
              has(logged, "ran:undefined:Verbs:card card2"),
          "the code reads the question placed on its page, whose scripts don't run", logged);

    // Templates may call it too: with their own key, which the reviewer ignores.
    logged.clear();
    s.eval("anki.mutateNextCardStates('mine', async states => console.log('peek:' + states.current.normal.new.position));");
    check(handedKey == "mine" && has(logged, "peek:3"), "a template's own call reads the states, hands back its own key",
          handedKey + logged);
    states.clear();
    check(run("") && handed.empty() && has(logged, "no card is being studied"), "no states: nothing to run on", logged);
}

} // namespace

// The SVG a serialised <canvas> (an <img>) shows, decoded.
std::string canvasSvg(const std::string& html, const std::string& id)
{
    size_t at = html.find("<img id=\"" + id + "\"");
    size_t src = at == std::string::npos ? at : html.find("src=\"data:image/svg+xml,", at);
    if (src == std::string::npos)
        return "";
    src += std::strlen("src=\"data:image/svg+xml,");
    std::string encoded = html.substr(src, html.find('"', src) - src), svg;
    for (size_t i = 0; i < encoded.size(); i++)
    {
        if (encoded[i] == '%' && i + 2 < encoded.size())
        {
            svg += (char)std::stoi(encoded.substr(i + 1, 2), nullptr, 16);
            i += 2;
        }
        else
            svg += encoded[i];
    }
    return svg;
}

void typing()
{
    CardScript s(runtime, media);
    std::string html = show(s, "<form id=\"f\" onsubmit=\"event.preventDefault(); log.push('submit')\">"
        "<input id=\"a\" placeholder=\"Capital?\" maxlength=\"20\"><button>Check</button></form>"
        "<textarea id=\"t\"></textarea><input id=\"c\" type=\"checkbox\"><div id=\"out\"></div><script>"
        "var log = []; var a = document.getElementById('a');"
        "['input', 'change', 'keydown', 'keyup'].forEach(function (t) { a.addEventListener(t, function (e) { log.push(t + (e.key ? ':' + e.key : '')); }); });"
        "document.getElementById('t').addEventListener('change', function () { log.push('t:' + this.value); });"
        "</script>");
    s.takeCommands();
    bool handled = s.tap(serialOf(html, "id=\"a\""), 1, 1);
    std::string commands;
    for (const auto& c : s.takeCommands())
        commands += c + ";";
    check(handled && has(commands, "keyboard:{\"serial\":" + std::to_string(serialOf(html, "id=\"a\""))) &&
              has(commands, "\"value\":\"\",\"multiline\":false,\"prompt\":\"Capital?\",\"maxLength\":20}"),
          "a tap on a template's text field asks for the keyboard", commands);
    s.typed(serialOf(html, "id=\"a\""), "Paris");
    html = s.html();
    s.eval("document.getElementById('out').textContent = log.join(' ')");
    check(has(s.html(), "id=\"a\" placeholder=\"Capital?\" maxlength=\"20\" value=\"Paris\"") &&
              innerOf(s.html(), "id=\"out\"") == "input change keydown:Enter keyup:Enter submit",
          "typed text fills the field: input, change, Enter, form submitted", s.html());
    s.tap(serialOf(html, "id=\"t\""), 1, 1);
    commands.clear();
    for (const auto& c : s.takeCommands())
        commands += c + ";";
    s.typed(serialOf(html, "id=\"t\""), "two\nlines");
    s.eval("document.getElementById('out').textContent = log.join(' ')");
    check(has(commands, "\"multiline\":true") && has(innerOf(s.html(), "id=\"out\""), "t:two\nlines") &&
              has(s.html(), ">two\nlines</textarea>"),
          "textareas take several lines, without Enter", s.html());
    s.tap(serialOf(html, "id=\"c\""), 1, 1);
    commands.clear();
    for (const auto& c : s.takeCommands())
        commands += c + ";";
    check(!has(commands, "keyboard:"), "checkboxes don't ask for the keyboard", commands);
}

void canvas()
{
    CardScript s(runtime, media);
    std::string html = show(s, "<canvas id=\"cv\" width=\"120\" height=\"60\"></canvas><div id=\"out\"></div><script>"
        "var c = document.getElementById('cv'), g = c.getContext('2d');"
        "g.fillStyle = 'rgba(67, 160, 71, 0.5)'; g.fillRect(0, 0, 120, 60);"
        "g.beginPath(); g.arc(60, 30, 20, 0, Math.PI * 2); g.strokeStyle = '#d32f2f'; g.lineWidth = 3; g.stroke();"
        "g.font = 'bold 16px Arial'; g.textAlign = 'center'; g.fillText('Hi <3', 60, 35);"
        "document.getElementById('out').textContent = c.width + 'x' + c.height + ' ' + (c.getContext('webgl') === null) +"
        " ' ' + (c.getContext('2d') === g);</script>");
    std::string svg = canvasSvg(html, "cv");
    check(has(html, "width=\"120\" height=\"60\" src=\"data:image/svg+xml,") && innerOf(html, "id=\"out\"") == "120x60 true true",
          "a canvas is serialised as an image of its drawing, its size from the attributes", html);
    check(has(svg, "<path d=\"M0 0L120 0L120 60L0 60Z\" fill=\"rgb(67,160,71)\" fill-opacity=\"0.5\"/>"),
          "fillRect with an rgba() colour", svg);
    check(has(svg, "fill=\"none\" stroke=\"#d32f2f\" stroke-width=\"3\"") && has(svg, "M80 30C"), "arc and stroke", svg);
    check(has(svg, "font-family=\"sans-serif\" font-size=\"16\" font-weight=\"bold\" text-anchor=\"middle\"") &&
              has(svg, ">Hi &lt;3</text>"),
          "fillText with font and alignment", svg);

    s.eval("g.save(); g.translate(10, 20); g.scale(2, 2); g.fillStyle = 'blue'; g.fillRect(0, 0, 5, 5); g.restore();"
           "g.fillRect(1, 1, 1, 1);");
    svg = canvasSvg(s.html(), "cv");
    check(has(svg, "<path d=\"M10 20L20 20L20 30L10 30Z\" fill=\"blue\"/>") && has(svg, "M1 1L2 1L2 2L1 2Z\" fill=\"rgb(67,160,71)\""),
          "transforms, and save/restore of styles", svg);

    s.eval("g.clearRect(0, 0, 60, 60);");
    svg = canvasSvg(s.html(), "cv");
    check(has(svg, "<mask id=") && has(svg, "<g mask=\"url(#"), "clearRect of a part erases through a mask", svg);
    s.eval("g.clearRect(0, 0, c.width, c.height);");
    svg = canvasSvg(s.html(), "cv");
    check(!has(svg, "<path") && !has(svg, "<g"), "clearRect of the whole canvas empties it", svg);

    s.eval("var img = document.createElement('img'); img.src = '_dot.png'; g.drawImage(img, 10, 10);"
           "var grad = g.createLinearGradient(0, 0, 100, 0); grad.addColorStop(0, 'red'); grad.addColorStop(1, 'hsl(240, 100%, 50%)');"
           "g.fillStyle = grad; g.fillRect(0, 0, 10, 10); document.getElementById('out').textContent = img.naturalWidth + 'x' + img.naturalHeight;"
           "c.width = 200;");
    html = s.html();
    svg  = canvasSvg(html, "cv");
    check(innerOf(html, "id=\"out\"") == "7x5", "images report their natural size", html);
    check(has(html, "width=\"200\" height=\"60\"") && !has(svg, "<path"), "setting the width resizes and clears the canvas", svg);
    s.eval("g.drawImage(img, 10, 10); g.fillStyle = grad; g.fillRect(0, 0, 10, 10);");
    svg = canvasSvg(s.html(), "cv");
    check(has(svg, "<image width=\"7\" height=\"5\"") && has(svg, "href=\"data:image/png;base64,") &&
              has(svg, "<linearGradient id=") && has(svg, "stop-color=\"rgb(0,0,255)\""),
          "drawImage from the media folder and a linear gradient", svg);
    check(s.lastError().empty(), "no errors drawing on a canvas", s.lastError());
}

int main(int argc, char** argv)
{
    runtime = readFile(argc > 1 ? argv[1] : "resources/script/runtime.js");
    if (runtime.empty())
    {
        std::printf("FAIL cannot read runtime.js\n");
        return 1;
    }
    CardScript::log = [](int level, const std::string& msg) {
        logged += msg + "\n";
        if (level > 0)
            std::printf("     log[%d]: %s\n", level, msg.substr(0, 600).c_str());
    };
    auto dir = std::filesystem::temp_directory_path() / "ankinx_script_test";
    std::filesystem::create_directories(dir);
    media = dir.string();
    writeFile(media + "/_lib.js", "function libAnswer() { return 42; }");
    writeFile(media + "/_data.json", "{\"x\": \"from media\"}");
    writeFile(media + "/_mod.js", "export const m = 'module';");
    writeFile(media + "/_style.css", ".from-media { color: blue }");
    // A PNG header only: 7 x 5 (enough for natural sizes).
    writeFile(media + "/_dot.png", std::string("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR\0\0\0\x07\0\0\0\x05\x08\x06\0\0\0", 33));

    basics();
    dom();
    jquery();
    scripts();
    forms();
    layout();
    mcq();
    canvas();
    typing();
    timing();
    scheduling();

    std::printf("%s\n", failures ? "SCRIPT TESTS FAILED" : "all script tests passed");
    return failures ? 1 : 0;
}
