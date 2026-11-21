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
    // The desktop's Scheduler section, then its RWKV and FSRS sections.
    void fillScheduler(const anki::json& options);
    void fillRwkv();
    void fillFsrs(const anki::json& options);
    // The preset selector and its menu (the desktop's Save menu actions).
    void showPresets(const anki::json& options);
    void presetActions();
    // The desktop's Display Order and Easy Days sections.
    void fillDisplayOrder(const anki::json& options);
    void fillEasyDays(const anki::json& options);
    // Easy Days' values: the weekdays' levels, the fuzz preview and warnings.
    void showEasyDays(const anki::json& options);
    void showEasyDaysNotes();
    // Rows for the chosen schedulers, as the desktop shows them.
    void showSchedulerRows();
    // The deck's own options (Deck only desired retention, subdeck minimums)
    // and this console's RWKV model.
    void showDeckValues();
    std::string deckRetentionText() const;
    void setRwkvModel(anki::json args);
    // The desktop's wording for `key` (the backend's labels), else `fallback`.
    std::string label(const std::string& key, const std::string& fallback) const;
    brls::Label* note(const std::string& text);
    brls::Label* subheading(const std::string& text);
    void collect(size_t from, std::vector<brls::View*>& rows);
    // Send `values` (and/or fsrs, custom_scheduling) to the backend, then show
    // what was saved.
    // `done`: shown once the change is saved.
    void apply(anki::json args, std::string done = "");

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
    void optimize(bool calibrate = false);
    void search(const std::string& title, const std::string& key);
    void showOptimized(const anki::json& result);
    void applyOptimized();
    void toggle(const std::string& title, const std::string& key);
    // A choice among `options` (backend values), shown as `names`.
    void choice(const std::string& title, const std::string& key, const std::vector<std::string>& options,
                const std::vector<std::string>& names);
    // A choice among the backend's `choices[key]` (numbers, with the
    // desktop's words), which can change with other values.
    void orderChoice(const std::string& key);
    void showOrderChoices();
    // A collection-wide number sent as `arg`; `percent`: shown and typed as %.
    void collectionNumber(const std::string& title, const std::string& arg, bool percent);
    std::string collectionNumberText(const std::string& arg) const;

    int64_t deckId;
    anki::json values;
    bool fsrs = false;
    // Value rows, to refresh in place: key -> (cell, shown with decimals).
    std::map<std::string, std::pair<brls::DetailCell*, bool>> details;
    std::map<std::string, brls::BooleanCell*> toggles;
    std::map<std::string, std::pair<brls::SelectorCell*, std::vector<std::string>>> choices;
    // The Review scheduler ("sm2" first while FSRS is off).
    brls::SelectorCell* schedulerCell = nullptr;
    std::vector<std::string> schedulerOptions;
    int schedulerIndex() const;
    // Rows by when they show (showSchedulerRows): SM-2 (FSRS off), Due dates
    // (RWKV-Instant), desired retention (FSRS or RWKV-Curve), the RWKV section
    // and its Instant and Curve parts, the FSRS section, FSRS-7's options.
    std::vector<brls::View*> sm2Rows, dueDateRows, retentionRows, rwkvRows, instantRows, curveRows, fsrsSectionRows,
        fsrs7Rows, dynamicRows, fsrsOnRows, sameDayRows;
    // "Reschedule cards on change", for this screen's changes.
    bool reschedule                = false;
    brls::Label* rescheduleWarning = nullptr;
    // The fork's collection-wide switches (arg -> cell) and the parameters row.
    std::map<std::string, brls::BooleanCell*> collectionToggles;
    int fsrsParams = 0;
    brls::DetailCell* paramsCell = nullptr;
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
    // Easy Days: the weekdays (Monday first), the collection-wide numbers
    // (arg -> cell, percent), the preview and the warnings.
    std::vector<brls::SelectorCell*> easyDayCells;
    std::map<std::string, std::pair<brls::DetailCell*, bool>> collectionNumbers;
    brls::Label* fuzzPreview      = nullptr;
    brls::Label* noNormalDayNote  = nullptr;
    brls::Label* easyDaysChangeNote = nullptr;
    // Changed here (easy days or fuzz): existing reviews keep their dates
    // unless "Reschedule cards on change" is on, as the desktop warns.
    bool easyDaysChanged = false;
    // The collection-wide values as saved (deck_options' answer).
    anki::json collectionValues;
    // The deck's own options, and this console's RWKV model.
    anki::json deck, rwkvModel;
    brls::DetailCell* deckRetentionCell = nullptr;
    brls::BooleanCell* respectSubdeckCell = nullptr;
    brls::DetailCell* applySubdecksCell   = nullptr;
    brls::SelectorCell* rwkvSizeCell = nullptr;
    brls::BooleanCell* singleModelCell = nullptr;
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
