#include "util/sync_flow.hpp"

#include <borealis.hpp>

#include "activity/login.hpp"
#include "anki/api.hpp"
#include "util/settings.hpp"

using namespace brls::literals;

namespace sync_flow
{
namespace
{

// Non-cancelable "please wait" dialog around a blocking backend call made on a
// worker thread. `work` returns the result; `done` gets it (or the error).
void runBlocking(const std::string& message, std::function<anki::json()> work,
                 std::function<void(const anki::json*, const anki::Error*)> done)
{
    auto* dialog = waitDialog(message);
    dialog->open();
    brls::async([dialog, work, done] {
        try
        {
            anki::json result = work();
            brls::sync([dialog, done, result] {
                dialog->close([done, result] { done(&result, nullptr); });
            });
        }
        catch (const anki::Error& e)
        {
            anki::Error error = e;
            brls::sync([dialog, done, error] {
                dialog->close([done, error] { done(nullptr, &error); });
            });
        }
    });
}

void showError(const std::string& message)
{
    auto* dialog = new brls::Dialog(message);
    dialog->addButton("hints/ok"_i18n, [] {});
    dialog->open();
}

// Report the background media sync once it ends.
void watchMediaSync()
{
    brls::delay(1500, [] {
        brls::async([] {
            anki::json status;
            try
            {
                status = anki::call("media_sync_status");
            }
            catch (const anki::Error&)
            {
                return;
            }
            brls::sync([status] {
                if (status.value("active", false))
                    watchMediaSync();
                else if (status["error"].is_string())
                    brls::Application::notify("ankinx/sync/media_failed"_i18n + ": " +
                                              status["error"].get<std::string>());
                else
                    brls::Application::notify("ankinx/sync/media_done"_i18n);
            });
        });
    });
}

void fullSync(bool upload, std::function<void()> onFinished)
{
    runBlocking(
        upload ? "ankinx/sync/uploading"_i18n : "ankinx/sync/downloading"_i18n,
        [upload] { return anki::call("full_sync", { { "upload", upload } }); },
        [onFinished](const anki::json*, const anki::Error* error) {
            if (error)
                showError(error->what());
            else
            {
                brls::Application::notify("ankinx/sync/done"_i18n);
                watchMediaSync();
            }
            onFinished();
        });
}

void confirmFullSync(bool upload, std::function<void()> onFinished)
{
    auto* dialog = new brls::Dialog(upload ? "ankinx/sync/confirm_upload"_i18n
                                           : "ankinx/sync/confirm_download"_i18n);
    dialog->addButton("hints/cancel"_i18n, [onFinished] { onFinished(); });
    dialog->addButton(upload ? "ankinx/sync/upload"_i18n : "ankinx/sync/download"_i18n,
                      [upload, onFinished] { fullSync(upload, onFinished); });
    dialog->setCancelable(false);
    dialog->open();
}

void handleSyncResult(const anki::json& result, std::function<void()> onFinished)
{
    std::string message = result.value("server_message", "");
    if (!message.empty())
        brls::Application::notify(message);

    std::string required = result.value("required", "none");
    if (required == "full_download")
        confirmFullSync(false, onFinished);
    else if (required == "full_upload")
        confirmFullSync(true, onFinished);
    else if (required == "full_sync")
    {
        // Both sides changed in incompatible ways: the user picks the survivor.
        auto* dialog = new brls::Dialog("ankinx/sync/conflict"_i18n);
        dialog->addButton("hints/cancel"_i18n, [onFinished] { onFinished(); });
        dialog->addButton("ankinx/sync/upload"_i18n, [onFinished] { confirmFullSync(true, onFinished); });
        dialog->addButton("ankinx/sync/download"_i18n, [onFinished] { confirmFullSync(false, onFinished); });
        dialog->setCancelable(false);
        dialog->open();
    }
    else
    {
        brls::Application::notify("ankinx/sync/done"_i18n);
        watchMediaSync();
        onFinished();
    }
}

} // namespace

void login(std::function<void(bool)> onDone)
{
    // A screen rather than two keyboards in a row: the Switch keyboard can't
    // be relaunched straight after closing, and the user needs to see which
    // field they're filling in and why a login failed.
    brls::Application::pushActivity(new LoginActivity(std::move(onDone)));
}

void logout()
{
    try
    {
        anki::call("logout");
        brls::Application::notify("ankinx/sync/logged_out"_i18n);
    }
    catch (const anki::Error& e)
    {
        showError(e.what());
    }
}

void start(std::function<void()> onFinished, Mode mode)
{
    // Automatic syncs report problems without stopping the user.
    auto report = [mode](const std::string& message) {
        if (mode == Mode::Manual)
            showError(message);
        else
            brls::Application::notify("ankinx/sync/failed"_i18n + ": " + message);
    };
    anki::json status;
    try
    {
        status = anki::call("status");
    }
    catch (const anki::Error& e)
    {
        report(e.what());
        return onFinished();
    }
    if (!status["username"].is_string())
    {
        if (mode != Mode::Manual)
            return onFinished();
        login([onFinished](bool loggedIn) {
            if (loggedIn)
                start(onFinished);
            else
                onFinished();
        });
        return;
    }

    runBlocking(
        "ankinx/sync/syncing"_i18n, [] { return anki::call("sync"); },
        [onFinished, mode, report](const anki::json* result, const anki::Error* error) {
            if (!error)
            {
                // A full sync can't be done unattended on exit: leave it for
                // the next time the app opens.
                if (mode == Mode::Exit && result->value("required", "none") != "none")
                    return onFinished();
                return handleSyncResult(*result, onFinished);
            }
            report(error->what());
            if (error->kind == "auth")
            {
                // Saved key rejected (password changed): forget it so the next
                // sync asks again.
                logout();
            }
            onFinished();
        });
}

void autoSync(std::function<void()> onFinished, Mode mode, bool always)
{
    if (!settings::autoSync())
        return onFinished();
    try
    {
        anki::json status = anki::call("status");
        const anki::json& needed = status["sync_needed"]; // null while closed
        bool changed             = needed.is_string() && needed.get<std::string>() != "none";
        if (!status["username"].is_string() || !status.value("open", false) || !(always || changed))
            return onFinished();
    }
    catch (const anki::Error&)
    {
        return onFinished();
    }
    start(onFinished, mode);
}

brls::Dialog* waitDialog(const std::string& text, brls::Label** label)
{
    brls::Style style = brls::Application::getStyle();
    auto* textLabel  = new brls::Label();
    textLabel->setText(text);
    textLabel->setFontSize(style["brls/dialog/fontSize"]);
    textLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    textLabel->setSingleLine(false);
    auto* box = new brls::Box();
    box->addView(textLabel);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    box->setPadding(style["brls/dialog/paddingTopBottom"], style["brls/dialog/paddingLeftRight"],
                    style["brls/dialog/paddingTopBottom"], style["brls/dialog/paddingLeftRight"]);
    box->setFocusable(true);
    box->setHideHighlight(true);
    auto* dialog = new brls::Dialog(box);
    dialog->setCancelable(false);
    if (label)
        *label = textLabel;
    return dialog;
}

void blocking(const std::string& message, std::function<anki::json()> work,
              std::function<void(const anki::json*, const anki::Error*)> done)
{
    runBlocking(message, std::move(work), std::move(done));
}

void showMessage(const std::string& message) { showError(message); }

} // namespace sync_flow
