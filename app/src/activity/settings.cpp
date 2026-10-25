#include "activity/settings.hpp"

#include "activity/stats.hpp"
#include "anki/api.hpp"
#include "anki_switch_core.h"
#include "app.hpp"
#include "util/forest.hpp"
#include "util/keyboard.hpp"
#include "util/settings.hpp"
#include "util/sync_flow.hpp"
#include "util/update.hpp"

using namespace brls::literals;

namespace
{

// A scrolling column of cells, as in the Switch's System Settings.
brls::Box* column(brls::ScrollingFrame** scroller)
{
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(20, 40, 30, 40);
    *scroller = new brls::ScrollingFrame();
    (*scroller)->setContentView(box);
    (*scroller)->setGrow(1.0f);
    return box;
}

brls::Header* header(const std::string& title)
{
    auto* h = new brls::Header();
    h->setTitle(title);
    return h;
}

brls::DetailCell* detail(const std::string& title, const std::string& value)
{
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(value);
    return cell;
}

std::string accountText()
{
    try
    {
        anki::json status = anki::call("status");
        if (status["username"].is_string())
            return status["username"].get<std::string>();
    }
    catch (const anki::Error&)
    {
    }
    return "ankinx/settings/not_logged_in"_i18n;
}

std::string layoutName(controls::Layout layout)
{
    return layout == controls::Layout::OneJoyCon ? "ankinx/controls/layout_one_joycon"_i18n
                                                 : "ankinx/controls/layout_standard"_i18n;
}

} // namespace

SettingsActivity::SettingsActivity() = default;

SettingsActivity::~SettingsActivity() { *alive = false; }

void SettingsActivity::onContentAvailable()
{
    frame->setTitle("ankinx/settings/title"_i18n);
    app::hideStatus(frame); // no + here: quit from the deck list
    tabs->addTab("ankinx/settings/tab_account"_i18n, [this] { return accountTab(); });
    tabs->addTab("ankinx/settings/tab_controls"_i18n, [this] { return controlsTab(); });
    tabs->addTab("ankinx/settings/tab_study"_i18n, [this] { return studyTab(); });
    tabs->addTab("ankinx/stats/title"_i18n, [this] { return statsTab(); });
    tabs->addTab("ankinx/settings/tab_collection"_i18n, [this] { return collectionTab(); });
    tabs->addTab("ankinx/settings/tab_about"_i18n, [this] { return aboutTab(); });
}

brls::View* SettingsActivity::accountTab()
{
    brls::ScrollingFrame* scroller;
    brls::Box* box = column(&scroller);
    box->addView(header("ankinx/settings/ankiweb"_i18n));

    auto* account = detail("ankinx/settings/account"_i18n, accountText());
    account->registerClickAction([account, token = alive](brls::View*) {
        anki::json status;
        try
        {
            status = anki::call("status");
        }
        catch (const anki::Error&)
        {
            return true;
        }
        if (!status["username"].is_string())
        {
            sync_flow::login([account, token](bool) {
                if (*token)
                    account->setDetailText(accountText());
            });
            return true;
        }
        auto* dialog = new brls::Dialog("ankinx/decks/logged_in_as"_i18n + " " + accountText());
        dialog->addButton("hints/cancel"_i18n, [] {});
        dialog->addButton("ankinx/decks/logout"_i18n, [account, token] {
            sync_flow::logout();
            if (*token)
                account->setDetailText(accountText());
        });
        dialog->open();
        return true;
    });
    box->addView(account);

    // A self-hosted sync server instead of AnkiWeb (as Anki's preferences):
    // used at the next login, so changing it logs out.
    auto serverText = [] {
        anki::json server = settings::get("sync_server");
        return server.is_string() ? server.get<std::string>() : "ankinx/settings/ankiweb_default"_i18n;
    };
    auto* server = detail("ankinx/settings/sync_server"_i18n, serverText());
    server->registerClickAction([server, account, serverText, token = alive](brls::View*) {
        anki::json current = settings::get("sync_server");
        keyboard::text("ankinx/settings/sync_server_prompt"_i18n, current.is_string() ? current.get<std::string>() : "",
                       200, [server, account, serverText, token](const std::string& text) {
                           std::string url = text;
                           while (!url.empty() && url.back() == ' ')
                               url.pop_back();
                           if (!url.empty() && url.back() != '/')
                               url += '/'; // rslib joins "sync/..." onto it
                           anki::json before = settings::get("sync_server");
                           if (before.is_string() ? before.get<std::string>() == url : url.empty())
                               return;
                           settings::set("sync_server", url.empty() ? anki::json() : anki::json(url));
                           sync_flow::logout();
                           brls::Application::notify("ankinx/settings/sync_server_changed"_i18n);
                           if (!*token)
                               return;
                           server->setDetailText(serverText());
                           account->setDetailText(accountText());
                       });
        return true;
    });
    box->addView(server);

    auto* autoSync = new brls::BooleanCell();
    autoSync->init("ankinx/settings/auto_sync"_i18n, settings::autoSync(), [](bool on) { settings::setAutoSync(on); });
    box->addView(autoSync);

    auto* syncNow = detail("ankinx/settings/sync_now"_i18n, "");
    syncNow->registerClickAction([account, token = alive](brls::View*) {
        sync_flow::start([account, token] {
            if (*token)
                account->setDetailText(accountText());
        });
        return true;
    });
    box->addView(syncNow);
    return scroller;
}

brls::View* SettingsActivity::controlsTab()
{
    brls::ScrollingFrame* scroller;
    brls::Box* box = column(&scroller);
    box->addView(header("ankinx/controls/layout"_i18n));

    // Automatic says what it detected, e.g. "Automatic (One Joy-Con)".
    std::string detected = layoutName(controls::oneJoyConDetected() ? controls::Layout::OneJoyCon
                                                                    : controls::Layout::Standard);
    auto* layout = new brls::SelectorCell();
    layout->init("ankinx/controls/layout"_i18n,
                 { "ankinx/controls/layout_automatic"_i18n + " (" + detected + ")",
                   "ankinx/controls/layout_standard"_i18n, "ankinx/controls/layout_one_joycon"_i18n },
                 (int)controls::layoutChoice(), [this, token = alive](int selected) {
                     controls::setLayoutChoice((controls::LayoutChoice)selected);
                     // Rebuild after the selector's dropdown has closed.
                     brls::sync([this, token] {
                         if (*token)
                             fillBindings();
                     });
                 });
    box->addView(layout);

    box->addView(detail("ankinx/controls/detected"_i18n, controls::describeControllers()));

    auto* grip = detail("ankinx/controls/change_grip"_i18n, "");
    grip->registerClickAction([this, token = alive](brls::View*) {
        controls::showControllerSupport();
        if (*token)
            fillBindings(); // the layout in use may have changed
        return true;
    });
    box->addView(grip);

    // A new tab: new rows (the old ones went with the old tab).
    bindingsHeader = nullptr;
    bindingCells.clear();
    bindingsBox = new brls::Box(brls::Axis::COLUMN);
    box->addView(bindingsBox);
    fillBindings();
    return scroller;
}

void SettingsActivity::fillBindings()
{
    if (!bindingsBox)
        return;
    controls::Layout layout = controls::activeLayout();
    // Built once, then updated in place: rebuilding would delete the focused
    // row (and the reset row while its own action runs).
    if (bindingsHeader)
    {
        bindingsHeader->setTitle("ankinx/controls/buttons"_i18n + ": " + layoutName(layout));
        for (auto& [action, cell] : bindingCells)
            cell->setDetailText(controls::glyph(controls::binding(layout, action)));
        return;
    }
    bindingsHeader = header("ankinx/controls/buttons"_i18n + ": " + layoutName(layout));
    bindingsBox->addView(bindingsHeader);
    for (controls::Action action : controls::allActions())
    {
        auto* cell = detail(brls::getStr(controls::actionName(action)),
                            controls::glyph(controls::binding(layout, action)));
        cell->registerClickAction([this, action](brls::View*) {
            captureButton(action);
            return true;
        });
        bindingCells.emplace_back(action, cell);
        bindingsBox->addView(cell);
    }
    auto* reset = detail("ankinx/controls/reset"_i18n, "");
    reset->registerClickAction([this](brls::View*) {
        controls::resetBindings(controls::activeLayout());
        fillBindings();
        brls::Application::notify("ankinx/controls/reset_done"_i18n);
        return true;
    });
    bindingsBox->addView(reset);
}

// "Press a button for Again": the next bindable button pressed is taken;
// B cancels. A taken button swaps with this action's old one.
void SettingsActivity::captureButton(controls::Action action)
{
    controls::Layout layout = controls::activeLayout();
    std::string name        = brls::getStr(controls::actionName(action));

    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setPadding(40, 40, 40, 40);
    content->setFocusable(true);
    content->setHideHighlight(true);
    auto* label = new brls::Label();
    label->setText(brls::getStr("ankinx/controls/press_button", name));
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    content->addView(label);

    auto* dialog = new brls::Dialog(content);
    for (brls::ControllerButton button : controls::bindableButtons())
        content->registerAction("", button, [this, dialog, layout, action, button, token = alive](brls::View*) {
            controls::Action swapped = controls::setBinding(layout, action, button);
            dialog->close([this, swapped, token] {
                if (!*token)
                    return;
                fillBindings();
                if (swapped != controls::Action::_Count)
                    brls::Application::notify(
                        brls::getStr("ankinx/controls/swapped", brls::getStr(controls::actionName(swapped))));
            });
            return true;
        }, true);
    dialog->setCancelable(true);
    dialog->open();
}

brls::View* SettingsActivity::studyTab()
{
    brls::ScrollingFrame* scroller;
    brls::Box* box = column(&scroller);
    box->addView(header("ankinx/settings/cards"_i18n));
    auto* scripts = new brls::BooleanCell();
    scripts->init("ankinx/settings/card_scripts"_i18n, settings::cardScripts(),
                  [](bool on) { settings::setCardScripts(on); });
    box->addView(scripts);

    box->addView(header("ankinx/settings/answering"_i18n));
    auto* swipes = new brls::BooleanCell();
    swipes->init("ankinx/settings/swipe_answers"_i18n, settings::get("swipe_answers") == true,
                 [](bool on) { settings::set("swipe_answers", on); });
    box->addView(swipes);
    static const char* ZONES[] = { "off", "sides", "four" };
    anki::json zones = settings::get("tap_zones");
    auto* tapZones   = new brls::SelectorCell();
    tapZones->init("ankinx/settings/tap_zones"_i18n,
                   { "ankinx/settings/tap_zones_off"_i18n, "ankinx/settings/tap_zones_sides"_i18n,
                     "ankinx/settings/tap_zones_four"_i18n },
                   zones == "sides" ? 1 : zones == "four" ? 2 : 0, [](int index) {
                       if (index >= 0 && index < 3)
                           settings::set("tap_zones", ZONES[index]);
                   });
    box->addView(tapZones);
    auto* rumble = new brls::BooleanCell();
    rumble->init("ankinx/settings/rumble"_i18n, settings::get("rumble") == true,
                 [](bool on) { settings::set("rumble", on); });
    box->addView(rumble);

    // Docked: card text a size up by default, for reading from the couch.
    box->addView(header("ankinx/settings/tv"_i18n));
    static const float SCALES[] = { 1.0f, 1.25f, 1.5f };
    float scale  = settings::tvCardScale();
    int selected = scale < 1.1f ? 0 : scale < 1.4f ? 1 : 2;
    auto* tv     = new brls::SelectorCell();
    tv->init("ankinx/settings/tv_card_size"_i18n,
             { "ankinx/settings/tv_same"_i18n, "ankinx/settings/tv_larger"_i18n, "ankinx/settings/tv_largest"_i18n },
             selected, [](int index) {
                 if (index >= 0 && index < 3)
                     settings::set("tv_card_scale", SCALES[index]);
             });
    box->addView(tv);
    return scroller;
}

// Statistics for the whole collection (each deck's are on its overview, R).
brls::View* SettingsActivity::statsTab()
{
    brls::ScrollingFrame* scroller;
    brls::Box* box = column(&scroller);
    box->addView(header("ankinx/stats/title"_i18n));
    auto* all = detail("ankinx/stats/all_decks"_i18n, "");
    all->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new StatsActivity(0, "ankinx/stats/all_decks"_i18n));
        return true;
    });
    box->addView(all);
    auto* note = new brls::Label();
    note->setText("ankinx/stats/deck_hint"_i18n);
    note->setFontSize(16);
    note->setMarginTop(8);
    note->setMarginLeft(16);
    box->addView(note);
    return scroller;
}

// Check Database and backups, as in Anki's Tools menu and AnkiDroid's
// Advanced settings.
brls::View* SettingsActivity::collectionTab()
{
    brls::ScrollingFrame* scroller;
    brls::Box* box = column(&scroller);
    box->addView(header("ankinx/settings/maintenance"_i18n));

    auto* check = detail("ankinx/settings/check_database"_i18n, "");
    check->registerClickAction([](brls::View*) {
        sync_flow::blocking(
            "ankinx/settings/checking"_i18n, [] { return anki::call("check_database"); },
            [](const anki::json* result, const anki::Error* error) {
                sync_flow::showMessage(error ? std::string(error->what()) : result->value("message", ""));
            });
        return true;
    });
    box->addView(check);

    box->addView(header("ankinx/settings/backups"_i18n));
    auto countText = [] {
        try
        {
            size_t n = anki::call("list_backups")["backups"].size();
            if (n == 0)
                return "ankinx/settings/no_backups"_i18n;
            return brls::getStr(n == 1 ? "ankinx/settings/backup_count_one" : "ankinx/settings/backup_count",
                                std::to_string(n));
        }
        catch (const anki::Error&)
        {
            return std::string();
        }
    };
    auto* restore = detail("ankinx/settings/restore"_i18n, countText());

    auto* backupNow = detail("ankinx/settings/backup_now"_i18n, "");
    backupNow->registerClickAction([restore, countText, token = alive](brls::View*) {
        sync_flow::blocking(
            "ankinx/settings/backing_up"_i18n, [] { return anki::call("backup_now"); },
            [restore, countText, token](const anki::json*, const anki::Error* error) {
                if (error)
                    return sync_flow::showMessage(error->what());
                brls::Application::notify("ankinx/settings/backup_done"_i18n);
                if (*token)
                    restore->setDetailText(countText());
            });
        return true;
    });
    box->addView(backupNow);

    // Pick a backup (newest first), confirm, then replace the collection.
    restore->registerClickAction([](brls::View*) {
        anki::json backups;
        try
        {
            backups = anki::call("list_backups")["backups"];
        }
        catch (const anki::Error& e)
        {
            sync_flow::showMessage(e.what());
            return true;
        }
        if (backups.empty())
        {
            brls::Application::notify("ankinx/settings/no_backups"_i18n);
            return true;
        }
        std::vector<std::string> labels;
        for (const auto& b : backups)
            labels.push_back(b.value("label", ""));
        auto* dropdown = new brls::Dropdown("ankinx/settings/restore"_i18n, labels, [backups](int index) {
            if (index < 0 || index >= (int)backups.size())
                return;
            std::string name  = backups[index].value("name", "");
            std::string label = backups[index].value("label", "");
            // After the menu has closed.
            brls::sync([name, label] {
                auto* dialog = new brls::Dialog(brls::getStr("ankinx/settings/restore_confirm", label));
                dialog->addButton("hints/cancel"_i18n, [] {});
                dialog->addButton("ankinx/settings/restore_button"_i18n, [name] {
                    sync_flow::blocking(
                        "ankinx/settings/restoring"_i18n,
                        [name] { return anki::call("restore_backup", { { "name", name } }); },
                        [](const anki::json*, const anki::Error* error) {
                            if (error)
                                sync_flow::showMessage(error->what());
                            else
                                sync_flow::showMessage("ankinx/settings/restored"_i18n);
                        });
                });
                dialog->open();
            });
        });
        brls::Application::pushActivity(new brls::Activity(app::withoutStatus(dropdown)));
        return true;
    });
    box->addView(restore);

    auto* note = new brls::Label();
    note->setFontSize(16);
    note->setMarginTop(16);
    note->setTextColor(brls::Application::getTheme()["brls/text"]);
    note->setText(brls::getStr("ankinx/settings/backups_note", anki::dataDir() + "/backups"));
    box->addView(note);
    return scroller;
}

brls::View* SettingsActivity::aboutTab()
{
    brls::ScrollingFrame* scroller;
    brls::Box* box = column(&scroller);
    box->addView(header("AnkiNX"));

    // The language (also of Anki's own messages): the console's, or one of
    // the translations. borealis reads it at start, so it applies after a restart.
    static const char* CODES[] = { "", "en-US", "pt-BR", "es", "ja" };
    std::vector<std::string> names = { "ankinx/settings/language_auto"_i18n, "English", "Português (Brasil)", "Español",
                                       "日本語" };
    anki::json chosen = settings::get("language");
    int selected      = 0;
    for (int i = 1; i < 5; i++)
        if (chosen.is_string() && chosen.get<std::string>() == CODES[i])
            selected = i;
    auto* language = new brls::SelectorCell();
    language->init("ankinx/settings/language"_i18n, names, selected, [](int index) {
        if (index < 0 || index > 4)
            return;
        settings::set("language", index ? anki::json(CODES[index]) : anki::json());
        brls::Application::notify("ankinx/settings/language_restart"_i18n);
    });
    box->addView(language);

    // Light or dark: the console's, or always one of them.
    static const char* THEMES[] = { "system", "light", "dark" };
    std::string theme = settings::theme();
    auto* themeCell   = new brls::SelectorCell();
    themeCell->init("ankinx/settings/theme"_i18n,
                    { "ankinx/settings/theme_system"_i18n, "ankinx/settings/theme_light"_i18n,
                      "ankinx/settings/theme_dark"_i18n },
                    theme == "light" ? 1 : theme == "dark" ? 2 : 0, [](int index) {
                        if (index >= 0 && index < 3)
                            settings::setTheme(THEMES[index]);
                    });
    box->addView(themeCell);

    // Memory Forest behind the deck list: off, or one of its sceneries (by
    // their names in the add-on, which are English).
    const auto& all                    = forest::sceneries();
    std::vector<std::string> sceneries = { "ankinx/settings/forest_off"_i18n };
    int forestIndex                    = 0;
    for (size_t i = 0; i < all.size(); i++)
    {
        sceneries.push_back(all[i].label);
        if (settings::forest() == all[i].key)
            forestIndex = (int)i + 1;
    }
    auto* forestCell = new brls::SelectorCell();
    forestCell->init("ankinx/settings/forest"_i18n, sceneries, forestIndex, [](int index) {
        const auto& all = forest::sceneries();
        if (index >= 0 && index <= (int)all.size())
            settings::setForest(index ? all[index - 1].key : "");
    });
    box->addView(forestCell);

    box->addView(detail("ankinx/settings/version"_i18n, ANKINX_VERSION));

    // Updates from GitHub releases: what the last check found; A checks now
    // (or offers the newer version).
    auto updateText = [] {
        const update::Info& info = update::info();
        if (!info.checked)
            return std::string();
        return info.newer ? brls::getStr("ankinx/update/available_short", info.latest) : "ankinx/update/latest"_i18n;
    };
    auto* updates = detail("ankinx/update/title"_i18n, updateText());
    updates->registerClickAction([updates, updateText, token = alive](brls::View*) {
        if (update::info().newer)
            update::offer();
        else
            update::checkNow([updates, updateText, token] {
                if (*token)
                    updates->setDetailText(updateText());
            });
        return true;
    });
    box->addView(updates);
    auto* autoCheck = new brls::BooleanCell();
    autoCheck->init("ankinx/update/auto_check"_i18n, settings::get("check_updates") != false,
                    [](bool on) { settings::set("check_updates", on); });
    box->addView(autoCheck);

    box->addView(detail("ankinx/settings/backend"_i18n, std::string("Anki ") + anki_buildhash()));
    box->addView(detail("ankinx/settings/data_folder"_i18n, anki::dataDir()));
    box->addView(detail("ankinx/settings/licence"_i18n, "AGPL-3.0-or-later"));
    box->addView(detail("ankinx/settings/source"_i18n, "github.com/lukasheber/AnkiNX"));
    return scroller;
}
