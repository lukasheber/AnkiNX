#include "activity/deck_overview.hpp"

#include "activity/reviewer.hpp"
#include "anki/api.hpp"
#include "app.hpp"

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
    load();
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
