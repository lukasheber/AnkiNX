#pragma once

#include <borealis.hpp>
#include <functional>
#include <memory>
#include <string>

// AnkiWeb login screen, like AnkiDroid's: Email and Password rows that each
// open the system keyboard when selected, then "Log in".
class LoginActivity : public brls::Activity
{
  public:
    // `onDone(true)` after a successful login; `onDone(false)` if the user leaves.
    explicit LoginActivity(std::function<void(bool)> onDone);
    ~LoginActivity() override;

    CONTENT_FROM_XML_RES("activity/login.xml");
    void onContentAvailable() override;

  private:
    void editEmail();
    void editPassword();
    void submit();
    void refresh();
    void setStatus(const std::string& text, bool error);

    std::function<void(bool)> onDone;
    std::string email;
    std::string password;
    bool busy     = false;
    bool loggedIn = false;
    // See ReviewerActivity: guards callbacks that outlive the screen.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    BRLS_BIND(brls::AppletFrame, frame, "frame");
    BRLS_BIND(brls::Box, emailRow, "emailRow");
    BRLS_BIND(brls::Box, passwordRow, "passwordRow");
    BRLS_BIND(brls::Label, emailValue, "emailValue");
    BRLS_BIND(brls::Label, passwordValue, "passwordValue");
    BRLS_BIND(brls::Label, status, "status");
    BRLS_BIND(brls::Button, loginButton, "loginButton");
};
