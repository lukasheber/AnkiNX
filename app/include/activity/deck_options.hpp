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
// daily limits, steps, lapses, burying, audio, timer, auto advance, FSRS
// (version, retention, parameters and optimizing them, the FSRS-7 fork's
// same-day options) or
// SM-2 values, and whether there is custom scheduling (which can be turned
// off here). Changes apply at once to the deck's preset (shared presets say
// so at the top).
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
    // Send `values` (and/or fsrs, custom_scheduling) to the backend, then show
    // what was saved.
    void apply(anki::json args);

    // Rows.
    void number(const std::string& title, const std::string& key, bool decimal);
    // Text such as "1m 10m 1d"; `format` is shown on the keyboard as an example.
    void steps(const std::string& title, const std::string& key, const std::string& format = "1m 10m 1d");
    // A collection-wide switch sent as `arg` (not one of the preset's values).
    brls::BooleanCell* collectionToggle(const std::string& title, const std::string& arg, bool on);
    // FSRS parameters: whether they are the defaults; offers to reset them.
    void paramsRow();
    void showParams();
    // Optimize the preset's parameters on the console (Anki's optimizer, in
    // the background with progress and Cancel), then offer the result next
    // to the current parameters.
    void optimize();
    void showOptimized(const anki::json& result);
    void applyOptimized();
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
    // FSRS rows only for FSRS-7 (the optimizer's same-day and penalty options).
    std::vector<brls::View*> fsrs7Rows;
    // The FSRS-7 fork's options: collection-wide switches (arg -> cell), the
    // parameters row, and the note on presets set to RWKV in Anki desktop.
    std::map<std::string, brls::BooleanCell*> collectionToggles;
    int fsrsParams = 0;
    brls::DetailCell* paramsCell = nullptr;
    std::string rwkv = "off";
    brls::Label* rwkvNote = nullptr;
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
