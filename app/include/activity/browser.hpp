#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <memory>
#include <string>

#include "anki/api.hpp"

class CardView;

// Read-only card browser (L on the deck overview): an Anki search, the
// matching cards by sort field, and a preview of each with flag and
// suspend actions. No editing (AnkiNX is for reviewing).
class BrowserActivity : public brls::Activity
{
  public:
    explicit BrowserActivity(std::string search);
    ~BrowserActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;
    void onResume() override;

  private:
    void load();
    void fill(const anki::json& result);

    std::string search;
    bool loaded = false;
    brls::AppletFrame* frame   = nullptr;
    brls::DetailCell* searchCell = nullptr;
    brls::Label* countLabel    = nullptr;
    brls::Box* results         = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// One card's answer side, with X (flag) and Y (suspend / unsuspend).
class PreviewActivity : public brls::Activity
{
  public:
    PreviewActivity(int64_t cardId, int flag, bool suspended);
    ~PreviewActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    void act(const std::string& action, anki::json args, const std::string& done);
    void updateHints();

    int64_t cardId;
    int flag;
    bool suspended;
    brls::ActionIdentifier suspendAction = -1;
    anki::json answerAv;
    std::string mediaDir;
    brls::AppletFrame* frame = nullptr;
    CardView* view           = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
