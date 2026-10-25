#include "activity/stats.hpp"

#include "anki/api.hpp"
#include "app.hpp"
#include "util/forest.hpp"
#include "view/forest_heatmap.hpp"
#include "view/stats_view.hpp"

using namespace brls::literals;

StatsActivity::StatsActivity(int64_t deckId, std::string title, std::string forest)
    : deckId(deckId)
    , title(std::move(title))
    , forestData(std::move(forest))
{
}

StatsActivity::~StatsActivity() { *alive = false; }

brls::View* StatsActivity::createContentView()
{
    view = new StatsView();
    view->setGrow(1.0f);
    if (!deckId)
    {
        heatmap = new ForestHeatmap();
        view->setForestHeatmap(heatmap);
    }
    frame = new brls::AppletFrame(view);
    frame->setTitle("ankinx/stats/title"_i18n + " — " + title);
    return frame;
}

void StatsActivity::onContentAvailable()
{
    app::hideStatus(frame);
    if (heatmap && !forestData.empty())
        heatmap->setData(forestData);
    else if (heatmap)
    {
        // the forest's data for its heatmap: the scenery of the day if the forest is on,
        // else the first (the plain green it is drawn in then)
        const forest::Scenery* scenery = forest::forToday();
        if (!scenery && !forest::sceneries().empty())
            scenery = &forest::sceneries()[0];
        if (scenery)
            brls::async([this, scenery, token = alive] {
                try
                {
                    std::string data = forest::grow(*scenery);
                    brls::sync([this, token, data] {
                        if (*token)
                            heatmap->setData(data);
                    });
                }
                catch (const anki::Error& e)
                {
                    brls::Logger::error("forest heatmap: {}", e.what());
                }
            });
    }
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
