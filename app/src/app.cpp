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
    hideStatus(contentView);
}

void hideStatus(brls::View* frame)
{
    // Ids from borealis' BottomBar (lib/views/bottom_bar.cpp).
    for (const char* id : { "brls/battery", "brls/wireless", "brls/hints/time" })
        if (brls::View* view = frame->getView(id))
            view->setVisibility(brls::Visibility::GONE);
}

} // namespace app
