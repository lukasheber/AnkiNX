#include "app.hpp"

#include "util/sync_flow.hpp"

using namespace brls::literals;

namespace app
{

void quit()
{
    static bool quitting = false;
    if (quitting)
        return;
    quitting = true;
    // Closing from HOME skips this (no time to sync); the collection is still
    // saved by the backend's shutdown in main().
    sync_flow::autoSync([] { brls::Application::quit(); }, sync_flow::Mode::Exit, false);
}

void registerQuit(brls::View* contentView)
{
    contentView->registerAction("hints/exit"_i18n, brls::BUTTON_START, [](brls::View*) {
        quit();
        return true;
    });
}

} // namespace app
