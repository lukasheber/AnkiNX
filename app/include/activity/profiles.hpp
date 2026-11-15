#pragma once

#include <borealis.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "anki/api.hpp"

// Profiles, as in Anki desktop: each has its own collection, media, backups
// and AnkiWeb account, for several people on a console or several
// collections of one person. The backend keeps them (api.rs, "Profiles"):
// the first one in the data folder itself, the others in its "profiles".
namespace profiles
{

// A profile's name as shown: the first one is "Default" until it is renamed.
std::string displayName(const std::string& name);

} // namespace profiles

// Settings > Account > Profile: the profiles, A switches to one, X renames
// it, Y deletes it; the last row adds one.
class ProfilesActivity : public brls::Activity
{
  public:
    // changed: the profile in use is another one, or has another name.
    explicit ProfilesActivity(std::function<void()> changed);
    ~ProfilesActivity() override;

    brls::View* createContentView() override;

  private:
    struct Row
    {
        std::string id;
        std::string name;
        brls::DetailCell* cell;
    };

    // Read the profiles again: new rows, and what each one says.
    void reload();
    void addRow(const std::string& id);
    void switchTo(const std::string& id);
    void add();
    void rename(const std::string& id);
    void remove(const std::string& id);
    Row* row(const std::string& id);
    // A backend error, in the app's words where it has them.
    void report(const anki::Error& error);

    std::function<void()> changed;
    std::vector<Row> rows;
    std::string current;
    brls::Box* list           = nullptr;
    brls::DetailCell* addCell = nullptr;
    // See ReviewerActivity: guards callbacks that outlive the screen.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
