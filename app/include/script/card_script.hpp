#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// JavaScript in card templates. QuickJS runs a browser page made for cards
// (resources/script/runtime.js, built from app/js): linkedom's DOM with
// events, forms and script loading as browsers have them, the real jQuery,
// and Anki's reviewer page, into which each side goes as in Anki desktop.
// The reviewer draws the page's body with litehtml, which also answers the
// page's layout questions (Host::layout).
//
// One instance per card: globals set on the question side are still there
// on the answer side, as in Anki desktop. Each entry into the page has a
// time and memory budget; errors are logged and never stop studying. See
// docs/full-js-plan.md and app/js/README.md.
class CardScript
{
  public:
    // A laid-out element, in CSS pixels and page coordinates: the border box,
    // the client (padding) box, and computed values by CSS property name.
    struct Box
    {
        float x = 0, y = 0, width = 0, height = 0;
        float clientLeft = 0, clientTop = 0, clientWidth = 0, clientHeight = 0;
        std::vector<std::pair<std::string, std::string>> style;
    };
    using Layout = std::unordered_map<int, Box>; // by data-ankinx-node serial

    struct Viewport
    {
        float width = 1280, height = 720, scrollY = 0, pixelRatio = 1;
    };

    // What the reviewer gives the page; each part is optional (host tests
    // run without them: layout questions then get empty boxes).
    struct Host
    {
        // Lay out the page: its body HTML, body class and CSS added to <head>.
        std::function<Layout(const std::string& body, const std::string& bodyClass, const std::string& css)> layout;
        std::function<Viewport()> viewport;
        std::function<void(float y)> scrollTo;
        // Custom scheduling (anki.mutateNextCardStates): the shown card's
        // scheduling states and context as JSON (next_card's "scheduling"),
        // or "" when there is none; and states the page hands back, with the
        // key it was given (mutateStates).
        std::function<std::string()> schedulingStates;
        std::function<void(const std::string& key, const std::string& states)> setSchedulingStates;
    };

    // `runtime`: resources/script/runtime.js. `mediaDir`: the collection's
    // media folder, which the page sees as its web server.
    CardScript(const std::string& runtime, std::string mediaDir, Host host = {});
    ~CardScript();

    // Worth running? (The side has <script> or on... handler attributes.)
    static bool wanted(const std::string& html);

    // Show a side, as Anki's _showQuestion / _showAnswer: its scripts run,
    // then what they start at once (promises, zero-delay timers, script
    // files), so the first drawing shows their result.
    void show(bool answer, const std::string& html, const std::string& bodyClass);
    // Put a side on the page as it is, running nothing in it (for code that
    // only reads it: custom scheduling).
    void place(const std::string& html, const std::string& bodyClass);
    // Custom scheduling, as Anki's reviewer runs it on each question: the
    // collection's code (deck options > Advanced) on the card's scheduling
    // states, which it gets and hands back through the Host with `key`.
    // False if the code could not start (a syntax error): the states stay.
    bool mutateStates(const std::string& key, const std::string& code);
    // The page's body as HTML for the card view, every element tagged
    // data-ankinx-node (and data-ankinx-click if it has click handlers).
    std::string html();
    // The body changed since the last html().
    bool dirty();
    // The body's class (scripts may change it) and CSS scripts added to <head>.
    std::string bodyClass();
    std::string extraCss();

    // A tap at (x, y), CSS pixels in page coordinates, on the element with
    // this data-ankinx-node: the events of a touchscreen tap, then a click.
    // True if the card did something with it (else the tap flips the card).
    bool tap(int serial, float x, float y);
    // Text typed on the keyboard into the template's field with this
    // serial (after a "keyboard:" command): its value, input, change, Enter.
    void typed(int serial, const std::string& text);
    // A key pressed (keydown, keyup) on the focused element.
    bool key(const std::string& key, const std::string& code, int keyCode);
    // Run code in the page (the app's own changes, e.g. hints).
    void eval(const std::string& code);

    // Milliseconds until the page's next timer, or -1 if there is none;
    // call runTimers() then.
    int nextTimer();
    void runTimers();

    // Commands the page sent the reviewer since the last call: "ans",
    // "ease1".."ease4" (Anki's pycmd), "link:<url>", "alert:<text>",
    // "play:<file>", "stop".
    std::vector<std::string> takeCommands();

    // Last error message, for the log and diagnostics.
    const std::string& lastError() const;

    // Where page messages and errors go (level 0 info, 1 warning, 2 error).
    // The app points this at its logger; the default prints to stderr.
    static std::function<void(int, const std::string&)> log;

    // Native side, used by the bindings in card_script.cpp.
    struct Native;

  private:
    void settle();
    std::string serialize();

    std::unique_ptr<Native> native;
    std::string rendered; // the body as last returned by html()
    std::string pending;  // serialised by dirty(), for html()
    bool pendingValid = false;
};
