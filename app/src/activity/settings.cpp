#include "activity/settings.hpp"

#include "anki/api.hpp"
#include "anki_switch_core.h"
#include "app.hpp"
#include "util/settings.hpp"
#include "util/sync_flow.hpp"

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
    app::registerQuit(frame);
    tabs->addTab("ankinx/settings/tab_account"_i18n, [this] { return accountTab(); });
    tabs->addTab("ankinx/settings/tab_controls"_i18n, [this] { return controlsTab(); });
    tabs->addTab("ankinx/settings/tab_study"_i18n, [this] { return studyTab(); });
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

    bindingsBox = new brls::Box(brls::Axis::COLUMN);
    box->addView(bindingsBox);
    fillBindings();
    return scroller;
}

void SettingsActivity::fillBindings()
{
    if (!bindingsBox)
        return;
    bindingsBox->clearViews();
    controls::Layout layout = controls::activeLayout();
    bindingsBox->addView(header("ankinx/controls/buttons"_i18n + ": " + layoutName(layout)));
    for (controls::Action action : controls::allActions())
    {
        auto* cell = detail(brls::getStr(controls::actionName(action)),
                            controls::glyph(controls::binding(layout, action)));
        cell->registerClickAction([this, action](brls::View*) {
            captureButton(action);
            return true;
        });
        bindingsBox->addView(cell);
    }
    auto* reset = detail("ankinx/controls/reset"_i18n, "");
    reset->registerClickAction([this, layout](brls::View*) {
        controls::resetBindings(layout);
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
    return scroller;
}

brls::View* SettingsActivity::aboutTab()
{
    brls::ScrollingFrame* scroller;
    brls::Box* box = column(&scroller);
    box->addView(header("AnkiNX"));
    box->addView(detail("ankinx/settings/version"_i18n, ANKINX_VERSION));
    box->addView(detail("ankinx/settings/backend"_i18n, std::string("Anki ") + anki_buildhash()));
    box->addView(detail("ankinx/settings/data_folder"_i18n, anki::dataDir()));
    box->addView(detail("ankinx/settings/licence"_i18n, "AGPL-3.0-or-later"));
    box->addView(detail("ankinx/settings/source"_i18n, "github.com/lukasheber/AnkiNX"));
    return scroller;
}
