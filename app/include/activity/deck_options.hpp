#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "anki/api.hpp"

// A deck's options (Y on the deck overview), as Anki's deck options screen:
// daily limits, steps, lapses, burying, audio, timer, auto advance, FSRS or
// SM-2 values. Changes
// apply at once to the deck's preset (shared presets say so at the top).
class DeckOptionsActivity : public brls::Activity
{
  public:
    explicit DeckOptionsActivity(int64_t deckId);
    ~DeckOptionsActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    // Fetch the options: build the list the first time, then update its rows
    // in place (never rebuilt: that would delete the focused row).
    void load();
    void fill(const anki::json& options);
    void refresh(const anki::json& options);
    // FSRS shows desired retention; SM-2 shows ease and multipliers.
    void showSchedulerRows();
    // Send `values` (and/or fsrs) to the backend, then show what was saved.
    void apply(anki::json args);

    // Rows.
    void number(const std::string& title, const std::string& key, bool decimal);
    void steps(const std::string& title, const std::string& key);
    void toggle(const std::string& title, const std::string& key);
    // A choice among `options` (backend values), shown as `names`.
    void choice(const std::string& title, const std::string& key, const std::vector<std::string>& options,
                const std::vector<std::string>& names);

    int64_t deckId;
    anki::json values;
    bool fsrs = false;
    // Value rows, to refresh in place: key -> (cell, shown with decimals).
    std::map<std::string, std::pair<brls::DetailCell*, bool>> details;
    std::map<std::string, brls::BooleanCell*> toggles;
    std::map<std::string, std::pair<brls::SelectorCell*, std::vector<std::string>>> choices;
    brls::BooleanCell* fsrsCell = nullptr;
    std::vector<brls::View*> fsrsRows, sm2Rows;
    bool built = false;
    brls::AppletFrame* frame = nullptr;
    brls::ScrollingFrame* scroller = nullptr;
    brls::Box* list = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
