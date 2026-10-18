#include "activity/reviewer.hpp"

#include "app.hpp"
#include "util/audio.hpp"
#include "util/controls.hpp"
#include "util/inline_svg.hpp"
#include "util/keyboard.hpp"
#include "util/ruby.hpp"
#include "util/settings.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>

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

// Material icons (resources/material): star, more_horiz. (The flag is a
// coloured dot: Material's flag codepoint is taken by the Switch symbol font.)
const char* ICON_STAR = "\xEE\xA0\xB8";
const char* ICON_MORE = "\xEE\x97\x93";

// Anki's flag colours, 1 (red) to 7 (purple).
NVGcolor flagColor(int flag)
{
    static const NVGcolor colors[8] = { nvgRGB(0, 0, 0),       nvgRGB(239, 83, 80),  nvgRGB(255, 167, 38),
                                        nvgRGB(102, 187, 106), nvgRGB(66, 165, 245), nvgRGB(240, 98, 146),
                                        nvgRGB(38, 198, 218),  nvgRGB(171, 71, 188) };
    return colors[std::clamp(flag, 0, 7)];
}

std::string flagName(int flag) { return brls::getStr("ankinx/review/flags/" + std::to_string(std::clamp(flag, 0, 7))); }

// Card info --------------------------------------------------------------

std::string date(int64_t secs)
{
    std::time_t t = (std::time_t)secs;
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", std::localtime(&t));
    return buf;
}

// A time span as Anki writes it in card info: 45s, 12m, 3h, 21d, 2.5mo, 1.2y.
std::string span(double secs)
{
    char buf[32];
    if (secs < 60)
        std::snprintf(buf, sizeof buf, "%.0fs", secs);
    else if (secs < 3600)
        std::snprintf(buf, sizeof buf, "%.0fm", secs / 60);
    else if (secs < 86400)
        std::snprintf(buf, sizeof buf, "%.0fh", secs / 3600);
    else if (secs < 86400 * 30)
        std::snprintf(buf, sizeof buf, "%.0fd", secs / 86400);
    else if (secs < 86400 * 365)
        std::snprintf(buf, sizeof buf, "%.1fmo", secs / (86400 * 30.0));
    else
        std::snprintf(buf, sizeof buf, "%.1fy", secs / (86400 * 365.0));
    return buf;
}

std::string cardInfoHtml(const anki::json& info)
{
    auto t = [](const char* key) { return escapeHtml(brls::getStr(std::string("ankinx/card_info/") + key)); };
    std::string rows;
    auto row = [&](const char* key, const std::string& value) {
        if (!value.empty())
            rows += "<tr><th>" + t(key) + "</th><td>" + escapeHtml(value) + "</td></tr>";
    };
    auto when = [&](const char* field) { return info[field].is_number() ? date(info[field].get<int64_t>()) : ""; };
    row("added", when("added"));
    row("first_review", when("first_review"));
    row("latest_review", when("latest_review"));
    row("due", when("due_date"));
    if (info["due_position"].is_number())
        row("position", std::to_string(info["due_position"].get<int>()));
    if (info.value("interval", 0) > 0)
        row("interval", span(info["interval"].get<double>() * 86400));
    if (info.value("ease", 0) > 0)
        row("ease", std::to_string(info["ease"].get<int>() / 10) + "%");
    char buf[32];
    if (info["stability"].is_number())
    {
        row("stability", span(info["stability"].get<double>() * 86400));
        // FSRS difficulty runs 1-10; Anki shows it as a percentage.
        std::snprintf(buf, sizeof buf, "%.0f%%", (info["difficulty"].get<double>() - 1) / 9 * 100);
        row("difficulty", buf);
    }
    if (info["retrievability"].is_number())
    {
        std::snprintf(buf, sizeof buf, "%.0f%%", info["retrievability"].get<double>() * 100);
        row("retrievability", buf);
    }
    row("reviews", std::to_string(info.value("reviews", 0)));
    row("lapses", std::to_string(info.value("lapses", 0)));
    if (info.value("reviews", 0) > 0)
    {
        row("average_time", span(info.value("average_secs", 0.0)));
        row("total_time", span(info.value("total_secs", 0.0)));
    }
    row("card_type", info.value("card_type", ""));
    row("note_type", info.value("notetype", ""));
    row("deck", info.value("deck", ""));
    row("preset", info.value("preset", ""));
    row("card_id", std::to_string(info.value("card_id", (int64_t)0)));
    row("note_id", std::to_string(info.value("note_id", (int64_t)0)));

    std::string html = "<table class=\"facts\">" + rows + "</table>";
    const anki::json& revlog = info["revlog"];
    if (!revlog.is_array() || revlog.empty())
        return html + "<p>" + t("no_reviews") + "</p>";
    // Newest first, as in Anki.
    html += "<table class=\"revlog\"><tr><th>" + t("date") + "</th><th>" + t("type") + "</th><th>" + t("rating") +
            "</th><th>" + t("interval") + "</th><th>" + t("ease") + "</th><th>" + t("time") + "</th></tr>";
    static const char* RATING_COLORS[5] = { "inherit", "#e53935", "#757575", "#43a047", "#1e88e5" };
    for (const anki::json& r : revlog)
    {
        int kind   = std::clamp(r.value("kind", 0), 0, 5);
        int rating = std::clamp(r.value("rating", 0), 0, 4);
        int ease   = r.value("ease", 0);
        html += "<tr><td>" + date(r.value("time", (int64_t)0)) + "</td><td>" +
                t(("kinds/" + std::to_string(kind)).c_str()) + "</td><td style=\"color:" + RATING_COLORS[rating] +
                "\">" + (rating ? std::to_string(rating) : "") + "</td><td>" +
                (r.value("interval_secs", 0) > 0 ? span(r["interval_secs"].get<double>()) : "") + "</td><td>" +
                (ease > 0 ? std::to_string(ease / 10) + "%" : "") + "</td><td>" +
                span(r.value("taken_secs", 0.0)) + "</td></tr>";
    }
    return html + "</table>";
}

const char* CARD_INFO_CSS = R"CSS(
body { margin: 24px; font-size: 20px; }
table { border-collapse: collapse; margin: 0 auto 24px auto; }
th, td { padding: 6px 14px; text-align: left; }
.facts th { color: #888888; font-weight: normal; }
.revlog th { color: #888888; font-weight: normal; border-bottom: 1px solid #888888; }
.revlog td { border-bottom: 1px solid #dddddd; }
.nightMode .revlog td { border-bottom-color: #444444; }
p { text-align: center; color: #888888; }
)CSS";

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
    markIcon->setText(ICON_STAR);
    menuIcon->setText(ICON_MORE);
    menuButton->addGestureRecognizer(new brls::TapGestureRecognizer(menuButton, [this] {
        if (menuAvailable())
            openCardMenu();
    }));

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
    showCardState();
    playAudio(card["question_av"]);
}

// Flag and mark of the shown card, in the header and as the flag bar.
void ReviewerActivity::showCardState()
{
    bool shown = state == State::Question || state == State::Answer;
    int flag   = shown ? card.value("flag", 0) : 0;
    bool mark  = shown && card.value("marked", false);
    flagIcon->setVisibility(flag ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    flagBar->setVisibility(flag ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
    if (flag)
    {
        flagIcon->setBackgroundColor(flagColor(flag));
        flagBar->setBackgroundColor(flagColor(flag));
    }
    markIcon->setVisibility(mark ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    menuButton->setVisibility(shown ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
}

// Whether replay has something to play on the side shown (sound files; TTS
// tags aren't played).
bool ReviewerActivity::sideHasAudio() const
{
    const char* key = state == State::Answer ? "answer_av" : "question_av";
    if (!card.contains(key) || !card[key].is_array())
        return false;
    for (const anki::json& tag : card[key])
        if (tag.value("kind", "") == "sound")
            return true;
    return false;
}

bool ReviewerActivity::menuAvailable() const
{
    return (state == State::Question || state == State::Answer) && card.contains("card_id");
}

void ReviewerActivity::openCardMenu()
{
    using Item = std::pair<std::string, std::function<void()>>;
    int64_t cardId = card["card_id"].get<int64_t>();
    std::vector<Item> items = {
        { "ankinx/review/flag"_i18n + "  \xE2\x96\xB8", [this] { chooseFlag(); } },
        { card.value("marked", false) ? "ankinx/review/unmark"_i18n : "ankinx/review/mark"_i18n,
          [this] { cardAction("toggle_mark", anki::json::object(), ""); } },
        { "ankinx/review/bury_card"_i18n,
          [this] { cardAction("bury_card", anki::json::object(), "ankinx/review/buried_card"_i18n); } },
        { "ankinx/review/bury_note"_i18n,
          [this] { cardAction("bury_note", anki::json::object(), "ankinx/review/buried_note"_i18n); } },
        { "ankinx/review/suspend_card"_i18n,
          [this] { cardAction("suspend_card", anki::json::object(), "ankinx/review/suspended_card"_i18n); } },
        { "ankinx/review/suspend_note"_i18n,
          [this] { cardAction("suspend_note", anki::json::object(), "ankinx/review/suspended_note"_i18n); } },
        { "ankinx/review/forget"_i18n,
          [this] { cardAction("forget", anki::json::object(), "ankinx/review/forgotten"_i18n); } },
        { "ankinx/review/set_due"_i18n, [this] { setDueDate(); } },
        { "ankinx/review/card_info"_i18n, [this, cardId] { showCardInfo(cardId); } },
    };
    if (previousCardId)
        items.push_back({ "ankinx/review/previous_info"_i18n, [this, id = previousCardId] { showCardInfo(id); } });

    std::vector<std::string> names;
    for (const Item& item : items)
        names.push_back(item.first);
    auto* dropdown = new brls::Dropdown("ankinx/review/card_menu"_i18n, names,
                                        [this, items, token = alive](int selected) {
                                            if (selected < 0 || selected >= (int)items.size())
                                                return;
                                            // After the menu has closed: some items open
                                            // another menu or the keyboard.
                                            brls::sync([token, action = items[selected].second] {
                                                if (*token)
                                                    action();
                                            });
                                        });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ReviewerActivity::chooseFlag()
{
    if (!menuAvailable())
        return;
    std::vector<std::string> names;
    for (int flag = 0; flag <= 7; flag++)
        names.push_back(flagName(flag));
    auto* dropdown = new brls::Dropdown(
        "ankinx/review/flag"_i18n, names,
        [this, token = alive](int flag) {
            if (*token && flag >= 0 && flag <= 7 && menuAvailable())
                cardAction("flag", { { "flag", flag } }, brls::getStr("ankinx/review/flag_set", flagName(flag)));
        },
        card.value("flag", 0));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

// As Anki's "Set Due Date": days from today, a range, "!" to set the interval too.
void ReviewerActivity::setDueDate()
{
    if (!menuAvailable())
        return;
    keyboard::text("ankinx/review/due_prompt"_i18n, "", 20, [this, token = alive](const std::string& days) {
        if (*token && !days.empty() && menuAvailable())
            cardAction("set_due_date", { { "days", days } }, "ankinx/review/due_set"_i18n);
    });
}

void ReviewerActivity::cardAction(const std::string& action, anki::json args, const std::string& done)
{
    if (!menuAvailable())
        return;
    args["card_id"]  = card["card_id"];
    args["action"]   = action;
    bool staysShown  = action == "flag" || action == "toggle_mark";
    if (!staysShown)
    {
        state = State::Loading;
        audio::stop();
    }
    brls::async([this, args, done, staysShown, token = alive] {
        try
        {
            anki::json result = anki::call("card_action", args);
            onUi(token, [this, args, result, done, staysShown] {
                if (!staysShown)
                {
                    brls::Application::notify(done);
                    loadNext();
                    return;
                }
                if (args["action"] == "flag")
                {
                    card["flag"] = args["flag"];
                    brls::Application::notify(done);
                }
                else
                {
                    card["marked"] = result.value("marked", false);
                    brls::Application::notify(card["marked"].get<bool>() ? "ankinx/review/marked"_i18n
                                                                         : "ankinx/review/unmarked"_i18n);
                }
                showCardState();
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            onUi(token, [this, text, staysShown] {
                if (staysShown)
                    brls::Application::notify(text);
                else
                    showError(text);
            });
        }
    });
}

// Anki's card info, laid out by a card view (HTML tables). B closes it.
void ReviewerActivity::showCardInfo(int64_t cardId)
{
    brls::async([cardId, token = alive] {
        try
        {
            anki::json info = anki::call("card_info", { { "card_id", cardId } });
            onUi(token, [info] {
                bool night       = nightMode();
                std::string html = std::string("<html><head><style>") + CARD_INFO_CSS + "</style>";
                if (night)
                    html += std::string("<style>") + NIGHT_CSS + "</style>";
                html += std::string("</head><body class=\"card") + (night ? " nightMode" : "") + "\">" +
                        cardInfoHtml(info) + "</body></html>";
                auto* view = new CardView();
                view->setGrow(1.0f);
                view->setHtml(html);
                auto* frame = new brls::AppletFrame(view);
                frame->setTitle("ankinx/review/card_info"_i18n);
                app::hideStatus(frame);
                brls::Application::pushActivity(new brls::Activity(frame));
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            onUi(token, [text] { brls::Application::notify(text); });
        }
    });
}

// The replay button: let go within 400 ms to replay, keep holding for the card
// menu (docs/ux-plan.md). The button state is polled; actions only see presses.
void ReviewerActivity::pressReplay(brls::ControllerButton button)
{
    int generation = ++holdGeneration;
    auto start     = std::chrono::steady_clock::now();
    auto poll      = std::make_shared<std::function<void()>>();
    // The pending delay holds the poll alive; the poll only refers to itself weakly.
    *poll = [this, token = alive, generation, button, start, weak = std::weak_ptr<std::function<void()>>(poll)] {
        if (!*token || generation != holdGeneration)
            return;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        if (!brls::Application::getControllerState().buttons[button])
            replay();
        else if (ms.count() >= 400)
        {
            if (menuAvailable())
                openCardMenu();
        }
        else if (auto again = weak.lock())
            brls::delay(30, [again] { (*again)(); });
    };
    brls::delay(30, [poll] { (*poll)(); });
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
    previousCardId = card.value("card_id", (int64_t)0);
    brls::async([this, ease, token = alive] {
        try
        {
            anki::json result = anki::call("answer", { { "ease", ease } });
            onUi(token, [this, result] {
                // As Anki: say when a card has become a leech.
                if (result.value("leech", false))
                {
                    auto* dialog = new brls::Dialog(result.value("suspended", false)
                                                        ? "ankinx/review/leech_suspended"_i18n
                                                        : "ankinx/review/leech"_i18n);
                    dialog->addButton("hints/ok"_i18n, [] {});
                    dialog->open();
                }
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
    std::string body = "<div style=\"text-align:center; margin-top:100px; font-size:26px\">" +
                       escapeHtml("ankinx/review/congrats"_i18n) + "</div>";
    doneMessages(body);
    card["css"]        = "";
    card["body_class"] = nightMode() ? "card nightMode" : "card";
    cardView->setHtml(documentFor(body));
    showAnswerBox->setVisibility(brls::Visibility::GONE);
    easeButtons->setVisibility(brls::Visibility::GONE);
    showCardState();
    refreshControls();
}

// What Anki's congratulations screen adds: learning cards due later (studying
// resumes by itself then), limits reached, buried cards.
void ReviewerActivity::doneMessages(std::string& body)
{
    const anki::json& congrats = card["congrats"];
    if (!congrats.is_object())
        return;
    std::vector<std::string> lines;
    if (congrats["secs_until_next_learn"].is_number())
    {
        int secs = congrats["secs_until_next_learn"].get<int>();
        lines.push_back(brls::getStr("ankinx/review/next_learn", std::to_string((secs + 59) / 60)));
        brls::delay((long)secs * 1000, [this, token = alive, generation = ++resumeGeneration] {
            if (*token && state == State::Done && generation == resumeGeneration)
                loadNext();
        });
    }
    if (congrats.value("new_limit_reached", false))
        lines.push_back("ankinx/review/new_limit"_i18n);
    if (congrats.value("review_limit_reached", false))
        lines.push_back("ankinx/review/review_limit"_i18n);
    if (congrats.value("buried", false))
        lines.push_back("ankinx/review/buried_today"_i18n);
    for (const std::string& line : lines)
        body += "<p style=\"text-align:center; font-size:20px; color:#888888\">" + escapeHtml(line) + "</p>";
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
        // Show Answer's button (A) does nothing here: grading is on the ease
        // buttons (and A only grades where a layout binds it, as one Joy-Con's Good).
        order = { Action::Again, Action::Hard, Action::Good, Action::Easy,
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
            else if (state == State::Done || state == State::Failed)
                brls::Application::popActivity();
            break;
        case Action::Again: answer(1); break;
        case Action::Hard: answer(2); break;
        case Action::Good: answer(3); break;
        case Action::Easy: answer(4); break;
        case Action::Undo: undo(); break;
        case Action::ReplayAudio: pressReplay(button); break;
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
            case Action::ShowAnswer:
                // The Show Answer button shows its button already.
                if (state != State::Question)
                    hint = "hints/ok"_i18n;
                break;
            case Action::Undo: hint = "ankinx/review/undo"_i18n; break;
            case Action::ReplayAudio:
                // Without audio on this side the button only opens the menu.
                if (!menuAvailable())
                    hint = "ankinx/review/replay"_i18n;
                else if (sideHasAudio())
                    hint = "ankinx/review/replay_hold"_i18n;
                else
                    hint = "ankinx/review/hold_menu"_i18n;
                break;
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
