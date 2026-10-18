#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <memory>
#include <string>

#include "anki/api.hpp"
#include "view/card_view.hpp"

// Deck overview (R on a deck, as AnkiDroid's Study Options screen): today's
// counts, the deck's description, Study now (A), Custom study (X) and
// Options (Y).
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
    // Custom study (X): choose, give a number (and state/tag), then study.
    void openCustomStudy();
    void chooseCram(const anki::json& defaults);
    void askNumber(const std::string& prompt, int initial, const std::string& kind, anki::json tags);
    void runCustomStudy(const std::string& kind, int value, anki::json tags);

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
    BRLS_BIND(brls::Box, customButton, "customButton");
    BRLS_BIND(brls::Label, customLabel, "customLabel");
};
