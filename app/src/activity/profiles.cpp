#include "activity/profiles.hpp"

#include "app.hpp"
#include "util/keyboard.hpp"
#include "util/settings.hpp"
#include "util/sync_flow.hpp"

#include <algorithm>

using namespace brls::literals;

namespace
{

const char* ICON_CHECK = "\xEE\x97\x8A"; // Material check

// As the backend's MAX_PROFILE_NAME.
const int MAX_NAME = 40;

} // namespace

namespace profiles
{

std::string displayName(const std::string& name) { return name.empty() ? "ankinx/profiles/default"_i18n : name; }

} // namespace profiles

ProfilesActivity::ProfilesActivity(std::function<void()> changed)
    : changed(std::move(changed))
{
}

ProfilesActivity::~ProfilesActivity() { *alive = false; }

brls::View* ProfilesActivity::createContentView()
{
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(20, 40, 30, 40);
    auto* note = new brls::Label();
    note->setFontSize(16);
    note->setMarginBottom(12);
    note->setText("ankinx/profiles/note"_i18n);
    box->addView(note);

    // The rows are never deleted, only hidden (a deleted profile's): one of
    // them is where the focus is, and whose action is running.
    list = new brls::Box(brls::Axis::COLUMN);
    box->addView(list);
    addCell = new brls::DetailCell();
    addCell->setText("ankinx/profiles/add"_i18n);
    addCell->setDetailText("");
    addCell->registerClickAction([this](brls::View*) {
        add();
        return true;
    });
    box->addView(addCell);
    reload();

    auto* scroller = new brls::ScrollingFrame();
    scroller->setContentView(box);
    scroller->setGrow(1.0f);
    auto* frame = new brls::AppletFrame(scroller);
    frame->setTitle("ankinx/profiles/title"_i18n);
    app::hideStatus(frame);
    return frame;
}

ProfilesActivity::Row* ProfilesActivity::row(const std::string& id)
{
    for (Row& r : rows)
        if (r.id == id)
            return &r;
    return nullptr;
}

void ProfilesActivity::addRow(const std::string& id)
{
    auto* cell = new brls::DetailCell();
    cell->registerClickAction([this, id](brls::View*) {
        switchTo(id);
        return true;
    });
    cell->updateActionHint(brls::BUTTON_A, "ankinx/profiles/switch"_i18n);
    cell->registerAction("ankinx/profiles/rename"_i18n, brls::BUTTON_X, [this, id](brls::View*) {
        rename(id);
        return true;
    });
    // (not the first profile: it is the data folder itself)
    cell->registerAction("ankinx/profiles/delete"_i18n, brls::BUTTON_Y, [this, id](brls::View*) {
        remove(id);
        return true;
    });
    cell->setActionAvailable(brls::BUTTON_Y, !id.empty());
    list->addView(cell);
    rows.push_back({ id, "", cell });
}

void ProfilesActivity::reload()
{
    anki::json all;
    try
    {
        all = anki::call("profiles");
    }
    catch (const anki::Error& e)
    {
        brls::Application::notify(e.what());
        return;
    }
    current = all.value("current", "");
    std::vector<std::string> ids;
    for (const auto& p : all["profiles"])
    {
        std::string id = p.value("id", "");
        ids.push_back(id);
        if (!row(id))
            addRow(id);
        Row* r  = row(id);
        r->name = p.value("name", "");
        r->cell->setText(profiles::displayName(r->name));
        std::string account = p["username"].is_string() ? p["username"].get<std::string>()
                                                         : "ankinx/settings/not_logged_in"_i18n;
        r->cell->setDetailText(id == current ? std::string(ICON_CHECK) + "  " + account : account);
    }
    for (Row& r : rows)
    {
        if (std::find(ids.begin(), ids.end(), r.id) != ids.end() ||
            r.cell->getVisibility() == brls::Visibility::GONE)
            continue;
        if (brls::Application::getCurrentFocus() == r.cell)
            brls::Application::giveFocus(addCell);
        r.cell->setVisibility(brls::Visibility::GONE);
    }
}

void ProfilesActivity::report(const anki::Error& error)
{
    sync_flow::showMessage(error.kind == "exists"   ? "ankinx/profiles/exists"_i18n
                           : error.kind == "in_use" ? "ankinx/profiles/delete_in_use"_i18n
                                                    : std::string(error.what()));
}

// The profile in use is synced first, if automatic sync is on (as before
// exiting: this console may not open it again for a while); the other one is
// synced by the deck list, as when the app opens.
void ProfilesActivity::switchTo(const std::string& id)
{
    if (id == current)
        return;
    sync_flow::autoSync(
        [this, id, token = alive] {
            sync_flow::blocking(
                "ankinx/profiles/switching"_i18n,
                [id] { return anki::call("switch_profile", { { "id", id } }); },
                [this, id, token](const anki::json* status, const anki::Error* error) {
                    if (error)
                        return sync_flow::showMessage(error->what());
                    settings::setProfile(id);
                    brls::Application::notify(brls::getStr(
                        "ankinx/profiles/switched", profiles::displayName(status->value("profile_name", ""))));
                    if (!*token)
                        return;
                    reload();
                    changed();
                });
        },
        sync_flow::Mode::Exit, false);
}

void ProfilesActivity::add()
{
    keyboard::text("ankinx/profiles/name_prompt"_i18n, "", MAX_NAME, [this, token = alive](const std::string& name) {
        if (!*token || name.find_first_not_of(' ') == std::string::npos)
            return;
        try
        {
            anki::call("add_profile", { { "name", name } });
            reload();
            brls::Application::notify("ankinx/profiles/added"_i18n);
        }
        catch (const anki::Error& e)
        {
            report(e);
        }
    });
}

void ProfilesActivity::rename(const std::string& id)
{
    Row* r = row(id);
    if (!r)
        return;
    keyboard::text("ankinx/profiles/name_prompt"_i18n, r->name, MAX_NAME,
                   [this, id, token = alive](const std::string& name) {
                       if (!*token || name.find_first_not_of(' ') == std::string::npos)
                           return;
                       try
                       {
                           anki::call("rename_profile", { { "id", id }, { "name", name } });
                           reload();
                           if (id == current)
                               changed();
                       }
                       catch (const anki::Error& e)
                       {
                           report(e);
                       }
                   });
}

void ProfilesActivity::remove(const std::string& id)
{
    Row* r = row(id);
    if (!r || id.empty())
        return;
    if (id == current)
        return sync_flow::showMessage("ankinx/profiles/delete_in_use"_i18n);
    auto* dialog = new brls::Dialog(brls::getStr("ankinx/profiles/delete_confirm", profiles::displayName(r->name)));
    dialog->addButton("hints/cancel"_i18n, [] {});
    dialog->addButton("ankinx/profiles/delete"_i18n, [this, id, token = alive] {
        sync_flow::blocking(
            "ankinx/profiles/deleting"_i18n, [id] { return anki::call("delete_profile", { { "id", id } }); },
            [this, id, token](const anki::json*, const anki::Error* error) {
                if (error)
                    sync_flow::showMessage(error->what());
                else
                {
                    settings::forgetProfile(id);
                    brls::Application::notify("ankinx/profiles/deleted"_i18n);
                }
                // (also after an error: some of it may be gone)
                if (*token)
                    reload();
            });
    });
    dialog->open();
}
