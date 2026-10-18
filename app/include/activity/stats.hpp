#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <memory>
#include <string>

class StatsView;

// Statistics (view/stats_view) for one deck, with its subdecks, or (deckId
// 0) the whole collection. From the deck overview (R) and Settings.
class StatsActivity : public brls::Activity
{
  public:
    StatsActivity(int64_t deckId, std::string title);
    ~StatsActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    int64_t deckId;
    std::string title;
    brls::AppletFrame* frame = nullptr;
    StatsView* view          = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
