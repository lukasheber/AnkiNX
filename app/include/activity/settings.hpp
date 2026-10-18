#pragma once

#include <borealis.hpp>
#include <memory>

#include "util/controls.hpp"

// Settings, opened with − from the deck list: side tabs like the Switch's
// System Settings (docs/ux-plan.md, section 1). Account & sync, Controls,
// About; Study and Statistics come with their features.
class SettingsActivity : public brls::Activity
{
  public:
    SettingsActivity();
    ~SettingsActivity() override;

    CONTENT_FROM_XML_RES("activity/settings.xml");
    void onContentAvailable() override;

  private:
    brls::View* accountTab();
    brls::View* controlsTab();
    brls::View* studyTab();
    brls::View* aboutTab();
    // The Controls tab's list of actions for the layout in use.
    void fillBindings();
    void captureButton(controls::Action action);

    brls::Box* bindingsBox = nullptr;
    // See ReviewerActivity: guards callbacks that outlive the screen.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    BRLS_BIND(brls::AppletFrame, frame, "frame");
    BRLS_BIND(brls::TabFrame, tabs, "tabs");
};
