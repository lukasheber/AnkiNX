#include "activity/browser.hpp"

#include "app.hpp"
#include "util/audio.hpp"
#include "util/card_document.hpp"
#include "util/keyboard.hpp"
#include "view/card_view.hpp"

#include <cstdlib>
#include <vector>

using namespace brls::literals;

namespace
{

std::string flagName(int flag) { return brls::getStr("ankinx/review/flags/" + std::to_string(flag)); }

// What a row's right side says: deck, then flag and suspension if any.
std::string rowDetail(const anki::json& card)
{
    std::string detail = card.value("deck", "");
    int flag           = card.value("flag", 0);
    if (flag > 0 && flag <= 7)
        detail += "  ·  " + flagName(flag);
    if (card.value("suspended", false))
        detail += "  ·  " + "ankinx/browser/suspended"_i18n;
    return detail;
}

} // namespace

// ----- Browser ----------------------------------------------------------------

BrowserActivity::BrowserActivity(std::string search)
    : search(std::move(search))
{
}

BrowserActivity::~BrowserActivity() { *alive = false; }

brls::View* BrowserActivity::createContentView()
{
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(20, 40, 30, 40);

    searchCell = new brls::DetailCell();
    searchCell->setText("ankinx/browser/search"_i18n);
    searchCell->registerClickAction([this](brls::View*) {
        keyboard::text("ankinx/browser/search_prompt"_i18n, search, 200, [this, token = alive](const std::string& text) {
            if (!*token)
                return;
            search = text;
            load();
        });
        return true;
    });
    box->addView(searchCell);

    countLabel = new brls::Label();
    countLabel->setFontSize(16);
    countLabel->setMarginLeft(16);
    countLabel->setMarginTop(8);
    countLabel->setMarginBottom(8);
    box->addView(countLabel);

    results = new brls::Box(brls::Axis::COLUMN);
    box->addView(results);

    auto* scroller = new brls::ScrollingFrame();
    scroller->setContentView(box);
    scroller->setGrow(1.0f);
    frame = new brls::AppletFrame(scroller);
    frame->setTitle("ankinx/browser/title"_i18n);
    return frame;
}

void BrowserActivity::onContentAvailable()
{
    app::hideStatus(frame);
    load();
}

// Back from a preview: flags or suspension may have changed.
void BrowserActivity::onResume()
{
    if (loaded)
        load();
}

void BrowserActivity::load()
{
    searchCell->setDetailText(search.empty() ? "ankinx/browser/everything"_i18n : search);
    brls::async([this, query = search, token = alive] {
        try
        {
            anki::json result = anki::call("browse", { { "search", query } });
            brls::sync([this, token, result] {
                if (*token)
                    fill(result);
            });
        }
        catch (const anki::Error& e)
        {
            // An invalid search: Anki's message says what is wrong.
            std::string text = e.what();
            brls::sync([this, token, text] {
                if (!*token)
                    return;
                results->clearViews();
                countLabel->setText(text);
            });
        }
    });
}

void BrowserActivity::fill(const anki::json& result)
{
    loaded      = true;
    int total   = result.value("total", 0);
    int shown   = (int)result["cards"].size();
    countLabel->setText(shown < total ? brls::getStr("ankinx/browser/count_some", std::to_string(shown),
                                                     std::to_string(total))
                                      : brls::getStr("ankinx/browser/count", std::to_string(total)));
    results->clearViews();
    for (const anki::json& card : result["cards"])
    {
        auto* row = new brls::DetailCell();
        row->setText(card.value("text", ""));
        row->setDetailText(rowDetail(card));
        int64_t id     = card["card_id"].get<int64_t>();
        int flag       = card.value("flag", 0);
        bool suspended = card.value("suspended", false);
        row->registerClickAction([id, flag, suspended](brls::View*) {
            brls::Application::pushActivity(new PreviewActivity(id, flag, suspended));
            return true;
        });
        results->addView(row);
    }
}

// ----- Preview ----------------------------------------------------------------

PreviewActivity::PreviewActivity(int64_t cardId, int flag, bool suspended)
    : cardId(cardId)
    , flag(flag)
    , suspended(suspended)
{
}

PreviewActivity::~PreviewActivity()
{
    *alive = false;
    audio::stop();
}

brls::View* PreviewActivity::createContentView()
{
    view = new CardView();
    view->setGrow(1.0f);
    view->setMarginLeft(24);
    view->setMarginRight(24);
    frame = new brls::AppletFrame(view);
    frame->setTitle("ankinx/browser/preview"_i18n);
    return frame;
}

void PreviewActivity::onContentAvailable()
{
    app::hideStatus(frame);
    view->setOnLink([this](const std::string& url) {
        // "playsound:a:0": the answer side's first sound.
        if (url.rfind("playsound:", 0) != 0)
            return;
        int index = std::atoi(url.c_str() + 12);
        if (index < 0 || index >= (int)answerAv.size() || answerAv[index].value("kind", "") != "sound")
            return;
        audio::play({ mediaDir + "/" + answerAv[index]["file"].get<std::string>() });
    });
    frame->registerAction("ankinx/review/flag"_i18n, brls::BUTTON_X, [this](brls::View*) {
        std::vector<std::string> names;
        for (int f = 0; f <= 7; f++)
            names.push_back(flagName(f));
        auto* dropdown = new brls::Dropdown(
            "ankinx/review/flag"_i18n, names,
            [this, token = alive](int chosen) {
                if (*token && chosen >= 0 && chosen <= 7)
                    act("flag", { { "flag", chosen } }, brls::getStr("ankinx/review/flag_set", flagName(chosen)));
            },
            flag);
        brls::Application::pushActivity(new brls::Activity(app::withoutStatus(dropdown)));
        return true;
    });
    updateHints();

    brls::async([this, token = alive, id = cardId] {
        try
        {
            std::string dir  = anki::call("status")["media_dir"].get<std::string>();
            anki::json card = anki::call("preview_card", { { "card_id", id }, { "night_mode", card_document::nightMode() } });
            brls::sync([this, token, dir, card] {
                if (!*token)
                    return;
                mediaDir = dir;
                answerAv = card["answer_av"];
                view->setMediaDir(dir);
                view->setHtml(card_document::build(card.value("answer", ""), card.value("css", ""),
                                                   card.value("body_class", "card")));
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            brls::sync([token, text] {
                if (*token)
                    brls::Application::notify(text);
            });
        }
    });
}

// Y suspends or unsuspends, so its hint says which.
void PreviewActivity::updateHints()
{
    if (suspendAction >= 0)
        frame->unregisterAction(suspendAction);
    suspendAction = frame->registerAction(suspended ? "ankinx/browser/unsuspend"_i18n : "ankinx/review/suspend_card"_i18n,
                          brls::BUTTON_Y, [this](brls::View*) {
                              if (suspended)
                                  act("unsuspend", anki::json::object(), "ankinx/browser/unsuspended"_i18n);
                              else
                                  act("suspend_card", anki::json::object(), "ankinx/review/suspended_card"_i18n);
                              return true;
                          });
    brls::Application::getGlobalHintsUpdateEvent()->fire();
}

void PreviewActivity::act(const std::string& action, anki::json args, const std::string& done)
{
    args["card_id"] = cardId;
    args["action"]  = action;
    brls::async([this, args, done, token = alive] {
        try
        {
            anki::call("card_action", args);
            brls::sync([this, token, args, done] {
                if (!*token)
                    return;
                if (args["action"] == "flag")
                    flag = args["flag"].get<int>();
                else
                {
                    suspended = args["action"] == "suspend_card";
                    updateHints();
                }
                brls::Application::notify(done);
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            brls::sync([token, text] {
                if (*token)
                    brls::Application::notify(text);
            });
        }
    });
}
