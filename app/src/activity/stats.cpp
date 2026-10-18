#include "activity/stats.hpp"

#include "anki/api.hpp"
#include "app.hpp"
#include "view/stats_view.hpp"

using namespace brls::literals;

StatsActivity::StatsActivity(int64_t deckId, std::string title)
    : deckId(deckId)
    , title(std::move(title))
{
}

StatsActivity::~StatsActivity() { *alive = false; }

brls::View* StatsActivity::createContentView()
{
    view = new StatsView();
    view->setGrow(1.0f);
    frame = new brls::AppletFrame(view);
    frame->setTitle("ankinx/stats/title"_i18n + " — " + title);
    return frame;
}

void StatsActivity::onContentAvailable()
{
    app::hideStatus(frame);
    anki::json args = anki::json::object();
    if (deckId)
        args["deck_id"] = deckId;
    brls::async([this, args, token = alive] {
        try
        {
            anki::json stats = anki::call("stats", args);
            brls::sync([this, token, stats] {
                if (*token)
                    view->setData(stats);
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
