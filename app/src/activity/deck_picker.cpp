#include "activity/deck_picker.hpp"

#include "activity/reviewer.hpp"
#include "anki/api.hpp"
#include "app.hpp"
#include "util/settings.hpp"
#include "util/sync_flow.hpp"

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
        cell->onToggle = [this, row] { activity->toggle(row); };
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
}

void DeckPickerActivity::onContentAvailable()
{
    decks->estimatedRowHeight = 64;
    decks->registerCell("Deck", [] { return DeckCell::create(); });
    decks->setDataSource(new DeckDataSource(this));

    auto* frame = (brls::AppletFrame*)this->getView("frame");
    frame->setTitle("ankinx/title"_i18n);
    // This is the root screen: B must not quit by accident (+ quits).
    frame->registerAction("", brls::BUTTON_B, [](brls::View*) { return true; }, true);
    frame->registerAction("ankinx/decks/sync"_i18n, brls::BUTTON_X, [this](brls::View*) {
        this->sync();
        return true;
    });
    frame->registerAction("ankinx/decks/account"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
        this->showAccountMenu();
        return true;
    });
    app::registerQuit(frame);

    openCollection();
}

void DeckPickerActivity::onResume()
{
    // Also called when a dialog closes (dialogs are activities).
    if (!ready)
        return;
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
            brls::sync([this, list] {
                entries = list;
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

void DeckPickerActivity::showAccountMenu()
{
    if (!ready)
        return;
    std::string username;
    try
    {
        anki::json status = anki::call("status");
        if (status["username"].is_string())
            username = status["username"].get<std::string>();
    }
    catch (const anki::Error&)
    {
    }

    std::string text = username.empty() ? "ankinx/decks/not_logged_in"_i18n
                                        : "ankinx/decks/logged_in_as"_i18n + " " + username;
    auto* dialog = new brls::Dialog(text + "\n\nAnkiNX " ANKINX_VERSION);
    dialog->addButton("hints/cancel"_i18n, [] {});
    if (username.empty())
        dialog->addButton("ankinx/decks/login"_i18n, [this] {
            sync_flow::login([this](bool loggedIn) {
                if (loggedIn)
                    this->sync();
            });
        });
    else
        dialog->addButton("ankinx/decks/logout"_i18n, [this] {
            sync_flow::logout();
            updateAccountLabel("");
        });
    bool autoSync = settings::autoSync();
    dialog->addButton(autoSync ? "ankinx/decks/auto_sync_off"_i18n : "ankinx/decks/auto_sync_on"_i18n,
                      [autoSync] {
                          settings::setAutoSync(!autoSync);
                          brls::Application::notify(!autoSync ? "ankinx/decks/auto_sync_enabled"_i18n
                                                              : "ankinx/decks/auto_sync_disabled"_i18n);
                      });
    dialog->open();
}

void DeckPickerActivity::updateAccountLabel(const std::string& username)
{
    account->setText(username.empty() ? "ankinx/decks/not_logged_in"_i18n
                                      : "ankinx/decks/logged_in_as"_i18n + " " + username);
}

void DeckPickerActivity::showMessage(const std::string& text)
{
    message->setText(text);
    message->setVisibility(brls::Visibility::VISIBLE);
    decks->setVisibility(brls::Visibility::GONE);
}
