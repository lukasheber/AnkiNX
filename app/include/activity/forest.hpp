#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>
#include <vector>

class ForestView;

// Memory Forest on the whole screen (the tree count on the deck list opens
// it): each tree says its day, its cards and how well they are remembered,
// as the add-on's tooltips do. The D-pad or a tap picks a tree, A opens its
// cards in the browser, B goes back.
class ForestActivity : public brls::Activity
{
  public:
    // `data`: the forest as forest::grow made it; `running`, a view already
    // drawing it (the deck list's), whose scene is used rather than built again.
    ForestActivity(std::string data, const ForestView* running = nullptr);
    ~ForestActivity() override;

    brls::View* createContentView() override;

  private:
    struct Tree
    {
        int ago;
        float x, y; // where it stands, in the scene's pixels
    };
    // What is picked: a tree (ago), or the deep forest (its days), or nothing
    // that has cards (a pond, an animal).
    struct Picked
    {
        std::string kind;
        int ago = -1, from = -1, to = -1;
    };

    void ready();
    void move(int dx, int dy);
    void select(int ago);
    void tapped(float x, float y);
    // The tooltip, from a pick's or mark's JSON answer.
    void showTip(const std::string& answer);
    void browse();

    std::string data;
    const ForestView* running = nullptr;
    ForestView* forest     = nullptr;
    brls::Box* tip         = nullptr;
    std::vector<Tree> trees;
    Picked picked;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
