#include "activity/forest.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "activity/browser.hpp"
#include "anki/api.hpp"
#include "view/forest_view.hpp"

using namespace brls::literals;

namespace
{

// The tooltip: this wide, this far from the screen's edges and above its tree.
constexpr float TIP_W = 420, TIP_MARGIN = 16, TIP_GAP = 10;
constexpr float TIP_LINE = 26, TIP_PAD = 14;
// Moving with the D-pad: a tree off to the side counts as this much further.
constexpr float ACROSS_WEIGHT = 3;

// Anki's search for the cards first studied from `from` days ago to `to`
// (Memory Forest's study_log.day_search; suspended cards keep their trees).
std::string daySearch(int from, int to)
{
    std::string search = "introduced:" + std::to_string(from + 1);
    if (to > 0)
        search += " -introduced:" + std::to_string(to);
    return search;
}

} // namespace

ForestActivity::ForestActivity(std::string data, const ForestView* running)
    : data(std::move(data))
    , running(running)
{
}

ForestActivity::~ForestActivity()
{
    *alive = false;
    // (the scene goes on behind the deck list: without the marker)
    if (forest)
        forest->query("mark", "{\"ago\":null}", [](const std::string&) {});
}

brls::View* ForestActivity::createContentView()
{
    auto* root = new brls::Box(brls::Axis::COLUMN);
    root->setWidthPercentage(100);
    root->setHeightPercentage(100);
    root->setBackgroundColor(nvgRGB(0, 0, 0));
    root->setFocusable(true);
    root->setHideHighlight(true);

    forest = new ForestView(false);
    forest->onReady = [this] { ready(); };
    // (on the root: borealis gives touches to focusable views, which the forest isn't)
    root->addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound*) {
        if (status.state == brls::GestureState::END)
            tapped(status.position.x, status.position.y);
    }));
    root->addView(forest);

    tip = new brls::Box(brls::Axis::COLUMN);
    tip->setPositionType(brls::PositionType::ABSOLUTE);
    tip->setWidth(TIP_W);
    tip->setPadding(TIP_PAD, TIP_PAD + 4, TIP_PAD, TIP_PAD + 4);
    tip->setBackgroundColor(nvgRGBA(20, 20, 24, 225));
    tip->setCornerRadius(8);
    tip->setVisibility(brls::Visibility::GONE);
    root->addView(tip);

    // the line under the add-on's forest, across its top here: the journal (on the days
    // there is something to say) and the numbers
    caption = new brls::Box(brls::Axis::COLUMN);
    caption->setPositionType(brls::PositionType::ABSOLUTE);
    caption->setPositionTop(0);
    caption->setPositionLeft(0);
    caption->setWidthPercentage(100);
    caption->setPadding(14, 40, 14, 40);
    caption->setBackgroundColor(nvgRGBA(20, 20, 24, 170));
    caption->setVisibility(brls::Visibility::GONE);
    journalLabel = new brls::Label();
    journalLabel->setFontSize(20);
    journalLabel->setTextColor(nvgRGB(255, 248, 224));
    journalLabel->setMarginBottom(4);
    caption->addView(journalLabel);
    numbersLabel = new brls::Label();
    numbersLabel->setFontSize(17);
    numbersLabel->setTextColor(nvgRGB(220, 220, 220));
    caption->addView(numbersLabel);
    root->addView(caption);

    auto* hints = new brls::Hints();
    hints->setPositionType(brls::PositionType::ABSOLUTE);
    hints->setPositionBottom(0);
    hints->setPositionRight(0);
    hints->setPadding(10, 40, 14, 40);
    root->addView(hints);

    root->registerAction("ankinx/forest/cards"_i18n, brls::BUTTON_A, [this](brls::View*) {
        browse();
        return true;
    });
    root->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    auto arrow = [this, root](brls::ControllerButton button, int dx, int dy) {
        root->registerAction("", button, [this, dx, dy](brls::View*) {
            move(dx, dy);
            return true;
        }, true, true);
    };
    arrow(brls::BUTTON_LEFT, -1, 0);
    arrow(brls::BUTTON_RIGHT, 1, 0);
    arrow(brls::BUTTON_UP, 0, -1);
    arrow(brls::BUTTON_DOWN, 0, 1);

    if (running)
        forest->share(*running);
    else
        forest->show(data);
    return root;
}

// The scene is on screen: learn where every tree stands, and start at the
// newest one.
void ForestActivity::ready()
{
    forest->query("caption", "{}", [this](const std::string& answer) {
        anki::json c = anki::json::parse(answer, nullptr, false);
        if (!c.is_object())
            return;
        std::string numbers;
        for (const auto& item : c["items"])
            numbers += (numbers.empty() ? "" : "  ·  ") + item.value("text", "");
        std::string credit = c.value("credit", "");
        if (!credit.empty())
            numbers += "  ·  " + credit;
        std::string journal = c.value("journal", "");
        journalLabel->setText(journal);
        journalLabel->setVisibility(journal.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        numbersLabel->setText(numbers);
        caption->setVisibility(numbers.empty() && journal.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    });
    forest->query("trees", "{}", [this](const std::string& answer) {
        anki::json list = anki::json::parse(answer, nullptr, false);
        trees.clear();
        if (list.is_array())
            for (const auto& t : list)
                trees.push_back({ t.value("ago", 0), t.value("x", 0.0f), t.value("y", 0.0f) });
        int newest = -1;
        for (const auto& t : trees)
            if (newest < 0 || t.ago < newest)
                newest = t.ago;
        if (newest >= 0)
            select(newest);
    });
}

// The nearest tree that way from the one picked (or the newest).
void ForestActivity::move(int dx, int dy)
{
    if (trees.empty())
        return;
    const Tree* from = nullptr;
    for (const auto& t : trees)
        if (t.ago == picked.ago && picked.kind == "tree")
            from = &t;
    if (!from)
    {
        ready();
        return;
    }
    const Tree* best = nullptr;
    float bestScore  = std::numeric_limits<float>::max();
    for (const auto& t : trees)
    {
        float along = dx ? (t.x - from->x) * dx : (t.y - from->y) * dy;
        float across = dx ? std::fabs(t.y - from->y) : std::fabs(t.x - from->x);
        if (along <= 0.5f || &t == from)
            continue;
        float score = along + ACROSS_WEIGHT * across;
        if (score < bestScore)
        {
            bestScore = score;
            best      = &t;
        }
    }
    if (best)
        select(best->ago);
}

void ForestActivity::select(int ago)
{
    picked = { "tree", ago };
    forest->query("mark", anki::json({ { "ago", ago } }).dump(), [this](const std::string& answer) { showTip(answer); });
}

void ForestActivity::tapped(float x, float y)
{
    float sx, sy;
    forest->screenToScene(x, y, sx, sy);
    forest->query("pick", anki::json({ { "x", sx }, { "y", sy } }).dump(), [this, x, y](const std::string& answer) {
        anki::json hit = anki::json::parse(answer, nullptr, false);
        if (!hit.is_object())
        {
            forest->query("mark", "{\"ago\":null}", [](const std::string&) {});
            picked = {};
            tip->setVisibility(brls::Visibility::GONE);
            return;
        }
        std::string kind = hit.value("kind", "");
        if (kind == "tree")
        {
            select(hit.value("ago", 0));
            return;
        }
        // a pond, an animal or the deep forest: its tooltip where it was tapped
        forest->query("mark", "{\"ago\":null}", [](const std::string&) {});
        picked = { kind, -1, hit.value("from", -1), hit.value("to", -1) };
        hit["x"]   = nullptr;
        hit["tapX"] = x;
        hit["tapY"] = y;
        showTip(hit.dump());
    });
}

void ForestActivity::showTip(const std::string& answer)
{
    anki::json tipData = anki::json::parse(answer, nullptr, false);
    if (!tipData.is_object() || !tipData.contains("lines") || tipData["lines"].empty())
    {
        tip->setVisibility(brls::Visibility::GONE);
        return;
    }
    const auto& lines = tipData["lines"];
    // a label a line, the first its title (a long one wraps)
    tip->clearViews();
    for (size_t i = 0; i < lines.size(); i++)
    {
        auto* line = new brls::Label();
        line->setSingleLine(false);
        line->setFontSize(i ? 18 : 20);
        line->setTextColor(i ? nvgRGB(220, 220, 220) : nvgRGB(255, 248, 224));
        line->setMarginTop(i ? 4 : 0);
        line->setText(lines[i].get<std::string>());
        tip->addView(line);
    }
    // Above the tree (below it if there is no room), or by the tap.
    float x, y;
    float height = TIP_PAD * 2 + TIP_LINE * (float)lines.size() + 4;
    if (tipData["x"].is_number())
    {
        forest->sceneToScreen(tipData["x"].get<float>(), tipData.value("top", 0.0f), x, y);
        y -= height + TIP_GAP + 24;
        if (y < TIP_MARGIN)
            y += height + 2 * TIP_GAP + 48;
    }
    else
    {
        x = tipData.value("tapX", 640.0f);
        y = tipData.value("tapY", 360.0f) - height - TIP_GAP;
    }
    float screenW = brls::Application::contentWidth, screenH = brls::Application::contentHeight;
    x = std::clamp(x - TIP_W / 2, TIP_MARGIN, screenW - TIP_W - TIP_MARGIN);
    // (below the caption across the top)
    float top = caption && caption->getVisibility() == brls::Visibility::VISIBLE ? caption->getHeight() + TIP_MARGIN : TIP_MARGIN;
    y         = std::clamp(y, top, std::max(top, screenH - height - TIP_MARGIN));
    tip->setPositionLeft(x);
    tip->setPositionTop(y);
    tip->setVisibility(brls::Visibility::VISIBLE);
}

void ForestActivity::browse()
{
    if (picked.kind == "tree")
        brls::Application::pushActivity(new BrowserActivity(daySearch(picked.ago, picked.ago)));
    else if (picked.kind == "deep")
        brls::Application::pushActivity(new BrowserActivity(daySearch(picked.from, picked.to)));
}
