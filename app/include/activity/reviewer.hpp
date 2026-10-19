#pragma once

#include <borealis.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "anki/api.hpp"
#include "script/card_script.hpp"
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
    void redo();
    void undoOrRedo(bool redo);
    void replay();
    void revealHint(const std::string& id);
    void revealAllHints();
    void showDone();
    void showError(const std::string& message);
    void updateCounts(const anki::json& counts, const std::string& queue);
    void setBar(bool answerSide);
    // Card scripts (script/card_script).
    void startScripts();
    void runScripts();
    void afterScript();
    void scheduleTimers();
    // Controls (util/controls): bindings depend on the controller layout.
    controls::Action actionFor(brls::ControllerButton button) const;
    bool onButton(brls::ControllerButton button);
    bool masksToggle() const;
    void refreshControls();
    void applyControls();
    std::string documentFor(const std::string& body) const;
    void playAudio(const anki::json& tags);
    // Card menu (hold the replay audio button, or tap ⋯), card info, messages.
    void showCardState();
    void openCardMenu();
    void chooseFlag();
    void setDueDate();
    // `card_action` on the shown card; `done` is the notification, or empty
    // when the result has its own (marking). Card-removing actions load the
    // next card.
    void cardAction(const std::string& action, anki::json args, const std::string& done);
    void showCardInfo(int64_t cardId);
    // The replay button: a short press replays, holding opens the card menu.
    void pressReplay(brls::ControllerButton button);
    void doneMessages(std::string& body);
    // Answer timer and auto advance (deck options in card["timing"]).
    void startTimer();
    void tickTimer(int generation);
    void scheduleAdvance();
    void advance();
    bool canAutoAdvance() const;
    bool menuAvailable() const;
    bool sideHasAudio() const;
    // Card zoom, remembered per deck (settings "zoom": deck id -> zoom).
    void setZoom(float zoom);
    // Whiteboard (touch): the pen toggles drawing over the card.
    void setWhiteboard(bool on);
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
    // JavaScript of the current card, if it has any; replaced per card.
    std::unique_ptr<CardScript> script;
    int scriptGeneration = 0;
    // The card answered before this one ("Previous card info").
    int64_t previousCardId = 0;
    // A press of the replay button being watched for a hold.
    int holdGeneration = 0;
    // Bumped when the done screen's "resume when learning cards are due" timer
    // must no longer fire.
    int resumeGeneration = 0;
    // The answer timer: when the card was shown, and its tick chain.
    std::chrono::steady_clock::time_point shownAt;
    int timerGeneration = 0;
    bool timerFrozen    = false;
    // Auto advance: turned on for the session from the card menu, as in Anki.
    bool autoAdvance       = false;
    int advanceGeneration  = 0;

    BRLS_BIND(brls::AppletFrame, frame, "frame");
    BRLS_BIND(CardView, cardView, "card");
    BRLS_BIND(brls::Box, showAnswerBox, "showAnswer");
    BRLS_BIND(brls::Label, showAnswerLabel, "showAnswerLabel");
    BRLS_BIND(brls::Box, easeButtons, "easeButtons");
    BRLS_BIND(brls::Label, countNew, "countNew");
    BRLS_BIND(brls::Label, countLearn, "countLearn");
    BRLS_BIND(brls::Label, countReview, "countReview");
    BRLS_BIND(brls::Box, flagIcon, "flagIcon");
    BRLS_BIND(brls::Label, markIcon, "markIcon");
    BRLS_BIND(brls::Label, menuIcon, "menuIcon");
    BRLS_BIND(brls::Box, menuButton, "menuButton");
    BRLS_BIND(brls::Box, flagBar, "flagBar");
    BRLS_BIND(brls::Label, timerLabel, "timerLabel");
    BRLS_BIND(brls::Box, penButton, "penButton");
    BRLS_BIND(brls::Label, penIcon, "penIcon");
    BRLS_BIND(brls::Label, penLabel, "penLabel");
    BRLS_BIND(brls::Box, clearButton, "clearButton");
    BRLS_BIND(brls::Label, clearIcon, "clearIcon");
    BRLS_BIND(brls::Label, clearLabel, "clearLabel");
    BRLS_BIND(brls::Box, drawHint, "drawHint");
    BRLS_BIND(brls::Label, drawHintLabel, "drawHintLabel");
};
