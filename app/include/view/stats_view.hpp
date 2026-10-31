#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>
#include <vector>

#include "anki/api.hpp"

class ForestHeatmap;

// Statistics panels drawn natively from the backend's `stats` digest:
// Today, Forecast (next 30 days), Heatmap (reviews per day, 26 weeks) and
// Retention (Anki's true retention). L/R or a tap on the tabs switch panels.
// On the heatmap the D-pad or a tap picks a day and A opens its cards (as
// clicking a day in Anki's calendar). For the whole collection, Memory
// Forest's study heatmap (view/forest_heatmap) comes first in place of the
// plain one, and works the same way.
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
    // The plain heatmap's chosen day (days from today, 0 or less).
    void chooseDay(int day);
    void tapDay(float x, float y);
    unsigned reviewsOn(int day) const;
    // Anki's search for the chosen day's answered cards, or "" for none.
    std::string daySearch() const;
    // A is available only where it opens cards.
    void updateHints();
    void drawTabs(NVGcontext* vg, float x, float y, float width);
    void drawToday(NVGcontext* vg, float x, float y, float width, float height);
    void drawForecast(NVGcontext* vg, float x, float y, float width, float height);
    void drawHeatmap(NVGcontext* vg, float x, float y, float width, float height);
    void drawRetention(NVGcontext* vg, float x, float y, float width, float height);

    anki::json data;
    std::vector<Panel> panels = { Panel::Today, Panel::Forecast, Panel::Heatmap, Panel::Retention };
    int panel = 0;
    std::unique_ptr<ForestHeatmap> forest;
    int chosenDay = 0;
    // where the plain heatmap was drawn last, for taps
    float heatmapLeft = 0, heatmapTop = 0, heatmapCell = 0;
    int heatmapFirst = 0;
};
