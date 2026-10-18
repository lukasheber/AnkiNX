#pragma once

#include <borealis.hpp>

#include "anki/api.hpp"

// Statistics panels drawn natively from the backend's `stats` digest:
// Today, Forecast (next 30 days), Heatmap (reviews per day, 26 weeks) and
// Retention (Anki's true retention). L/R or a tap on the tabs switch panels.
class StatsView : public brls::View
{
  public:
    StatsView();

    void setData(anki::json data);
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    void switchPanel(int delta);
    void drawTabs(NVGcontext* vg, float x, float y, float width);
    void drawToday(NVGcontext* vg, float x, float y, float width, float height);
    void drawForecast(NVGcontext* vg, float x, float y, float width, float height);
    void drawHeatmap(NVGcontext* vg, float x, float y, float width, float height);
    void drawRetention(NVGcontext* vg, float x, float y, float width, float height);

    anki::json data;
    int panel = 0;
    static constexpr int PANELS = 4;
};
