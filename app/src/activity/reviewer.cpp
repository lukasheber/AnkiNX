#include "activity/reviewer.hpp"

#include "app.hpp"
#include "util/audio.hpp"
#include "util/controls.hpp"
#include "util/inline_svg.hpp"
#include "util/keyboard.hpp"
#include "util/ruby.hpp"
#include "util/settings.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include <sys/stat.h>

using namespace brls::literals;

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

std::string escapeHtml(const std::string& text)
{
    std::string out;
    for (char c : text)
    {
        switch (c)
        {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// Night mode, applied after the note type's CSS (whose .card rule usually sets
// black on white); `.card.nightMode` is more specific than `.card`.
const char* NIGHT_CSS = R"CSS(
.card.nightMode { color: #e6e6e6; background-color: #2c2c2c; }
.nightMode a { color: #8ab4f8; }
.nightMode button { background-color: #3a3a3a; color: #e6e6e6; border-color: #666666; }
)CSS";

// Ease button glyphs from the Switch's shared symbol font (as in borealis hints).
const char* EASE_KEYS[4]   = { "ankinx/review/again", "ankinx/review/hard", "ankinx/review/good",
                               "ankinx/review/easy" };

bool nightMode() { return brls::Application::getThemeVariant() == brls::ThemeVariant::DARK; }

// Image Occlusion shapes of one side, from next_card's "occlusions".
std::vector<Occlusion> occlusionsFor(const anki::json& card, const char* side)
{
    std::vector<Occlusion> out;
    const anki::json& all = card["occlusions"];
    if (!all.is_object() || !all[side].is_array())
        return out;
    for (const auto& j : all[side])
    {
        Occlusion s;
        std::string state = j.value("state", "active");
        s.state = state == "inactive" ? Occlusion::State::Inactive
                  : state == "highlight" ? Occlusion::State::Highlight
                                         : Occlusion::State::Active;
        std::string kind = j.value("shape", "rect");
        s.kind = kind == "ellipse" ? Occlusion::Kind::Ellipse
                 : kind == "polygon" ? Occlusion::Kind::Polygon
                 : kind == "text"    ? Occlusion::Kind::Text
                                     : Occlusion::Kind::Rect;
        s.occludeInactive = j.value("occlude_inactive", false);
        s.left     = j.value("left", 0.0f);
        s.top      = j.value("top", 0.0f);
        s.width    = j.value("width", 0.0f);
        s.height   = j.value("height", 0.0f);
        s.rx       = j.value("rx", 0.0f);
        s.ry       = j.value("ry", 0.0f);
        s.angle    = j.value("angle", 0.0f);
        s.text     = j.value("text", "");
        s.scale    = j.value("scale", 1.0f);
        s.fontSize = j.value("font_size", 0.0f);
        s.fill     = j.value("fill", "");
        for (const auto& p : j.value("points", anki::json::array()))
            s.points.emplace_back(p[0].get<float>(), p[1].get<float>());
        out.push_back(std::move(s));
    }
    return out;
}

} // namespace

ReviewerActivity::ReviewerActivity(int64_t deckId, std::string deckName)
    : deckId(deckId)
    , deckName(std::move(deckName))
{
}

ReviewerActivity::~ReviewerActivity()
{
    *alive = false;
    audio::stop();
}

void ReviewerActivity::onUi(std::shared_ptr<bool> token, std::function<void()> f)
{
    brls::sync([token, f] {
        if (*token)
            f();
    });
}

void ReviewerActivity::onContentAvailable()
{
    frame->setTitle(deckName);
    app::registerQuit(frame);
    showAnswerLabel->setText(std::string("  ") + "ankinx/review/show_answer"_i18n);

    for (int ease = 1; ease <= 4; ease++)
    {
        auto* box = (brls::Box*)this->getView("ease" + std::to_string(ease));
        box->addGestureRecognizer(new brls::TapGestureRecognizer(box, [this, ease] { answer(ease); }));
    }
    showAnswerBox->addGestureRecognizer(new brls::TapGestureRecognizer(showAnswerBox, [this] { showAnswer(); }));

    cardView->setOnTap([this] {
        if (state == State::Question)
            showAnswer();
    });
    // Taps on card elements go to card scripts first (onclick etc.).
    cardView->setOnElementTap([this](int serial) {
        if (!script)
            return false;
        bool handled = script->click(serial);
        afterScript();
        return handled;
    });
    cardView->setOnLink([this](const std::string& url) {
        if (url.rfind("anki-hint:", 0) == 0)
            return revealHint(url.substr(10));
        if (url == "anki-typeans:")
            return typeAnswer();
        // "playsound:q:0" -> the first audio tag of the question side.
        if (url.rfind("playsound:", 0) != 0)
            return;
        std::string side = url.substr(10, 1);
        int index        = std::atoi(url.c_str() + 12);
        const anki::json& tags = card[side == "q" ? "question_av" : "answer_av"];
        if (index >= 0 && index < (int)tags.size())
            playAudio(anki::json::array({ tags[index] }));
    });

    // Buttons follow the bindings of the controller layout in use
    // (util/controls); see onButton and refreshControls.
    refreshControls();

    // Media folder for images and audio, then the first card.
    brls::async([this, token = alive] {
        try
        {
            anki::json status = anki::call("status");
            std::string dir   = status["media_dir"].get<std::string>();
            anki::call("select_deck", { { "deck_id", deckId } });
            onUi(token, [this, dir] {
                mediaDir = dir;
                cardView->setMediaDir(dir);
                loadNext();
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            onUi(token, [this, text] { showError(text); });
        }
    });
}

void ReviewerActivity::loadNext()
{
    state = State::Loading;
    audio::stop();
    bool night = nightMode();
    brls::async([this, night, token = alive] {
        try
        {
            anki::json next = anki::call("next_card", { { "night_mode", night } });
            onUi(token, [this, next] {
                card = next;
                if (card["done"].get<bool>())
                    showDone();
                else
                    showQuestion();
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            onUi(token, [this, text] { showError(text); });
        }
    });
}

std::string ReviewerActivity::documentFor(const std::string& body) const
{
    std::string html = "<html><head><style>";
    html += BASE_CSS;
    html += "</style><style>";
    html += card.value("css", "");
    html += "</style>";
    if (nightMode())
        html += std::string("<style>") + NIGHT_CSS + "</style>";
    html += "</head><body class=\"" + card.value("body_class", "card") + "\">";
    html += layoutRuby(inlineSvgToImages(body, anki::dataDir() + "/cache/svg"));
    html += "</body></html>";
    return html;
}

// Controller path for hint fields: reveal every hint on the current side.
void ReviewerActivity::revealAllHints()
{
    const std::string marker = "href=\"anki-hint:";
    std::vector<std::string> ids;
    for (size_t at = shownBody.find(marker); at != std::string::npos; at = shownBody.find(marker, at + 1))
    {
        size_t start = at + marker.size();
        size_t end   = shownBody.find('"', start);
        if (end != std::string::npos)
            ids.push_back(shownBody.substr(start, end - start));
    }
    for (const auto& id : ids)
        revealHint(id);
}

// What Anki's hint-field onclick script does: hide the link, show the div.
void ReviewerActivity::revealHint(const std::string& id)
{
    if (script)
    {
        // The card's DOM is the source of truth while scripts run.
        script->eval("(function(){var l=document.querySelector('a[href=\"anki-hint:" + id +
                     "\"]');if(l)l.style.display='none';var d=document.getElementById('" + id +
                     "');if(d)d.style.display='block';})()");
        afterScript();
        return;
    }
    auto replace = [this](const std::string& from, const std::string& to) {
        size_t at = shownBody.find(from);
        if (at != std::string::npos)
            shownBody.replace(at, from.size(), to);
    };
    replace("href=\"anki-hint:" + id + "\"", "href=\"anki-hint:" + id + "\" style=\"display: none\"");
    replace("id=\"" + id + "\" class=hint style=\"display: none\"", "id=\"" + id + "\" class=hint");
    cardView->setHtml(documentFor(shownBody), true);
}

void ReviewerActivity::showQuestion()
{
    state = State::Question;
    typed.clear();
    updateCounts(card["counts"], card.value("queue", ""));
    shownBody = questionBody();
    startScripts();
    runScripts();
    cardView->setOcclusions(occlusionsFor(card, "question"));
    cardView->setMasksHidden(false);
    cardView->setHtml(documentFor(shownBody));
    setBar(false);
    playAudio(card["question_av"]);
}

// A new card: a script context if either side has scripts (and they're on in
// Settings > Study). One context per card, as Anki keeps the page between
// question and answer.
void ReviewerActivity::startScripts()
{
    script.reset();
    scriptGeneration++;
    std::string sides = card.value("question", "") + card.value("answer", "");
    if (!settings::cardScripts() || !CardScript::wanted(sides))
        return;
    static const std::string prelude = [] {
        std::ifstream in(BRLS_ASSET("script/prelude.js"), std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }();
    script = std::make_unique<CardScript>(prelude, mediaDir);
}

// Run the side in shownBody through the card's scripts.
void ReviewerActivity::runScripts()
{
    if (!script)
        return;
    script->show(shownBody, card.value("body_class", "card"));
    shownBody = script->html();
    afterScript();
}

// After scripts ran (a tap, a timer, a hint): show their changes.
void ReviewerActivity::afterScript()
{
    if (!script)
        return;
    if (script->dirty())
    {
        shownBody = script->html();
        cardView->setHtml(documentFor(shownBody), true);
    }
    scheduleTimers();
    // pycmd("ans") etc. Next frame: this can run inside the card view's tap
    // handling, and showing the answer replaces its document.
    for (const std::string& command : script->takeCommands())
        brls::sync([this, token = alive, generation = scriptGeneration, command] {
            if (!*token || generation != scriptGeneration)
                return;
            if (command == "ans" && state == State::Question)
                showAnswer();
            else if (command.rfind("ease", 0) == 0 && state == State::Answer)
            {
                int ease = std::atoi(command.c_str() + 4);
                if (ease >= 1 && ease <= 4)
                    answer(ease);
            }
        });
}

void ReviewerActivity::scheduleTimers()
{
    for (auto [id, ms] : script->takeTimers())
        brls::delay(ms, [this, token = alive, generation = scriptGeneration, id = id] {
            // Ignore timers of a card that has gone.
            if (!*token || !script || generation != scriptGeneration)
                return;
            script->fireTimer(id);
            afterScript();
        });
}

// The question, with the answer box (type-answer cards) showing what was typed.
std::string ReviewerActivity::questionBody() const
{
    std::string body   = card["question"].get<std::string>();
    std::string marker = card.value("typed_marker", "");
    size_t at          = marker.empty() ? std::string::npos : body.find(marker);
    if (at != std::string::npos)
        body.replace(at, marker.size(),
                     typed.empty() ? "<span class=\"typeans-prompt\">" + escapeHtml("ankinx/review/type_answer"_i18n) +
                                         "</span>"
                                   : escapeHtml(typed));
    return body;
}

// Type the answer with the system keyboard (Anki's answer box). Confirming
// shows the answer, compared with what was typed, as Enter does in Anki.
void ReviewerActivity::typeAnswer()
{
    if (state != State::Question || !card.value("type_answer", false))
        return;
    keyboard::text("ankinx/review/your_answer"_i18n, typed, 500, [this, token = alive](const std::string& text) {
        if (!*token || state != State::Question)
            return;
        typed = text;
        showAnswer();
    });
}

void ReviewerActivity::showAnswer()
{
    if (state != State::Question)
        return;
    state     = State::Answer;
    shownBody = card["answer"].get<std::string>();
    if (card.value("type_answer", false) && !typed.empty())
    {
        try
        {
            shownBody = anki::call("type_answer", { { "typed", typed } })["answer"].get<std::string>();
        }
        catch (const anki::Error& e)
        {
            brls::Application::notify(e.what());
        }
    }
    runScripts();
    cardView->setOcclusions(occlusionsFor(card, "answer"));
    cardView->setMasksHidden(false);
    cardView->setHtml(documentFor(shownBody));
    cardView->scrollToAnswerAfterLayout();
    const anki::json& buttons = card["buttons"];
    for (int ease = 1; ease <= 4 && ease <= (int)buttons.size(); ease++)
        ((brls::Label*)this->getView("ease" + std::to_string(ease) + "Time"))
            ->setText(buttons[ease - 1].get<std::string>());
    setBar(true);
    playAudio(card["answer_av"]);
}

void ReviewerActivity::answer(int ease)
{
    if (state != State::Answer)
        return;
    state = State::Loading;
    audio::stop();
    brls::async([this, ease, token = alive] {
        try
        {
            anki::call("answer", { { "ease", ease } });
            onUi(token, [this] { loadNext(); });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            onUi(token, [this, text] { showError(text); });
        }
    });
}

void ReviewerActivity::undo()
{
    if (state == State::Loading)
        return;
    state = State::Loading;
    brls::async([this, token = alive] {
        try
        {
            bool undone = anki::call("undo")["undone"].get<bool>();
            onUi(token, [this, undone] {
                brls::Application::notify(undone ? "ankinx/review/undone"_i18n
                                                 : "ankinx/review/nothing_to_undo"_i18n);
                loadNext();
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            onUi(token, [this, text] { showError(text); });
        }
    });
}

void ReviewerActivity::replay()
{
    if (state == State::Question)
        playAudio(card["question_av"]);
    else if (state == State::Answer)
        playAudio(card["answer_av"]);
}

void ReviewerActivity::playAudio(const anki::json& tags)
{
    std::vector<std::string> paths;
    std::string missing;
    for (const auto& tag : tags)
    {
        if (tag.value("kind", "") != "sound") // TTS tags have no audio file
            continue;
        std::string file = tag["file"].get<std::string>();
        std::string path = mediaDir + "/" + file;
        struct stat st;
        if (stat(path.c_str(), &st) == 0)
            paths.push_back(path);
        else if (missing.empty())
            missing = file;
    }
    // As AnkiDroid: say so instead of staying silent (not synced yet, or
    // missing from the collection).
    if (!missing.empty())
        brls::Application::notify("ankinx/review/missing_media"_i18n + ": " + missing);
    if (!paths.empty())
        audio::play(paths);
}

void ReviewerActivity::showDone()
{
    state = State::Done;
    updateCounts(card["counts"], "");
    std::string body = "<div style=\"text-align:center; margin-top:120px; font-size:26px\">" +
                       "ankinx/review/congrats"_i18n + "</div>";
    card["css"]        = "";
    card["body_class"] = nightMode() ? "card nightMode" : "card";
    cardView->setHtml(documentFor(body));
    showAnswerBox->setVisibility(brls::Visibility::GONE);
    easeButtons->setVisibility(brls::Visibility::GONE);
    refreshControls();
}

void ReviewerActivity::showError(const std::string& message)
{
    state = State::Failed;
    showAnswerBox->setVisibility(brls::Visibility::GONE);
    easeButtons->setVisibility(brls::Visibility::GONE);
    auto* dialog = new brls::Dialog("ankinx/review/error"_i18n + "\n\n" + message);
    dialog->addButton("hints/back"_i18n, [] { brls::Application::popActivity(); });
    dialog->setCancelable(false);
    dialog->open();
}

void ReviewerActivity::updateCounts(const anki::json& counts, const std::string& queue)
{
    struct
    {
        brls::Label* label;
        const char* key;
        const char* queueName;
    } items[3] = { { countNew, "new", "New" }, { countLearn, "learn", "Learning" },
                   { countReview, "review", "Review" } };
    for (auto& item : items)
    {
        item.label->setText(std::to_string(counts.value(item.key, 0)));
        bool current = queue == item.queueName;
        item.label->setLineColor(item.label->getTextColor());
        item.label->setLineBottom(current ? 2.0f : 0.0f);
    }
}

void ReviewerActivity::setBar(bool answerSide)
{
    showAnswerBox->setVisibility(answerSide ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    easeButtons->setVisibility(answerSide ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    refreshControls();
}

// What a button does now: the first of its bound actions that applies in the
// current state, in this order. A button can carry several actions (one
// Joy-Con: X is hints on the question side, undo on the answer side).
controls::Action ReviewerActivity::actionFor(brls::ControllerButton button) const
{
    using controls::Action;
    std::vector<Action> bound = controls::actionsFor(controls::activeLayout(), button);
    auto has = [&](Action a) { return std::find(bound.begin(), bound.end(), a) != bound.end(); };
    std::vector<Action> order;
    if (state == State::Question)
        order = { Action::ShowAnswer, Action::Hints, Action::ReplayAudio, Action::Undo };
    else if (state == State::Answer)
        order = { Action::Again, Action::Hard, Action::Good, Action::Easy, Action::ShowAnswer,
                  Action::Undo,  Action::Hints, Action::ReplayAudio };
    else
        order = { Action::ShowAnswer, Action::Undo };
    order.push_back(Action::ZoomOut);
    order.push_back(Action::ZoomIn);
    for (Action a : order)
        if (has(a))
            return a;
    return Action::_Count;
}

bool ReviewerActivity::onButton(brls::ControllerButton button)
{
    using controls::Action;
    switch (actionFor(button))
    {
        case Action::ShowAnswer:
            if (state == State::Question && card.value("type_answer", false))
                typeAnswer();
            else if (state == State::Question)
                showAnswer();
            else if (state == State::Answer)
                answer(3);
            else if (state == State::Done || state == State::Failed)
                brls::Application::popActivity();
            break;
        case Action::Again: answer(1); break;
        case Action::Hard: answer(2); break;
        case Action::Good: answer(3); break;
        case Action::Easy: answer(4); break;
        case Action::Undo: undo(); break;
        case Action::ReplayAudio: replay(); break;
        case Action::Hints:
            if (masksToggle())
                cardView->setMasksHidden(!cardView->getMasksHidden());
            else
                revealAllHints();
            break;
        case Action::ZoomOut: cardView->setZoom(cardView->getZoom() - 0.1f); break;
        case Action::ZoomIn: cardView->setZoom(cardView->getZoom() + 0.1f); break;
        default: break;
    }
    return true;
}

// Image Occlusion answers: X is Anki's "Toggle masks" button.
bool ReviewerActivity::masksToggle() const
{
    const anki::json& io = card["occlusions"];
    return state == State::Answer && io.is_object() && io.value("can_toggle", false);
}

// Deferred to the next frame: this is often reached from inside an action
// (A shows the answer), and the actions can't be replaced while borealis is
// dispatching one of them.
void ReviewerActivity::refreshControls()
{
    if (refreshPending)
        return;
    refreshPending = true;
    brls::sync([this, token = alive] {
        if (!*token)
            return;
        refreshPending = false;
        applyControls();
    });
}

// Register every bindable button, with footer hints for what it does in the
// current state and layout, and label the ease buttons with their buttons.
void ReviewerActivity::applyControls()
{
    using controls::Action;
    for (brls::ActionIdentifier id : buttonActions)
        frame->unregisterAction(id);
    buttonActions.clear();
    for (brls::ControllerButton button : controls::bindableButtons())
    {
        std::string hint;
        switch (actionFor(button))
        {
            case Action::ShowAnswer: hint = "hints/ok"_i18n; break;
            case Action::Undo: hint = "ankinx/review/undo"_i18n; break;
            case Action::ReplayAudio: hint = "ankinx/review/replay"_i18n; break;
            case Action::Hints:
                hint = masksToggle() ? "ankinx/review/toggle_masks"_i18n : "ankinx/review/hints"_i18n;
                break;
            default: break; // grading is on the ease buttons; zoom needs no hint
        }
        bool zoom = actionFor(button) == Action::ZoomOut || actionFor(button) == Action::ZoomIn;
        buttonActions.push_back(frame->registerAction(
            hint, button, [this, button](brls::View*) { return onButton(button); }, hint.empty(), zoom));
    }
    brls::Application::getGlobalHintsUpdateEvent()->fire();

    controls::Layout layout = controls::activeLayout();
    const Action eases[4]   = { Action::Again, Action::Hard, Action::Good, Action::Easy };
    for (int ease = 1; ease <= 4; ease++)
    {
        auto* name = (brls::Label*)this->getView("ease" + std::to_string(ease) + "Name");
        name->setText(controls::glyph(controls::binding(layout, eases[ease - 1])) + "  " +
                      brls::getStr(EASE_KEYS[ease - 1]));
    }
}
