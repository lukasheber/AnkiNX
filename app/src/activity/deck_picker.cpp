#include "activity/deck_picker.hpp"

#include "activity/deck_overview.hpp"
#include "activity/reviewer.hpp"
#include "anki/api.hpp"
#include "app.hpp"
#include "activity/settings.hpp"
#include "util/sync_flow.hpp"
#include "util/update.hpp"

#include <algorithm>
#include <functional>

using namespace brls::literals;

namespace
{

const NVGcolor COLOR_NEW    = nvgRGB(66, 165, 245);
const NVGcolor COLOR_LEARN  = nvgRGB(239, 83, 80);
const NVGcolor COLOR_REVIEW = nvgRGB(102, 187, 106);
const NVGcolor COLOR_ZERO   = nvgRGB(128, 128, 128);

void setCount(brls::Label* label, int count, NVGcolor color)
{
    label->setText(std::to_string(count));
    label->setTextColor(count > 0 ? color : COLOR_ZERO);
}

const char* ICON_SYNC     = "\xEE\x98\xA7"; // Material sync
const char* ICON_SETTINGS = "\xEE\xA2\xB8"; // Material settings
const char* ICON_EXIT     = "\xEE\xA1\xB9"; // Material exit_to_app
const char* ICON_CLOUD     = "\xEE\x8A\xBF"; // Material cloud_done
const char* ICON_STREAK    = "\xEE\xA0\x8E"; // Material whatshot (a flame)
const char* ICON_CLOUD_OFF ="\xEE\x8B\x81"; // Material cloud_off

// Width of a line of text in the regular font, as a Label measures it.
float textWidth(const std::string& text, float fontSize)
{
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg)
        return 0;
    float bounds[4];
    nvgSave(vg);
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFontSize(vg, fontSize);
    nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    nvgRestore(vg);
    return bounds[2] - bounds[0];
}

// Secondary text, readable on both themes (borealis' disabled grey is too
// dark on the dark background).
NVGcolor mutedColor()
{
    bool dark = brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;
    return dark ? nvgRGB(185, 185, 185) : nvgRGB(110, 110, 110);
}

// A header button: the controller button's glyph and an icon; tappable.
// dot: a small badge on the icon, hidden until set visible.
brls::Box* headerButton(brls::ControllerButton button, const char* icon, std::function<void()> action,
                        brls::Box** dot = nullptr)
{
    auto* box = new brls::Box(brls::Axis::ROW);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setPadding(4, 10, 4, 10);
    box->setMarginLeft(14);
    auto* key = new brls::Label();
    key->setFontSize(22);
    key->setText(brls::Hint::getKeyIcon(button, true));
    key->setMarginRight(8);
    box->addView(key);
    auto* glyph = new brls::Label();
    glyph->setFontSize(30);
    glyph->setText(icon);
    box->addView(glyph);
    if (dot)
    {
        *dot = new brls::Box();
        (*dot)->setWidth(10);
        (*dot)->setHeight(10);
        (*dot)->setCornerRadius(5);
        (*dot)->setBackgroundColor(nvgRGB(66, 165, 245));
        (*dot)->setPositionType(brls::PositionType::ABSOLUTE);
        (*dot)->setPositionTop(4);
        (*dot)->setPositionRight(6);
        (*dot)->setVisibility(brls::Visibility::INVISIBLE);
        box->addView(*dot);
    }
    box->addGestureRecognizer(new brls::TapGestureRecognizer(box, std::move(action)));
    return box;
}

class DeckDataSource : public brls::RecyclerDataSource
{
  public:
    explicit DeckDataSource(DeckPickerActivity* activity)
        : activity(activity)
    {
    }

    int numberOfSections(brls::RecyclerFrame*) override { return 1; }

    int numberOfRows(brls::RecyclerFrame*, int) override { return (int)activity->getEntries().size(); }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override
    {
        auto* cell = (DeckCell*)recycler->dequeueReusableCell("Deck");
        size_t row = index.row;
        cell->bind(activity->getEntries()[row]);
        cell->onToggle   = [this, row] { activity->toggle(row); };
        cell->onOverview = [this, row] { activity->overview(row); };
        return cell;
    }

    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override { activity->study(index.row); }

  private:
    DeckPickerActivity* activity;
};

} // namespace

DeckCell::DeckCell()
{
    this->inflateFromXMLRes("xml/cells/deck_cell.xml");
    this->registerAction("ankinx/decks/toggle"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        if (onToggle)
            onToggle();
        return true;
    });
    this->registerAction("ankinx/decks/overview"_i18n, brls::BUTTON_RB, [this](brls::View*) {
        if (onOverview)
            onOverview();
        return true;
    });
    this->updateActionHint(brls::BUTTON_A, "ankinx/decks/study"_i18n);
    moreIcon->setText("\xEE\x97\x93"); // Material more_horiz
    more->addGestureRecognizer(new brls::TapGestureRecognizer(more, [this] {
        if (onOverview)
            onOverview();
    }));
}

DeckCell* DeckCell::create() { return new DeckCell(); }

void DeckCell::bind(const DeckEntry& deck)
{
    // Indent subdecks; ▶ collapsed / ▼ expanded parents.
    arrow->setMarginLeft((float)(deck.level - 1) * 32.0f);
    arrow->setText(deck.hasChildren ? (deck.collapsed ? "▶" : "▼") : "");
    name->setText(deck.name);
    setCount(newCount, deck.newCount, COLOR_NEW);
    setCount(learnCount, deck.learnCount, COLOR_LEARN);
    setCount(reviewCount, deck.reviewCount, COLOR_REVIEW);
    // Y says what it would do here; greyed out on decks without subdecks.
    this->updateActionHint(brls::BUTTON_Y,
                           deck.hasChildren && !deck.collapsed ? "ankinx/decks/collapse"_i18n
                                                               : "ankinx/decks/expand"_i18n);
    this->setActionAvailable(brls::BUTTON_Y, deck.hasChildren);
}

void DeckPickerActivity::onContentAvailable()
{
    decks->estimatedRowHeight = 64;
    decks->registerCell("Deck", [] { return DeckCell::create(); });
    decks->setDataSource(new DeckDataSource(this));

    auto* frame = (brls::AppletFrame*)this->getView("frame");
    frame->setTitle("ankinx/title"_i18n);
    // A second, smaller line under the title: the cards due today, as in
    // AnkiDroid's toolbar.
    if (auto* title = dynamic_cast<brls::Label*>(frame->getView("brls/applet_frame/title_label")))
    {
        if (auto* titleBox = dynamic_cast<brls::Box*>(title->getParent()))
        {
            titleBox->setAxis(brls::Axis::COLUMN);
            titleBox->setJustifyContent(brls::JustifyContent::CENTER);
            titleBox->setPaddingTop(0);
            titleBox->setPaddingBottom(0);
            title->setMarginTop(0);
            dueLabel = new brls::Label();
            dueLabel->setFontSize(16);
            dueLabel->setMarginTop(2);
            dueLabel->setTextColor(mutedColor());
            dueLabel->setVisibility(brls::Visibility::INVISIBLE);
            titleBox->addView(dueLabel);
        }
    }
    account->setTextColor(mutedColor());
    studiedToday->setTextColor(mutedColor());
    streakLabel->setTextColor(mutedColor());
    streakIcon->setText(ICON_STREAK);
    fitTable();
    // This is the root screen: B must not quit by accident (+ quits).
    frame->registerAction("", brls::BUTTON_B, [](brls::View*) { return true; }, true);
    // Screen-wide actions sit in the header, on the right (their buttons, or
    // a tap); the footer keeps what applies to the highlighted deck.
    frame->registerAction("ankinx/decks/sync"_i18n, brls::BUTTON_X, [this](brls::View*) {
        this->sync();
        return true;
    }, true);
    frame->registerAction("ankinx/settings/title"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
        this->openSettings();
        return true;
    }, true);
    app::registerQuit(frame, true);
    if (auto* hintBox = dynamic_cast<brls::Box*>(frame->getView("brls/applet_frame/hint_box")))
    {
        hintBox->setMarginTop(0);
        hintBox->setAlignSelf(brls::AlignSelf::CENTER);
        hintBox->setAlignItems(brls::AlignItems::CENTER);
        hintBox->addView(headerButton(brls::BUTTON_X, ICON_SYNC, [this] { sync(); }, &syncDot));
        hintBox->addView(headerButton(brls::BUTTON_BACK, ICON_SETTINGS, [this] { openSettings(); }));
        hintBox->addView(headerButton(brls::BUTTON_START, ICON_EXIT, [] { app::quit(); }));
    }

    openCollection();
}

void DeckPickerActivity::onResume()
{
    // Also called when a dialog closes (dialogs are activities).
    if (!ready)
        return;
    if (returningFromSettings)
    {
        // The account may have changed there.
        returningFromSettings = false;
        afterSync();
    }
    else
        refresh();
    if (returningFromStudy)
    {
        // Due counts have changed; upload the reviews if automatic sync is on.
        returningFromStudy = false;
        sync_flow::autoSync([this] { afterSync(); }, sync_flow::Mode::Automatic, false);
    }
}

void DeckPickerActivity::openCollection()
{
    showMessage("ankinx/decks/opening"_i18n);
    std::string locale = brls::Application::getPlatform()->getLocale();
    brls::async([this, locale] {
        try
        {
            anki::json langs = { locale, locale.substr(0, locale.find('-')), "en" };
            anki::call("init", { { "base_dir", anki::dataDir() }, { "langs", langs } });
            anki::json status = anki::call("open");
            std::string username = status["username"].is_string() ? status["username"].get<std::string>() : "";
            brls::sync([this, username] {
                ready = true;
                updateAccountLabel(username);
                refresh();
                // Bring in what was studied on other devices, as Anki desktop
                // does on startup.
                sync_flow::autoSync([this] { afterSync(); }, sync_flow::Mode::Automatic, true);
                update::checkInBackground();
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = "ankinx/decks/open_failed"_i18n + "\n\n" + e.what();
            brls::sync([this, text] { showMessage(text); });
        }
    });
}

void DeckPickerActivity::refresh()
{
    brls::async([this] {
        try
        {
            anki::json tree = anki::call("deck_tree");
            std::vector<DeckEntry> list;
            for (const auto& d : tree["decks"])
                list.push_back(DeckEntry {
                    d["id"].get<int64_t>(), d["name"].get<std::string>(), d["level"].get<int>(),
                    d["collapsed"].get<bool>(), d["has_children"].get<bool>(), d["new"].get<int>(),
                    d["learn"].get<int>(), d["review"].get<int>() });
            // Anki's offline check (no network): reviews or edits not uploaded.
            anki::json status    = anki::call("status");
            const auto& needed   = status["sync_needed"];
            bool pending         = status["username"].is_string() && needed.is_string() && needed != "none";
            std::string studied = tree.value("studied_today", "");
            int streak          = tree.value("streak", 0);
            bool streakCapped   = tree.value("streak_capped", false);
            float pace          = tree.value("secs_per_card", 0.0f);
            brls::sync([this, list, pending, studied, streak, streakCapped, pace] {
                entries     = list;
                secsPerCard = pace;
                studiedToday->setText(studied.empty() ? "ankinx/decks/none_studied"_i18n : studied);
                updateStreak(streak, streakCapped);
                fitStatusLine();
                updateSyncDot(pending);
                updateDueLabel();
                fitTable();
                if (entries.empty())
                {
                    showMessage("ankinx/decks/empty"_i18n);
                    return;
                }
                message->setVisibility(brls::Visibility::GONE);
                decks->setVisibility(brls::Visibility::VISIBLE);
                focusedIndex = std::min(focusedIndex, entries.size() - 1);
                decks->setDefaultCellFocus(brls::IndexPath(0, focusedIndex));
                decks->reloadData();
                // The list was hidden while loading, so nothing had focus.
                if (brls::Application::getActivitiesStack().back() == this)
                    brls::Application::giveFocus(decks);
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            brls::sync([this, text] { showMessage(text); });
        }
    });
}

void DeckPickerActivity::study(size_t index)
{
    if (index >= entries.size())
        return;
    focusedIndex = index;
    const DeckEntry& deck = entries[index];
    returningFromStudy    = true;
    brls::Application::pushActivity(new ReviewerActivity(deck.id, deck.name));
}

void DeckPickerActivity::overview(size_t index)
{
    if (index >= entries.size())
        return;
    focusedIndex = index;
    // Studying usually follows: refresh and sync on the way back, as after study.
    returningFromStudy = true;
    brls::Application::pushActivity(new DeckOverviewActivity(entries[index].id));
}

void DeckPickerActivity::toggle(size_t index)
{
    if (index >= entries.size() || !entries[index].hasChildren)
        return;
    focusedIndex = index;
    int64_t id     = entries[index].id;
    bool collapsed = !entries[index].collapsed;
    brls::async([this, id, collapsed] {
        try
        {
            anki::call("set_collapsed", { { "deck_id", id }, { "collapsed", collapsed } });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            brls::sync([text] { brls::Application::notify(text); });
        }
        brls::sync([this] { refresh(); });
    });
}

void DeckPickerActivity::sync()
{
    if (!ready)
        return;
    sync_flow::start([this] { afterSync(); });
}

void DeckPickerActivity::afterSync()
{
    try
    {
        anki::json status    = anki::call("status");
        std::string username = status["username"].is_string() ? status["username"].get<std::string>() : "";
        updateAccountLabel(username);
    }
    catch (const anki::Error&)
    {
    }
    refresh();
}

// Settings (account, controls, about), docs/ux-plan.md section 1.
void DeckPickerActivity::openSettings()
{
    if (!ready)
        return;
    returningFromSettings = true;
    brls::Application::pushActivity(new SettingsActivity());
}

void DeckPickerActivity::updateAccountLabel(const std::string& username)
{
    // Short, so it never runs into the centred study line: an icon and the
    // username.
    account->setText(username.empty() ? std::string(ICON_CLOUD_OFF) + "  " + "ankinx/decks/not_logged_in"_i18n
                                      : std::string(ICON_CLOUD) + "  " + username);
    fitStatusLine();
}

// The account (left) stops short of the centred study line: a long email
// ends in "…" instead of running into it.
void DeckPickerActivity::fitStatusLine()
{
    const float LEFT = 65, GAP = 24;
    float studied    = textWidth(studiedToday->getFullText(), 17);
    float room       = (brls::Application::contentWidth - studied) / 2 - LEFT - GAP;
    float needed     = textWidth(account->getFullText(), 16) + 2;
    account->setWidth(std::max(80.0f, std::min(needed, room)));
}

void DeckPickerActivity::updateSyncDot(bool pending)
{
    if (syncDot)
        syncDot->setVisibility(pending ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
}

// Top-level decks' counts already include their subdecks' (within limits).
void DeckPickerActivity::updateDueLabel()
{
    if (!dueLabel)
        return;
    int due = 0;
    for (const auto& deck : entries)
        if (deck.level == 1)
            due += deck.newCount + deck.learnCount + deck.reviewCount;
    std::string text = due == 0 ? "ankinx/decks/due_none"_i18n
                                : brls::getStr(due == 1 ? "ankinx/decks/due_one" : "ankinx/decks/due_other",
                                               std::to_string(due));
    // "~N min" at the recent pace, as AnkiDroid shows (learning cards come
    // back, so it is a rough guide).
    if (due > 0 && secsPerCard > 0)
    {
        int minutes = (int)(due * secsPerCard / 60 + 0.5f);
        text += "  ·  " + (minutes < 1 ? "ankinx/decks/estimate_short"_i18n
                                       : brls::getStr("ankinx/decks/estimate", std::to_string(minutes)));
    }
    dueLabel->setText(text);
    dueLabel->setVisibility(brls::Visibility::VISIBLE);
}

// Days in a row with reviews (today counts once studied); hidden at 0.
void DeckPickerActivity::updateStreak(int days, bool capped)
{
    streakBox->setVisibility(days > 0 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    std::string number = std::to_string(days) + (capped ? "+" : "");
    streakLabel->setText(brls::getStr(days == 1 ? "ankinx/decks/streak_one" : "ankinx/decks/streak_other", number));
}

// The table is only as wide as the longest deck name (with its indent)
// needs, centred on the screen, so the counts stay close to the names.
void DeckPickerActivity::fitTable()
{
    // Widths from deck_cell.xml: padding 20 + arrow 36 ... counts 3 × 90 +
    // ⋯ 60 + padding 10.
    const float FIXED = 20 + 36 + 3 * 90 + 60 + 10;
    const float GAP   = 48; // between the name and the first count
    float names       = 0;
    for (const auto& deck : entries)
        names = std::max(names, (float)(deck.level - 1) * 32.0f + textWidth(deck.name, 22));
    float width = std::clamp(FIXED + names + GAP, 760.0f, 1200.0f);
    decks->setWidth(width);
    columns->setWidth(width);
}

void DeckPickerActivity::showMessage(const std::string& text)
{
    message->setText(text);
    message->setVisibility(brls::Visibility::VISIBLE);
    decks->setVisibility(brls::Visibility::GONE);
    // The message holds the focus while the list is hidden. A dialog opened
    // meanwhile (automatic sync on open, an update offer) then has a view to
    // give the focus back to when it closes; with none, borealis left the
    // focus on the closed dialog's deleted button and the next focus change
    // crashed.
    // (At start-up this runs before the activity is on the stack; pushing it
    // then focuses its first focusable view, the message.)
    message->setFocusable(true);
    message->setHideHighlight(true);
    const auto& stack = brls::Application::getActivitiesStack();
    if (!stack.empty() && stack.back() == this)
        brls::Application::giveFocus(message);
}
