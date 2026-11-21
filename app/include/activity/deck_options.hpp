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
// the deck's preset (chosen, added, cloned, renamed, removed), daily limits,
// steps, lapses, display order, burying, audio, timer, auto advance, easy
// days, FSRS or SM-2 values, and whether there is custom scheduling (which
// can be turned off here). Changes apply at once to the deck's preset
// (shared presets say so at the top).
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
    // Send `values` (and/or fsrs, custom_scheduling, preset) to the backend,
    // then show what was saved; `done`: shown once the change is saved.
    void apply(anki::json args, std::string done = "");
    // The preset selector and its menu (the desktop's Save menu actions).
    void showPresets(const anki::json& options);
    void presetActions();
    // The desktop's Display Order and Easy Days sections.
    void fillDisplayOrder();
    void fillEasyDays();
    void showEasyDays();
    // A choice among the backend's `choices[key]` (numbers, with the
    // desktop's words), which can change with other values.
    void orderChoice(const std::string& key);
    void showOrderChoices();
    // The desktop's wording for `key` (the backend's labels), else `fallback`.
    std::string label(const std::string& key, const std::string& fallback) const;
    // A description under a row (the desktop's texts, their lines joined).
    brls::Label* note(const std::string& text);

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
    // The desktop's wording (deck_options' labels).
    anki::json labels;
    // The presets (ids in the selector's order) and the deck's.
    brls::SelectorCell* presetCell = nullptr;
    std::vector<int64_t> presetIds;
    int64_t presetId = 0;
    std::string presetName, removeConfirm;
    brls::Label* sharedNote = nullptr;
    // Display Order's choices: key -> (cell, the choices' values).
    anki::json orderChoicesJson;
    std::map<std::string, std::pair<brls::SelectorCell*, std::vector<int>>> orderChoices;
    // Easy Days: the weekdays (Monday first) and the desktop's warnings;
    // `easyDaysChanged`: changed here (existing reviews keep their dates).
    std::vector<brls::SelectorCell*> easyDayCells;
    brls::Label* noNormalDayNote    = nullptr;
    brls::Label* easyDaysChangeNote = nullptr;
    bool easyDaysChanged            = false;
    // Whether the collection has custom scheduling code (written in Anki
    // desktop; selecting the row offers to delete it).
    bool customScheduling = false;
    brls::DetailCell* customSchedulingCell = nullptr;
    bool built = false;
    brls::AppletFrame* frame = nullptr;
    brls::ScrollingFrame* scroller = nullptr;
    brls::Box* list = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
