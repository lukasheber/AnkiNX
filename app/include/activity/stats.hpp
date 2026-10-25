#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <memory>
#include <string>

class ForestHeatmap;
class StatsView;

// Statistics (view/stats_view) for one deck, with its subdecks, or (deckId
// 0) the whole collection, with Memory Forest's study heatmap first. From
// the deck overview (R), Settings and the streak on the deck list.
class StatsActivity : public brls::Activity
{
  public:
    // `forest`: Memory Forest's data (forest::grow), for its heatmap on the whole
    // collection's; grown here if not given.
    StatsActivity(int64_t deckId, std::string title, std::string forest = "");
    ~StatsActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    int64_t deckId;
    std::string title;
    std::string forestData;
    ForestHeatmap* heatmap = nullptr; // (StatsView owns it)
    brls::AppletFrame* frame = nullptr;
    StatsView* view          = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
