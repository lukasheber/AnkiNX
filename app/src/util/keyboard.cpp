#include "util/keyboard.hpp"

#include "app.hpp"
#include "util/kana.hpp"
#include "util/settings.hpp"
#include "view/thumb_keyboard.hpp"

#include <borealis.hpp>
#include <utility>
#include <vector>

using namespace brls::literals;

#ifdef __SWITCH__
#include <switch.h>

namespace keyboard
{
namespace
{

void show(SwkbdConfig& config, const std::string& prompt, int maxLength, const Callback& onText)
{
    swkbdConfigSetGuideText(&config, prompt.c_str());
    swkbdConfigSetHeaderText(&config, prompt.c_str()); // shown only up to 32 chars
    swkbdConfigSetStringLenMax(&config, maxLength);
    swkbdConfigSetBlurBackground(&config, true);
    std::vector<char> buffer((size_t)maxLength * 4 + 1, 0); // UTF-8
    Result rc = swkbdShow(&config, buffer.data(), buffer.size());
    swkbdClose(&config);
    if (R_SUCCEEDED(rc)) // fails when cancelled
        onText(buffer.data());
}

} // namespace

void text(const std::string& prompt, const std::string& initial, int maxLength, Callback onText)
{
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return;
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetType(&config, SwkbdType_All);
    swkbdConfigSetInitialText(&config, initial.c_str());
    show(config, prompt, maxLength, onText);
}

void password(const std::string& prompt, int maxLength, Callback onText)
{
    SwkbdConfig config;
    if (R_FAILED(swkbdCreate(&config, 0)))
        return;
    swkbdConfigMakePresetPassword(&config);
    show(config, prompt, maxLength, onText);
}

} // namespace keyboard

#else // desktop preview: borealis' own input dialog

namespace keyboard
{

void text(const std::string& prompt, const std::string& initial, int maxLength, Callback onText)
{
    brls::Application::getPlatform()->getImeManager()->openForText(
        [onText](std::string s) { onText(s); }, prompt, "", maxLength, initial);
}

void password(const std::string& prompt, int maxLength, Callback onText)
{
    brls::Application::getPlatform()->getImeManager()->openForPassword(
        [onText](std::string s) { onText(s); }, prompt, "", maxLength);
}

} // namespace keyboard

#endif

namespace keyboard
{

void answer(const std::string& prompt, const std::string& initial, int maxLength, Callback onText, bool kana)
{
    anki::json choice = settings::get("typing_keyboard");
    if (app::isHandheld() && choice.is_string() && choice != "system")
    {
        ThumbKeyboard::open(choice == "thumb_right"  ? ThumbKeyboard::Layout::RightThumb
                            : choice == "thumb_left" ? ThumbKeyboard::Layout::LeftThumb
                                                     : ThumbKeyboard::Layout::Split,
                            prompt, initial, maxLength, std::move(onText), kana);
        return;
    }
    if (!kana)
    {
        text(prompt, initial, maxLength, std::move(onText));
        return;
    }
    // The system keyboard doesn't convert: romaji typed there becomes
    // hiragana when confirmed.
    text(prompt + " (" + "ankinx/keyboard/hiragana"_i18n + ")", initial, maxLength,
         [onText](const std::string& typed) { onText(kana::toKana(typed)); });
}

} // namespace keyboard
