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

  private:
    BRLS_BIND(brls::Label, arrow, "arrow");
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Label, newCount, "new");
    BRLS_BIND(brls::Label, learnCount, "learn");
    BRLS_BIND(brls::Label, reviewCount, "review");
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

  private:
    void openCollection();
    void refresh();
    void sync();
    void afterSync();
    void showAccountMenu();
    void updateAccountLabel(const std::string& username);
    void showMessage(const std::string& text);

    BRLS_BIND(brls::RecyclerFrame, decks, "decks");
    BRLS_BIND(brls::Label, message, "message");
    BRLS_BIND(brls::Label, account, "account");

    std::vector<DeckEntry> entries;
    size_t focusedIndex = 0;
    bool ready          = false;
    // Back from the study screen: sync the reviews (closing from HOME can't).
    bool returningFromStudy = false;
};
