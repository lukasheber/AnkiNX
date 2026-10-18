#include "activity/deck_overview.hpp"

#include "activity/deck_options.hpp"
#include "activity/reviewer.hpp"
#include "anki/api.hpp"
#include "app.hpp"
#include "util/keyboard.hpp"

#include <cstdlib>
#include <string>
#include <vector>

using namespace brls::literals;

namespace
{

const char* DESCRIPTION_CSS = R"CSS(
body { margin: 8px 0; font-size: 20px; text-align: center; color: #333333; }
.nightMode { color: #e6e6e6; }
a { color: #3b82f6; }
.nightMode a { color: #8ab4f8; }
img { max-width: 100%; height: auto; }
)CSS";

bool nightMode() { return brls::Application::getThemeVariant() == brls::ThemeVariant::DARK; }

} // namespace

DeckOverviewActivity::DeckOverviewActivity(int64_t deckId)
    : deckId(deckId)
{
}

DeckOverviewActivity::~DeckOverviewActivity() { *alive = false; }

void DeckOverviewActivity::onContentAvailable()
{
    app::hideStatus(frame);
    newCaption->setText("ankinx/decks/col_new"_i18n);
    learnCaption->setText("ankinx/decks/col_learn"_i18n);
    reviewCaption->setText("ankinx/decks/col_due"_i18n);
    studyLabel->setText(brls::Hint::getKeyIcon(brls::BUTTON_A, true) + "  " + "ankinx/overview/study_now"_i18n);
    studyButton->addGestureRecognizer(new brls::TapGestureRecognizer(studyButton, [this] { study(); }));
    frame->registerAction("ankinx/overview/study_now"_i18n, brls::BUTTON_A, [this](brls::View*) {
        study();
        return true;
    }, true);
    customLabel->setText(brls::Hint::getKeyIcon(brls::BUTTON_X, true) + "  " + "ankinx/overview/custom_study"_i18n);
    customButton->addGestureRecognizer(new brls::TapGestureRecognizer(customButton, [this] { openCustomStudy(); }));
    frame->registerAction("ankinx/overview/custom_study"_i18n, brls::BUTTON_X, [this](brls::View*) {
        openCustomStudy();
        return true;
    }, true);
    optionsLabel->setText(brls::Hint::getKeyIcon(brls::BUTTON_Y, true) + "  " + "ankinx/overview/options"_i18n);
    optionsButton->addGestureRecognizer(new brls::TapGestureRecognizer(optionsButton, [this] { openOptions(); }));
    frame->registerAction("ankinx/overview/options"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        openOptions();
        return true;
    }, true);
    load();
}

void DeckOverviewActivity::openCustomStudy()
{
    if (!loaded || filtered)
        return;
    brls::async([this, token = alive, id = deckId] {
        try
        {
            anki::json defaults = anki::call("custom_study_defaults", { { "deck_id", id } });
            brls::sync([this, token, defaults] {
                if (!*token)
                    return;
                auto t = [](const char* key) { return brls::getStr(std::string("ankinx/custom_study/") + key); };
                std::vector<std::string> choices = { t("new_limit"), t("review_limit"), t("forgot"),
                                                     t("ahead"),     t("preview"),      t("cram") };
                auto* dropdown = new brls::Dropdown(
                    "ankinx/overview/custom_study"_i18n, choices, [this, token, defaults, t](int choice) {
                        // After the menu has closed: the next step opens another.
                        brls::sync([this, token, defaults, choice, t] {
                            if (!*token)
                                return;
                            int availableNew    = defaults.value("available_new", 0);
                            int availableReview = defaults.value("available_review", 0);
                            switch (choice)
                            {
                                case 0:
                                    askNumber(brls::getStr("ankinx/custom_study/new_limit_prompt",
                                                           std::to_string(availableNew)),
                                              defaults.value("extend_new", 10), "new_limit", anki::json::array());
                                    break;
                                case 1:
                                    askNumber(brls::getStr("ankinx/custom_study/review_limit_prompt",
                                                           std::to_string(availableReview)),
                                              defaults.value("extend_review", 50), "review_limit",
                                              anki::json::array());
                                    break;
                                case 2: askNumber(t("forgot_prompt"), 1, "forgot", anki::json::array()); break;
                                case 3: askNumber(t("ahead_prompt"), 1, "ahead", anki::json::array()); break;
                                case 4: askNumber(t("preview_prompt"), 1, "preview", anki::json::array()); break;
                                case 5: chooseCram(defaults); break;
                                default: break;
                            }
                        });
                    });
                brls::Application::pushActivity(new brls::Activity(app::withoutStatus(dropdown)));
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

// "Study by card state or tag": which cards, then optionally one tag.
void DeckOverviewActivity::chooseCram(const anki::json& defaults)
{
    auto t = [](const char* key) { return brls::getStr(std::string("ankinx/custom_study/") + key); };
    static const char* KINDS[4] = { "cram_new", "cram_due", "cram_review", "cram_all" };
    std::vector<std::string> states = { t("cram_new"), t("cram_due"), t("cram_review"), t("cram_all") };
    auto* dropdown = new brls::Dropdown(t("cram"), states, [this, token = alive, defaults, t](int state) {
        if (state < 0 || state > 3)
            return;
        brls::sync([this, token, defaults, state, t] {
            if (!*token)
                return;
            std::vector<std::string> tags = { t("all_tags") };
            for (const auto& tag : defaults.value("tags", anki::json::array()))
                tags.push_back(tag.get<std::string>());
            auto* tagMenu = new brls::Dropdown(t("tag"), tags, [this, token, state, tags, t](int tag) {
                if (tag < 0)
                    return;
                anki::json include = anki::json::array();
                if (tag > 0)
                    include.push_back(tags[tag]);
                brls::sync([this, token, state, include, t] {
                    if (*token)
                        askNumber(t("cram_prompt"), 100, KINDS[state], include);
                });
            });
            brls::Application::pushActivity(new brls::Activity(app::withoutStatus(tagMenu)));
        });
    });
    brls::Application::pushActivity(new brls::Activity(app::withoutStatus(dropdown)));
}

void DeckOverviewActivity::askNumber(const std::string& prompt, int initial, const std::string& kind, anki::json tags)
{
    keyboard::text(prompt, std::to_string(initial), 4, [this, kind, tags, token = alive](const std::string& text) {
        if (!*token)
            return;
        char* end = nullptr;
        long value = std::strtol(text.c_str(), &end, 10);
        if (text.empty() || *end != 0 || value < 1 || value > 9999)
        {
            brls::Application::notify("ankinx/options/invalid"_i18n);
            return;
        }
        runCustomStudy(kind, (int)value, tags);
    });
}

// Raising a limit stays here (the counts change); the other kinds build the
// "Custom Study Session" filtered deck, which is studied at once.
void DeckOverviewActivity::runCustomStudy(const std::string& kind, int value, anki::json tags)
{
    anki::json args = { { "deck_id", deckId }, { "kind", kind }, { "value", value }, { "tags", tags } };
    brls::async([this, args, kind, token = alive] {
        try
        {
            anki::json result = anki::call("custom_study", args);
            brls::sync([this, token, result, kind] {
                if (!*token)
                    return;
                if (kind.size() > 6 && kind.compare(kind.size() - 6, 6, "_limit") == 0)
                {
                    brls::Application::notify("ankinx/custom_study/limit_raised"_i18n);
                    load();
                    return;
                }
                brls::Application::pushActivity(
                    new ReviewerActivity(result["deck_id"].get<int64_t>(), result.value("name", "")));
            });
        }
        catch (const anki::Error& e)
        {
            // e.g. "No cards matched the criteria you provided."
            std::string text = e.what();
            brls::sync([token, text] {
                if (*token)
                    brls::Application::notify(text);
            });
        }
    });
}

// Filtered decks have no options of their own (Anki shows their search instead).
void DeckOverviewActivity::openOptions()
{
    if (loaded && !filtered)
        brls::Application::pushActivity(new DeckOptionsActivity(deckId));
}

// Back from studying: the counts have changed.
void DeckOverviewActivity::onResume()
{
    if (loaded)
        load();
}

void DeckOverviewActivity::load()
{
    brls::async([this, token = alive, id = deckId] {
        try
        {
            anki::json overview = anki::call("deck_overview", { { "deck_id", id } });
            brls::sync([this, token, overview] {
                if (!*token)
                    return;
                loaded   = true;
                deckName = overview.value("name", "");
                filtered = overview.value("filtered", false);
                optionsButton->setVisibility(filtered ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
                customButton->setVisibility(filtered ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
                frame->setTitle(deckName);
                newCount->setText(std::to_string(overview.value("new", 0)));
                learnCount->setText(std::to_string(overview.value("learn", 0)));
                reviewCount->setText(std::to_string(overview.value("review", 0)));
                bool night       = nightMode();
                std::string html = std::string("<html><head><style>") + DESCRIPTION_CSS + "</style></head><body" +
                                   (night ? " class=\"nightMode\"" : "") + ">" +
                                   overview.value("description", "") + "</body></html>";
                description->setHtml(html, true);
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

void DeckOverviewActivity::study()
{
    if (!loaded)
        return;
    brls::Application::pushActivity(new ReviewerActivity(deckId, deckName));
}
