#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "anki/api.hpp"

// A deck's options (Y on the deck overview), as Anki's deck options screen:
// daily limits, steps, lapses, burying, audio, FSRS or SM-2 values. Changes
// apply at once to the deck's preset (shared presets say so at the top).
class DeckOptionsActivity : public brls::Activity
{
  public:
    explicit DeckOptionsActivity(int64_t deckId);
    ~DeckOptionsActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    // Fetch the options; build the list, or (rebuild false) refresh the shown
    // values in place, keeping focus. The list is rebuilt anyway when FSRS
    // changed, as its rows differ.
    void load(bool rebuild);
    void fill(const anki::json& options);
    // Send `values` (and/or fsrs) to the backend, then show what was saved.
    void apply(anki::json args);

    // Rows.
    void number(const std::string& title, const std::string& key, bool decimal);
    void steps(const std::string& title, const std::string& key);
    void toggle(const std::string& title, const std::string& key);

    int64_t deckId;
    anki::json values;
    bool fsrs = false;
    // Value rows, to refresh in place: key -> (cell, shown with decimals).
    std::map<std::string, std::pair<brls::DetailCell*, bool>> details;
    brls::AppletFrame* frame = nullptr;
    brls::ScrollingFrame* scroller = nullptr;
    brls::Box* list = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
