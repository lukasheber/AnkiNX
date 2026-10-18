#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <memory>
#include <string>

#include "view/card_view.hpp"

// Deck overview (R on a deck, as AnkiDroid's Study Options screen): today's
// counts, the deck's description, Study now (A) and Options (Y).
class DeckOverviewActivity : public brls::Activity
{
  public:
    explicit DeckOverviewActivity(int64_t deckId);
    ~DeckOverviewActivity() override;

    CONTENT_FROM_XML_RES("activity/deck_overview.xml");
    void onContentAvailable() override;
    void onResume() override;

  private:
    void load();
    void study();
    void openOptions();

    int64_t deckId;
    std::string deckName;
    bool loaded   = false;
    bool filtered = false;
    // Cleared in the destructor; background work checks a copy.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    BRLS_BIND(brls::AppletFrame, frame, "frame");
    BRLS_BIND(brls::Label, newCount, "newCount");
    BRLS_BIND(brls::Label, learnCount, "learnCount");
    BRLS_BIND(brls::Label, reviewCount, "reviewCount");
    BRLS_BIND(brls::Label, newCaption, "newCaption");
    BRLS_BIND(brls::Label, learnCaption, "learnCaption");
    BRLS_BIND(brls::Label, reviewCaption, "reviewCaption");
    BRLS_BIND(CardView, description, "description");
    BRLS_BIND(brls::Box, studyButton, "studyButton");
    BRLS_BIND(brls::Label, studyLabel, "studyLabel");
    BRLS_BIND(brls::Box, optionsButton, "optionsButton");
    BRLS_BIND(brls::Label, optionsLabel, "optionsLabel");
};
