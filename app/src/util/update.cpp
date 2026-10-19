#include "util/update.hpp"

#include <borealis.hpp>
#include <cstdio>
#include <memory>
#include <sys/stat.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "anki/api.hpp"
#include "app.hpp"
#include "util/settings.hpp"
#include "util/sync_flow.hpp"

using namespace brls::literals;

namespace update
{
namespace
{

Info current;
std::string selfPath; // "sdmc:/switch/AnkiNX.nro"
bool checkedThisRun = false;

std::string pendingPath() { return selfPath + ".new"; }

// The backend's paths have no device prefix ("/switch/...").
std::string withoutDevice(const std::string& path)
{
    size_t colon = path.find(":/");
    return colon == std::string::npos ? path : path.substr(colon + 1);
}

void store(const anki::json& result)
{
    current.checked = true;
    current.newer   = result.value("newer", false);
    current.latest  = result.value("latest", "");
    current.url     = result["url"].is_string() ? result["url"].get<std::string>() : "";
    current.notes   = result.value("notes", "");
    current.size    = result["size"].is_number() ? result["size"].get<uint64_t>() : 0;
}

std::string megabytes(uint64_t bytes) { return std::to_string((bytes + 512 * 1024) / (1024 * 1024)); }

// Poll the download and show its progress; close when it ends.
void watch(brls::Dialog* dialog, brls::Label* label, std::shared_ptr<bool> open)
{
    brls::delay(400, [dialog, label, open] {
        if (!*open)
            return;
        anki::json status;
        try
        {
            status = anki::call("update_status");
        }
        catch (const anki::Error& e)
        {
            status = { { "done", true }, { "error", e.what() } };
        }
        if (!status.value("done", false))
        {
            uint64_t received = status.value("received", (uint64_t)0);
            uint64_t total    = status.value("total", (uint64_t)0);
            std::string text  = brls::getStr("ankinx/update/downloading", current.latest);
            if (total > 0)
                text += "\n\n" + std::to_string(received * 100 / total) + "%  (" + megabytes(received) + " / " +
                        megabytes(total) + " MB)";
            label->setText(text);
            return watch(dialog, label, open);
        }
        *open             = false;
        std::string error = status["error"].is_string() ? status["error"].get<std::string>() : "";
        dialog->close([error] {
            sync_flow::showMessage(error.empty() ? brls::getStr("ankinx/update/downloaded", current.latest)
                                                 : "ankinx/update/failed"_i18n + "\n\n" + error);
        });
    });
}

void download()
{
    try
    {
        anki::call("download_update", { { "url", current.url }, { "path", withoutDevice(pendingPath()) } });
    }
    catch (const anki::Error& e)
    {
        sync_flow::showMessage("ankinx/update/failed"_i18n + "\n\n" + e.what());
        return;
    }
    brls::Label* label = nullptr;
    auto* dialog       = sync_flow::waitDialog(brls::getStr("ankinx/update/downloading", current.latest), &label);
    dialog->open();
    watch(dialog, label, std::make_shared<bool>(true));
}

} // namespace

void setSelfPath(const std::string& path) { selfPath = path; }

const Info& info() { return current; }

bool canInstall()
{
#ifdef __SWITCH__
    return selfPath.size() > 4 && selfPath.compare(selfPath.size() - 4, 4, ".nro") == 0;
#else
    return false;
#endif
}

void checkInBackground(std::function<void()> onResult)
{
    if (checkedThisRun || settings::get("check_updates") == false)
        return;
    checkedThisRun = true;
    brls::async([onResult] {
        anki::json result;
        try
        {
            result = anki::call("check_update");
        }
        catch (const anki::Error&)
        {
            return; // offline: try again next time the app opens
        }
        brls::sync([result, onResult] {
            store(result);
            if (current.newer)
                brls::Application::notify(brls::getStr("ankinx/update/available", current.latest));
            if (onResult)
                onResult();
        });
    });
}

void checkNow(std::function<void()> onResult)
{
    sync_flow::blocking(
        "ankinx/update/checking"_i18n, [] { return anki::call("check_update"); },
        [onResult](const anki::json* result, const anki::Error* error) {
            if (error)
            {
                sync_flow::showMessage("ankinx/update/check_failed"_i18n + "\n\n" + error->what());
                return;
            }
            store(*result);
            if (onResult)
                onResult();
            if (current.newer)
                offer();
            else
                sync_flow::showMessage(brls::getStr("ankinx/update/up_to_date", ANKINX_VERSION));
        });
}

void offer()
{
    if (!current.newer)
        return;
    std::string text = brls::getStr("ankinx/update/confirm", current.latest, megabytes(current.size));
    if (!canInstall() || current.url.empty())
    {
        // The desktop preview, or a release without the .nro.
        sync_flow::showMessage(brls::getStr("ankinx/update/manual", current.latest) +
                               "\n\ngithub.com/lukasheber/AnkiNX");
        return;
    }
    auto* dialog = new brls::Dialog(text);
    dialog->addButton("hints/cancel"_i18n, [] {});
    dialog->addButton("ankinx/update/download"_i18n, [] { brls::sync([] { download(); }); });
    dialog->open();
}

void applyPending()
{
    if (!canInstall())
        return;
    std::string fresh = pendingPath();
    struct stat st;
    if (stat(fresh.c_str(), &st) != 0)
        return;
#ifdef __SWITCH__
    // The running .nro stays open while its romfs is mounted.
    romfsExit();
#endif
    // Keep the old .nro until the new one is in place.
    std::string old = selfPath + ".old";
    std::remove(old.c_str());
    if (std::rename(selfPath.c_str(), old.c_str()) != 0)
        return;
    if (std::rename(fresh.c_str(), selfPath.c_str()) != 0)
    {
        std::rename(old.c_str(), selfPath.c_str());
        return;
    }
    std::remove(old.c_str());
}

} // namespace update
