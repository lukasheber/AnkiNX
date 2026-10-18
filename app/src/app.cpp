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
    // borealis' BottomBar (lib/views/bottom_bar.cpp): a row of the hints and
    // a box with battery, Wi-Fi and clock, spread to both ends. Without the
    // status box, the hints go in the middle.
    brls::View* battery = frame->getView("brls/battery");
    brls::Box* status   = battery ? dynamic_cast<brls::Box*>(battery->getParent()) : nullptr;
    brls::Box* row      = status ? dynamic_cast<brls::Box*>(status->getParent()) : nullptr;
    if (!status || !row)
        return;
    status->setVisibility(brls::Visibility::GONE);
    row->setJustifyContent(brls::JustifyContent::CENTER);
    // No greyed "A OK" when A does nothing (borealis adds one by default).
    for (brls::View* child : row->getChildren())
        if (auto* hints = dynamic_cast<brls::Hints*>(child))
            hints->setAddUnableAButtonAction(false);
}

} // namespace app
