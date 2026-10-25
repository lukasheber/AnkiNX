#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>
#include <vector>

#include "anki/api.hpp"

// Memory Forest's study heatmap (app/forest) for the Statistics screen: the
// add-on draws it (its calendar.js, through runner.js `heatmap`) on a forest
// thread of its own, from the forest's data; this puts its pictures on screen,
// scaled up, with the months, the weekdays and the streaks' lengths, and the
// chosen day's tooltip under it. Not a view: StatsView draws it in its first
// panel and hands it the D-pad and taps.
class ForestHeatmap
{
  public:
    ForestHeatmap();
    ~ForestHeatmap();

    // The forest's data (forest::grow); the heatmap is drawn once it comes.
    void setData(const std::string& data);
    void draw(NVGcontext* vg, float x, float y, float width, float height);
    // The chosen day: a week on or back (dx), a day (dy); past the months
    // shown, the next or earlier months come.
    void move(int dx, int dy);
    void tap(float x, float y);
    // Anki's search for the chosen day's cards (those answered that day, or
    // due), or "" when it has none.
    std::string search() const;

    struct Worker;

  private:
    void ask(int page);
    void chooseDay(int o);

    std::shared_ptr<Worker> worker;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    // what was last drawn: its layout (runner.js heatmap) and its pictures
    anki::json layout;
    int image = 0, above = 0, imageW = 0, imageH = 0;
    int page    = 0;
    int chosen  = 0; // the chosen day, days from today
    std::string daySearch; // (its cards, runner.js heatmapDay)
    bool asking = false;
    std::vector<std::string> tip;
    // where the heatmap was drawn last: its top left and scale, for taps
    float ox = 0, oy = 0, k = 1;
};
