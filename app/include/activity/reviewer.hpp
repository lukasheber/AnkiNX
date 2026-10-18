#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "anki/api.hpp"
#include "util/controls.hpp"
#include "view/card_view.hpp"

// Study screen, like AnkiDroid's Reviewer: question, Show Answer, then the four
// ease buttons with their next intervals.
class ReviewerActivity : public brls::Activity
{
  public:
    ReviewerActivity(int64_t deckId, std::string deckName);
    ~ReviewerActivity() override;

    CONTENT_FROM_XML_RES("activity/reviewer.xml");
    void onContentAvailable() override;

  private:
    enum class State
    {
        Loading,
        Question,
        Answer,
        Done,
        Failed,
    };

    void loadNext();
    void showQuestion();
    std::string questionBody() const;
    void typeAnswer();
    void showAnswer();
    void answer(int ease);
    void undo();
    void replay();
    void revealHint(const std::string& id);
    void revealAllHints();
    void showDone();
    void showError(const std::string& message);
    void updateCounts(const anki::json& counts, const std::string& queue);
    void setBar(bool answerSide);
    // Controls (util/controls): bindings depend on the controller layout.
    controls::Action actionFor(brls::ControllerButton button) const;
    bool onButton(brls::ControllerButton button);
    bool masksToggle() const;
    void refreshControls();
    void applyControls();
    std::string documentFor(const std::string& body) const;
    void playAudio(const anki::json& tags);
    // Run `f` on the UI thread unless the activity was closed meanwhile. Background
    // tasks must take `token` (a copy of `alive`) when they are created, on the
    // UI thread, never read `alive` themselves.
    static void onUi(std::shared_ptr<bool> token, std::function<void()> f);

    // Cleared in the destructor.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    int64_t deckId;
    std::string deckName;
    std::string mediaDir;
    State state = State::Loading;
    anki::json card;
    // HTML body currently on screen (question or answer), as modified by hints.
    std::string shownBody;
    // Answer typed on a type-answer card ({{type:Field}}).
    std::string typed;
    // Actions registered for the bindable buttons (re-registered on changes).
    std::vector<brls::ActionIdentifier> buttonActions;
    bool refreshPending = false;

    BRLS_BIND(brls::AppletFrame, frame, "frame");
    BRLS_BIND(CardView, cardView, "card");
    BRLS_BIND(brls::Box, showAnswerBox, "showAnswer");
    BRLS_BIND(brls::Label, showAnswerLabel, "showAnswerLabel");
    BRLS_BIND(brls::Box, easeButtons, "easeButtons");
    BRLS_BIND(brls::Label, countNew, "countNew");
    BRLS_BIND(brls::Label, countLearn, "countLearn");
    BRLS_BIND(brls::Label, countReview, "countReview");
};
