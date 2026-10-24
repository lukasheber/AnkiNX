#include "util/wake.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace wake
{

brls::VoidEvent* event()
{
    static brls::VoidEvent wakeEvent;
#ifdef __SWITCH__
    // libnx calls applet hooks from appletMainLoop(), which borealis' main
    // loop runs: the UI thread. borealis has its own hook; this one only
    // listens for waking.
    static AppletHookCookie cookie;
    static bool hooked = [] {
        appletHook(&cookie, [](AppletHookType hook, void*) {
            if (hook == AppletHookType_OnResume)
            {
                brls::Logger::info("woke from sleep");
                event()->fire();
            }
        }, nullptr);
        return true;
    }();
    (void)hooked;
#endif
    return &wakeEvent;
}

} // namespace wake
