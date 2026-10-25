#pragma once

#include <borealis.hpp>
#include <memory>
#include <vector>

#include "anki/api.hpp"

class ForestHeatmap;

// Statistics panels drawn natively from the backend's `stats` digest:
// Today, Forecast (next 30 days), Heatmap (reviews per day, 26 weeks) and
// Retention (Anki's true retention). L/R or a tap on the tabs switch panels.
// For the whole collection, Memory Forest's study heatmap (view/forest_heatmap)
// comes first in place of the plain one: the D-pad or a tap picks a day, A
// opens its cards.
class StatsView : public brls::View
{
  public:
    StatsView();
    ~StatsView() override;

    void setData(anki::json data);
    // Memory Forest's heatmap as the first panel (owned from then on).
    void setForestHeatmap(ForestHeatmap* heatmap);
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    enum class Panel
    {
        Today,
        Forecast,
        Heatmap,
        Retention,
        Forest,
    };
    Panel current() const { return panels[panel]; }
    void switchPanel(int delta);
    void drawTabs(NVGcontext* vg, float x, float y, float width);
    void drawToday(NVGcontext* vg, float x, float y, float width, float height);
    void drawForecast(NVGcontext* vg, float x, float y, float width, float height);
    void drawHeatmap(NVGcontext* vg, float x, float y, float width, float height);
    void drawRetention(NVGcontext* vg, float x, float y, float width, float height);

    anki::json data;
    std::vector<Panel> panels = { Panel::Today, Panel::Forecast, Panel::Heatmap, Panel::Retention };
    int panel = 0;
    std::unique_ptr<ForestHeatmap> forest;
};
