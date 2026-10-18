#include "view/stats_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

using namespace brls::literals;

namespace
{

const NVGcolor BLUE   = nvgRGB(66, 165, 245);
const NVGcolor RED    = nvgRGB(239, 83, 80);
const NVGcolor GREEN  = nvgRGB(102, 187, 106);
const NVGcolor ORANGE = nvgRGB(255, 167, 38);

std::string t(const char* key) { return brls::getStr(std::string("ankinx/stats/") + key); }

NVGcolor textColor() { return brls::Application::getTheme()["brls/text"]; }

NVGcolor dimColor()
{
    NVGcolor c = textColor();
    c.a        = 0.55f;
    return c;
}

bool dark() { return brls::Application::getThemeVariant() == brls::ThemeVariant::DARK; }

void text(NVGcontext* vg, float x, float y, const std::string& s, float size, NVGcolor color,
          int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP)
{
    nvgFontFaceId(vg, brls::Application::getDefaultFont());
    nvgFontSize(vg, size);
    nvgFillColor(vg, color);
    nvgTextAlign(vg, align);
    nvgText(vg, x, y, s.c_str(), nullptr);
}

std::string percent(unsigned part, unsigned total)
{
    if (total == 0)
        return "—";
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.1f%%", 100.0 * part / total);
    return buf;
}

unsigned u(const anki::json& j, const char* key) { return j.value(key, 0u); }

} // namespace

StatsView::StatsView()
{
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->registerAction("ankinx/stats/previous"_i18n, brls::BUTTON_LB, [this](brls::View*) {
        switchPanel(-1);
        return true;
    });
    this->registerAction("ankinx/stats/next"_i18n, brls::BUTTON_RB, [this](brls::View*) {
        switchPanel(1);
        return true;
    });
    // Tapping a tab opens it.
    this->addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound*) {
        if (status.state != brls::GestureState::END)
            return;
        float localY = status.position.y - this->getY();
        if (localY > 56)
            return;
        int tab = (int)((status.position.x - this->getX()) / (this->getWidth() / PANELS));
        panel   = std::clamp(tab, 0, PANELS - 1);
    }));
}

void StatsView::setData(anki::json value) { data = std::move(value); }

void StatsView::switchPanel(int delta) { panel = (panel + delta + PANELS) % PANELS; }

void StatsView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*)
{
    drawTabs(vg, x, y, width);
    if (data.is_null())
        return;
    float top = y + 80, h = height - 90, left = x + 40, w = width - 80;
    switch (panel)
    {
        case 0: drawToday(vg, left, top, w, h); break;
        case 1: drawForecast(vg, left, top, w, h); break;
        case 2: drawHeatmap(vg, left, top, w, h); break;
        default: drawRetention(vg, left, top, w, h); break;
    }
}

void StatsView::drawTabs(NVGcontext* vg, float x, float y, float width)
{
    const char* names[PANELS] = { "today", "forecast", "heatmap", "retention" };
    float tabWidth = width / PANELS;
    for (int i = 0; i < PANELS; i++)
    {
        float cx    = x + tabWidth * (i + 0.5f);
        bool active = i == panel;
        text(vg, cx, y + 16, t(names[i]), 22, active ? BLUE : dimColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        if (active)
        {
            nvgBeginPath(vg);
            nvgRect(vg, cx - 50, y + 46, 100, 3);
            nvgFillColor(vg, BLUE);
            nvgFill(vg);
        }
    }
}

void StatsView::drawToday(NVGcontext* vg, float x, float y, float width, float)
{
    const anki::json& today = data["today"];
    text(vg, x + width / 2, y, data.value("studied_today", ""), 24, textColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    unsigned answers = u(today, "answers"), correct = u(today, "correct");
    char minutes[16];
    std::snprintf(minutes, sizeof minutes, "%.1f", u(today, "secs") / 60.0);
    struct Tile
    {
        std::string value, label;
        NVGcolor color;
    } tiles[4] = { { std::to_string(answers), t("cards"), BLUE },
                   { minutes, t("minutes"), textColor() },
                   { std::to_string(answers - std::min(answers, correct)), t("again"), RED },
                   { percent(correct, answers), t("correct"), GREEN } };
    float tileWidth = width / 4;
    for (int i = 0; i < 4; i++)
    {
        float cx = x + tileWidth * (i + 0.5f);
        text(vg, cx, y + 60, tiles[i].value, 48, tiles[i].color, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        text(vg, cx, y + 118, tiles[i].label, 18, dimColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    }

    std::string kinds = t("learn") + " " + std::to_string(u(today, "learn")) + "   ·   " + t("review") + " " +
                        std::to_string(u(today, "review")) + "   ·   " + t("relearn") + " " +
                        std::to_string(u(today, "relearn")) + "   ·   " + t("mature_correct") + " " +
                        percent(u(today, "mature_correct"), u(today, "mature"));
    text(vg, x + width / 2, y + 170, kinds, 20, textColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    // The deck's cards by state.
    const anki::json& cards = data["cards"];
    struct Count
    {
        const char* key;
        NVGcolor color;
    } counts[6] = { { "new", BLUE },     { "learn", RED },       { "young", GREEN },
                    { "mature", GREEN }, { "suspended", ORANGE }, { "buried", ORANGE } };
    text(vg, x + width / 2, y + 240, t("card_counts"), 20, dimColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    float countWidth = width / 6;
    for (int i = 0; i < 6; i++)
    {
        float cx = x + countWidth * (i + 0.5f);
        text(vg, cx, y + 275, std::to_string(u(cards, counts[i].key)), 32, counts[i].color,
             NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        text(vg, cx, y + 315, t((std::string("cards_") + counts[i].key).c_str()), 16, dimColor(),
             NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    }
}

void StatsView::drawForecast(NVGcontext* vg, float x, float y, float width, float height)
{
    std::vector<unsigned> days;
    for (const auto& n : data["forecast"])
        days.push_back(n.get<unsigned>());
    if (days.empty())
        return;
    unsigned total = 0, most = 1;
    for (unsigned n : days)
        total += n, most = std::max(most, n);
    char summary[128];
    std::snprintf(summary, sizeof summary, "%s: %u   ·   %s: %.1f", t("due_30").c_str(), total,
                  t("per_day").c_str(), total / (double)days.size());
    text(vg, x + width / 2, y, summary, 22, textColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    float chartTop = y + 60, chartBottom = y + height - 50, chartHeight = chartBottom - chartTop;
    float barWidth = width / days.size();
    text(vg, x - 8, chartTop, std::to_string(most), 16, dimColor(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgBeginPath(vg);
    nvgRect(vg, x, chartBottom, width, 1);
    nvgFillColor(vg, dimColor());
    nvgFill(vg);
    for (size_t i = 0; i < days.size(); i++)
    {
        float barHeight = chartHeight * days[i] / most;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + i * barWidth + 2, chartBottom - barHeight, barWidth - 4, barHeight, 3);
        nvgFillColor(vg, i == 0 ? RED : BLUE); // today includes the overdue backlog
        nvgFill(vg);
        if (i % 5 == 0)
            text(vg, x + (i + 0.5f) * barWidth, chartBottom + 10, i == 0 ? t("today_short") : std::to_string(i), 16,
                 dimColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    }
}

void StatsView::drawHeatmap(NVGcontext* vg, float x, float y, float width, float height)
{
    std::vector<unsigned> days;
    for (const auto& n : data["heatmap"])
        days.push_back(n.get<unsigned>());
    if (days.empty())
        return;
    unsigned total = 0, studied = 0, most = 1;
    for (unsigned n : days)
        total += n, studied += n > 0, most = std::max(most, n);
    char summary[160];
    std::snprintf(summary, sizeof summary, "%s: %u / %zu   ·   %s: %u   ·   %s: %.1f", t("days_studied").c_str(),
                  studied, days.size(), t("reviews").c_str(), total, t("per_day").c_str(),
                  total / (double)days.size());
    text(vg, x + width / 2, y, summary, 22, textColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

    // Weeks as columns, weekdays as rows (Sunday on top), today last.
    std::time_t now = std::time(nullptr);
    int todayWeekday = std::localtime(&now)->tm_wday;
    int first        = ((todayWeekday - (int)(days.size() - 1)) % 7 + 7) % 7; // weekday of the oldest day
    int columns      = (first + (int)days.size() + 6) / 7;
    float cell       = std::min((width - 60) / columns, (height - 90) / 7);
    float gridLeft   = x + (width - cell * columns) / 2;
    float gridTop    = y + 60;
    NVGcolor empty   = dark() ? nvgRGB(60, 60, 60) : nvgRGB(225, 225, 225);
    for (size_t i = 0; i < days.size(); i++)
    {
        int slot = first + (int)i;
        float cx = gridLeft + (slot / 7) * cell, cy = gridTop + (slot % 7) * cell;
        NVGcolor color = empty;
        if (days[i] > 0)
        {
            // Four shades of green by share of the busiest day.
            float level = std::ceil(4.0f * days[i] / most) / 4.0f;
            color       = nvgLerpRGBA(nvgRGB(155, 220, 160), nvgRGB(30, 130, 60), level);
        }
        nvgBeginPath(vg);
        nvgRoundedRect(vg, cx + 2, cy + 2, cell - 4, cell - 4, 3);
        nvgFillColor(vg, color);
        nvgFill(vg);
        if (i + 1 == days.size())
        {
            nvgStrokeColor(vg, BLUE);
            nvgStrokeWidth(vg, 2);
            nvgStroke(vg);
        }
    }
}

void StatsView::drawRetention(NVGcontext* vg, float x, float y, float width, float)
{
    text(vg, x + width / 2, y, t("true_retention"), 22, textColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    const char* periods[6] = { "today", "yesterday", "week", "month", "year", "all_time" };
    const char* headers[3] = { "young", "mature", "total" };
    float col0 = x + width * 0.15f, colWidth = width * 0.25f, rowHeight = 46, top = y + 60;
    for (int c = 0; c < 3; c++)
        text(vg, col0 + colWidth * (c + 1), top, t(headers[c]), 20, dimColor(), NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    for (int r = 0; r < 6; r++)
    {
        float rowY = top + rowHeight * (r + 1);
        text(vg, col0, rowY, t((std::string("period_") + periods[r]).c_str()), 20, textColor(),
             NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        const anki::json& p = data["retention"][periods[r]];
        unsigned yp = p.is_array() ? p[0].get<unsigned>() : 0, yf = p.is_array() ? p[1].get<unsigned>() : 0;
        unsigned mp = p.is_array() ? p[2].get<unsigned>() : 0, mf = p.is_array() ? p[3].get<unsigned>() : 0;
        unsigned passed[3] = { yp, mp, yp + mp }, all[3] = { yp + yf, mp + mf, yp + yf + mp + mf };
        for (int c = 0; c < 3; c++)
        {
            std::string cell = percent(passed[c], all[c]);
            if (all[c])
                cell += "  (" + std::to_string(all[c]) + ")";
            text(vg, col0 + colWidth * (c + 1), rowY, cell, 20, all[c] ? textColor() : dimColor(),
                 NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
        }
    }
}
