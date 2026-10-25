#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct DeckEntry
{
    int64_t id;
    std::string name;
    int level;
    bool collapsed;
    bool hasChildren;
    int newCount;
    int learnCount;
    int reviewCount;
};

class DeckCell : public brls::RecyclerCell
{
  public:
    DeckCell();
    static DeckCell* create();

    void bind(const DeckEntry& deck);
    std::function<void()> onToggle;
    std::function<void()> onOverview;

  private:
    BRLS_BIND(brls::Label, arrow, "arrow");
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Label, newCount, "new");
    BRLS_BIND(brls::Label, learnCount, "learn");
    BRLS_BIND(brls::Label, reviewCount, "review");
    BRLS_BIND(brls::Box, more, "more");
    BRLS_BIND(brls::Label, moreIcon, "moreIcon");
};

// Main screen: the deck list with due counts, like AnkiDroid's DeckPicker.
class DeckPickerActivity : public brls::Activity
{
  public:
    CONTENT_FROM_XML_RES("activity/deck_picker.xml");

    void onContentAvailable() override;
    void onResume() override;

    const std::vector<DeckEntry>& getEntries() const { return entries; }
    void study(size_t index);
    void toggle(size_t index);
    // R: the deck overview (counts, description, options, custom study).
    void overview(size_t index);
    // Memory Forest is behind the list.
    bool forestOn() const { return forestShown; }

  private:
    void openCollection();
    void refresh();
    void sync();
    void afterSync();
    void openSettings();
    void updateAccountLabel(const std::string& username);
    void showMessage(const std::string& text);
    void fitTable();
    void fitStatusLine();
    void updateDueLabel();
    void updateStreak(int days, bool capped);

    BRLS_BIND(brls::RecyclerFrame, decks, "decks");
    BRLS_BIND(brls::Label, message, "message");
    BRLS_BIND(brls::Label, account, "account");
    BRLS_BIND(brls::Label, studiedToday, "studiedToday");
    BRLS_BIND(brls::Box, streakBox, "streakBox");
    BRLS_BIND(brls::Label, streakIcon, "streakIcon");
    BRLS_BIND(brls::Label, streakLabel, "streakLabel");
    BRLS_BIND(brls::Box, columns, "columns");
    BRLS_BIND(brls::Box, forestBox, "forestBox");
    BRLS_BIND(brls::Label, forestIcon, "forestIcon");
    BRLS_BIND(brls::Label, forestLabel, "forestLabel");
    // "N cards due", under the title in the header.
    brls::Label* dueLabel = nullptr;
    // Average seconds per review (last 30 days), for "~N min"; 0 = unknown.
    float secsPerCard = 0;
    // Dot on the header's sync button: changes not yet on AnkiWeb.
    brls::Box* syncDot = nullptr;
    // Memory Forest behind the screen (view/forest_view.hpp), when it is on:
    // grown again from the collection at each refresh, and its data, which
    // the full-screen forest (L, or a tap on the tree count) draws too.
    class ForestView* forest = nullptr;
    std::string forestData;
    bool forestShown = false;
    void growForest();
    void openForest();
    void updateSyncDot(bool pending);

    std::vector<DeckEntry> entries;
    size_t focusedIndex = 0;
    bool ready          = false;
    // Back from the study screen: sync the reviews (closing from HOME can't).
    bool returningFromStudy = false;
    bool returningFromSettings = false;
    // Woken from sleep: refresh and sync once this screen is showing.
    bool syncAfterWake = false;
    void wakeSync();
};
