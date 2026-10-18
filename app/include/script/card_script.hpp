#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "script/dom.hpp"

struct JSRuntime;
struct JSContext;

// JavaScript in card templates, without a browser: QuickJS runs the card's
// scripts against a small DOM (script/dom), and the reviewer shows the
// resulting HTML. One instance per card: like Anki desktop, which keeps the
// page between the question and the answer, globals set on the question
// side are still there on the answer side (randomised content stays in
// step). Scripts get a time and memory budget; errors are logged and never
// stop studying. See app/ROADMAP.md, "JavaScript evaluation".
class CardScript
{
  public:
    // `prelude`: resources/script/prelude.js. `mediaDir`: for <script src>.
    CardScript(const std::string& prelude, std::string mediaDir);
    ~CardScript();

    // Worth running? (The side has <script> or on... handler attributes.)
    static bool wanted(const std::string& html);

    // Show a card side: build its DOM, run its scripts and the load hooks.
    void show(const std::string& html, const std::string& bodyClass);
    // The current HTML of the side (inside <body>), elements tagged with
    // data-ankinx-node for tap handling. Clears `dirty`.
    std::string html();
    // The DOM changed since html().
    bool dirty() const;

    // A tap on the element with this data-ankinx-node: dispatch "click".
    // True if a handler ran.
    bool click(int serial);
    // Run code in the card's context (the app's own changes, e.g. hints).
    void eval(const std::string& code);

    // Timers scripts started since the last call: (id, delay in ms).
    std::vector<std::pair<int, int>> takeTimers();
    void fireTimer(int id);
    // Commands scripts sent to the reviewer since the last call, as Anki
    // desktop's pycmd(): "ans" (show the answer), "ease1".."ease4".
    std::vector<std::string> takeCommands();

    // Last error message, for the log and diagnostics.
    const std::string& lastError() const { return error; }

    // Where script messages and errors go (level 0 info, 1 warning, 2 error).
    // The app points this at its logger; the default prints to stderr.
    static std::function<void(int, const std::string&)> log;

    // Native side, used by the bindings in card_script.cpp.
    struct Native;

  private:
    void run(const char* what, const std::string& code);
    void runJobs();

    std::unique_ptr<Native> native;
    std::string error;
};
