#include "activity/login.hpp"

#include "anki/api.hpp"
#include "app.hpp"
#include "util/keyboard.hpp"
#include "util/settings.hpp"

using namespace brls::literals;

namespace
{

std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    return start == std::string::npos ? "" : s.substr(start, end - start + 1);
}

} // namespace

LoginActivity::LoginActivity(std::function<void(bool)> onDone)
    : onDone(std::move(onDone))
{
}

LoginActivity::~LoginActivity()
{
    *alive = false;
    if (!loggedIn && onDone)
    {
        // Left without logging in. Report after this screen is gone.
        auto callback = onDone;
        brls::sync([callback] { callback(false); });
    }
}

void LoginActivity::onContentAvailable()
{
    frame->setTitle("ankinx/login/title"_i18n);
    app::registerQuit(frame);
    emailRow->registerClickAction([this](brls::View*) {
        editEmail();
        return true;
    });
    passwordRow->registerClickAction([this](brls::View*) {
        editPassword();
        return true;
    });
    loginButton->registerClickAction([this](brls::View*) {
        submit();
        return true;
    });
    // Touch
    emailRow->addGestureRecognizer(new brls::TapGestureRecognizer(emailRow, [this] { editEmail(); }));
    passwordRow->addGestureRecognizer(new brls::TapGestureRecognizer(passwordRow, [this] { editPassword(); }));
    refresh();
}

void LoginActivity::editEmail()
{
    if (busy)
        return;
    keyboard::text("ankinx/sync/email"_i18n, email, 128, [this, token = alive](const std::string& text) {
        if (!*token)
            return;
        email = trim(text);
        refresh();
        if (!email.empty() && password.empty())
            brls::Application::giveFocus(passwordRow);
    });
}

void LoginActivity::editPassword()
{
    if (busy)
        return;
    keyboard::password("ankinx/sync/password"_i18n, 128, [this, token = alive](const std::string& text) {
        if (!*token)
            return;
        password = text;
        refresh();
        if (!password.empty())
            brls::Application::giveFocus(loginButton);
    });
}

void LoginActivity::refresh()
{
    emailValue->setText(email.empty() ? "ankinx/login/required"_i18n : email);
    // One dot per character; the password itself is never displayed.
    std::string dots;
    for (size_t i = 0; i < password.size(); i++)
        dots += "•";
    passwordValue->setText(password.empty() ? "ankinx/login/required"_i18n : dots);
}

void LoginActivity::setStatus(const std::string& text, bool error)
{
    status->setText(text);
    status->setTextColor(error ? nvgRGB(239, 83, 80) : brls::Application::getTheme()["brls/text"]);
}

void LoginActivity::submit()
{
    if (busy)
        return;
    if (email.empty() || password.empty())
    {
        setStatus("ankinx/login/missing"_i18n, true);
        brls::Application::giveFocus(email.empty() ? emailRow : passwordRow);
        return;
    }
    busy = true;
    setStatus("ankinx/sync/logging_in"_i18n, false);
    std::string user = email, pass = password;
    brls::async([this, user, pass, token = alive] {
        std::string error;
        try
        {
            // A self-hosted sync server (Settings > Account), else AnkiWeb.
            anki::json args = { { "username", user }, { "password", pass } };
            if (settings::get("sync_server").is_string())
                args["endpoint"] = settings::get("sync_server");
            anki::call("login", args);
        }
        catch (const anki::Error& e)
        {
            error = e.what();
        }
        brls::sync([this, error, token] {
            if (!*token)
                return;
            busy = false;
            if (!error.empty())
            {
                setStatus(error, true);
                return;
            }
            loggedIn = true;
            brls::Application::notify("ankinx/sync/logged_in"_i18n);
            auto callback = onDone;
            brls::Application::popActivity(brls::TransitionAnimation::FADE, [callback] {
                if (callback)
                    callback(true);
            });
        });
    });
}
