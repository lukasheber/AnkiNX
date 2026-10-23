// AnkiNX: Anki for the Nintendo Switch (borealis frontend over Anki's Rust backend).

#include <borealis.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include "activity/deck_picker.hpp"
#include "anki/api.hpp"
#include "script/card_script.hpp"
#include "util/audio.hpp"
#include "util/controls.hpp"
#include "util/settings.hpp"
#include "util/update.hpp"
#include "view/card_view.hpp"

using namespace brls::literals;

// Card text uses the Standard shared font (Latin + Japanese) first, so kanji in
// Japanese decks get Japanese glyph forms; borealis' own chain starts with
// Chinese. Fall back to the other system fonts for Chinese, Korean and symbols.
static void setupCardFontFallbacks()
{
    NVGcontext* vg = brls::Application::getNVGContext();
    int regular    = brls::Application::getFont(brls::FONT_REGULAR);
    if (!vg || regular == brls::FONT_INVALID)
        return;
    for (const std::string& name :
         { brls::FONT_CHINESE_SIMPLIFIED, brls::FONT_CHINESE_SIMPLIFIED_EXT, brls::FONT_CHINESE_TRADITIONAL,
           brls::FONT_KOREAN_REGULAR, brls::FONT_SWITCH_ICONS, brls::FONT_MATERIAL_ICONS })
    {
        int font = brls::Application::getFont(name);
        if (font != brls::FONT_INVALID && font != regular)
            nvgAddFallbackFontId(vg, regular, font);
    }
}

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "-d") == 0)
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
#ifdef __SWITCH__
    // On the console the log goes to /switch/ankinx/ankinx.log (stdout only
    // reaches a PC through nxlink). The previous run's is kept as
    // ankinx.old.log: after a crash, the app is usually opened again first.
    {
        std::string dir = anki::dataDir();
        mkdir(dir.c_str(), 0777);
        std::rename((dir + "/ankinx.log").c_str(), (dir + "/ankinx.old.log").c_str());
        if (std::FILE* log = std::fopen((dir + "/ankinx.log").c_str(), "w"))
            brls::Logger::setLogOutput(log);
    }
#endif
    if (argc > 0)
        update::setSelfPath(argv[0]); // hbmenu passes the .nro's path

    // The console's language, unless Settings > About picks one.
    anki::json language = settings::get("language");
    brls::Platform::APP_LOCALE_DEFAULT =
        language.is_string() && !language.get<std::string>().empty() ? language.get<std::string>() : brls::LOCALE_AUTO;
    if (!brls::Application::init())
    {
        brls::Logger::error("Unable to init the borealis application");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("ankinx/title"_i18n);
    // A slimmer top bar than borealis' 88 px: more room for cards and lists.
    brls::getStyle().addMetric("brls/applet_frame/header_height", 72.0f);
    // "+" quits from every screen through app::quit (sync first), registered
    // by each activity instead of borealis' global quit.
    brls::Application::registerXMLView("CardView", CardView::create);
    setupCardFontFallbacks();

    CardScript::log = [](int level, const std::string& msg) {
        if (level >= 2)
            brls::Logger::error("card script: {}", msg);
        else if (level == 1)
            brls::Logger::warning("card script: {}", msg);
        else
            brls::Logger::info("card script: {}", msg);
    };
    brls::Application::pushActivity(new DeckPickerActivity());
    // Joy-Cons taken off the console: offer the system grip screen (one
    // sideways Joy-Con), as games do.
    controls::watchControllers();

    // Applet mode (hbmenu via the Album) has far less memory than title
    // override; warn, but let the user continue.
    if (!brls::Application::getPlatform()->isApplicationMode())
    {
        auto* dialog = new brls::Dialog("ankinx/hint/applet_mode"_i18n);
        dialog->addButton("hints/exit"_i18n, [] { brls::Application::quit(); });
        dialog->addButton("ankinx/hint/continue"_i18n, [] {});
        dialog->setCancelable(false);
        dialog->open();
    }

    while (brls::Application::mainLoop())
        ;

    // Also reached when the app is closed from HOME (borealis' switch_wrapper
    // locks exit until here). Close the collection (with backup) and stop the
    // backend's runtime threads now, while sockets still work: libnx tears the
    // socket service down after main() returns.
    audio::shutdown();
    try
    {
        anki::call("shutdown");
    }
    catch (const anki::Error& e)
    {
        brls::Logger::error("backend shutdown failed: {}", e.what());
    }
    // A downloaded update replaces the .nro now that nothing uses it.
    update::applyPending();
    return EXIT_SUCCESS;
}
