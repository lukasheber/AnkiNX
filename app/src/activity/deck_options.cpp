#include "activity/deck_options.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app.hpp"
#include "util/keyboard.hpp"

using namespace brls::literals;

namespace
{

std::string t(const std::string& key) { return brls::getStr("ankinx/options/" + key); }

brls::Header* header(const std::string& title)
{
    auto* h = new brls::Header();
    h->setTitle(title);
    return h;
}

// 2.5 -> "2.50", 20 -> "20".
std::string formatValue(const anki::json& value, bool decimal)
{
    if (!value.is_number())
        return value.is_string() ? value.get<std::string>() : "";
    char buf[32];
    if (decimal)
        std::snprintf(buf, sizeof buf, "%.2f", value.get<double>());
    else
        std::snprintf(buf, sizeof buf, "%lld", (long long)value.get<double>());
    return buf;
}

} // namespace

DeckOptionsActivity::DeckOptionsActivity(int64_t deckId)
    : deckId(deckId)
{
}

DeckOptionsActivity::~DeckOptionsActivity() { *alive = false; }

brls::View* DeckOptionsActivity::createContentView()
{
    list = new brls::Box(brls::Axis::COLUMN);
    list->setPadding(20, 40, 30, 40);
    scroller = new brls::ScrollingFrame();
    scroller->setContentView(list);
    scroller->setGrow(1.0f);
    frame = new brls::AppletFrame(scroller);
    frame->setTitle(t("title"));
    return frame;
}

void DeckOptionsActivity::onContentAvailable()
{
    app::hideStatus(frame);
    load();
}

void DeckOptionsActivity::load()
{
    brls::async([this, token = alive, id = deckId] {
        try
        {
            anki::json options = anki::call("deck_options", { { "deck_id", id } });
            brls::sync([this, token, options] {
                if (!*token)
                    return;
                if (built)
                    refresh(options);
                else
                    fill(options);
            });
        }
        catch (const anki::Error& e)
        {
            std::string text = e.what();
            brls::sync([token, text] {
                if (*token)
                    brls::Application::notify(text);
            });
        }
    });
}

void DeckOptionsActivity::fill(const anki::json& options)
{
    values = options["values"];
    fsrs   = options.value("fsrs", false);
    built  = true;

    // Which preset this is; a shared one changes other decks too.
    int decks = options.value("deck_count", 1);
    auto* preset = new brls::DetailCell();
    preset->setText(t("preset"));
    preset->setDetailText(options.value("preset", ""));
    list->addView(preset);
    if (decks > 1)
    {
        auto* note = new brls::Label();
        note->setText(brls::getStr("ankinx/options/shared", std::to_string(decks)));
        note->setFontSize(16);
        note->setMarginLeft(16);
        note->setMarginBottom(8);
        list->addView(note);
    }

    list->addView(header(t("daily_limits")));
    number(t("new_per_day"), "new_per_day", false);
    number(t("reviews_per_day"), "reviews_per_day", false);

    list->addView(header(t("new_cards")));
    steps(t("learn_steps"), "learn_steps");
    number(t("graduating_interval"), "graduating_interval_good", false);
    number(t("easy_interval"), "graduating_interval_easy", false);

    list->addView(header(t("lapses")));
    steps(t("relearn_steps"), "relearn_steps");
    number(t("minimum_interval"), "minimum_lapse_interval", false);
    number(t("leech_threshold"), "leech_threshold", false);
    choice(t("leech_action"), "leech_action", { "suspend", "tag" }, { t("leech_suspend"), t("leech_tag") });

    list->addView(header(t("burying")));
    toggle(t("bury_new"), "bury_new");
    toggle(t("bury_reviews"), "bury_reviews");
    toggle(t("bury_interday"), "bury_interday_learning");

    list->addView(header(t("audio")));
    toggle(t("no_autoplay"), "disable_autoplay");
    toggle(t("skip_question_audio"), "skip_question_when_replaying_answer");

    list->addView(header(t("timer")));
    toggle(t("show_timer"), "show_timer");
    toggle(t("stop_timer"), "stop_timer_on_answer");
    number(t("cap_answer_time"), "cap_answer_time_to_secs", false);

    list->addView(header(t("auto_advance")));
    number(t("question_secs"), "seconds_to_show_question", true);
    number(t("answer_secs"), "seconds_to_show_answer", true);
    toggle(t("wait_for_audio"), "wait_for_audio");
    choice(t("question_action"), "question_action", { "show_answer", "reminder" },
           { t("qa_show_answer"), t("qa_reminder") });
    choice(t("answer_action"), "answer_action", { "bury", "again", "good", "hard", "reminder" },
           { t("aa_bury"), t("aa_again"), t("aa_good"), t("aa_hard"), t("aa_reminder") });

    list->addView(header(t("advanced")));
    number(t("maximum_interval"), "maximum_review_interval", false);
    // FSRS is collection-wide in Anki; turning it on computes memory states.
    fsrsCell = new brls::BooleanCell();
    fsrsCell->init(t("fsrs"), fsrs, [this](bool on) { apply({ { "fsrs", on } }); });
    list->addView(fsrsCell);
    // Both sets of rows exist; FSRS shows one or the other (rebuilding the
    // list would delete the focused row).
    number(t("desired_retention"), "desired_retention", true);
    fsrsRows.push_back(list->getChildren().back());
    for (const char* key : { "starting_ease", "easy_bonus", "interval_modifier", "hard_interval", "new_interval" })
    {
        number(t(key), key, true);
        sm2Rows.push_back(list->getChildren().back());
    }
    showSchedulerRows();
    // The list arrives after the screen did: focus its first row.
    if (brls::Application::getActivitiesStack().back() == this)
        brls::Application::giveFocus(list);
}

void DeckOptionsActivity::number(const std::string& title, const std::string& key, bool decimal)
{
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(formatValue(values[key], decimal));
    cell->registerClickAction([this, title, key, decimal](brls::View*) {
        keyboard::text(title, formatValue(values[key], decimal), 10, [this, key, decimal](const std::string& text) {
            char* end     = nullptr;
            double number = std::strtod(text.c_str(), &end);
            if (text.empty() || *end != 0 || !std::isfinite(number))
            {
                brls::Application::notify(t("invalid"));
                return;
            }
            anki::json value = decimal ? anki::json(number) : anki::json((int64_t)std::llround(number));
            apply({ { "values", { { key, value } } } });
        });
        return true;
    });
    details[key] = { cell, decimal };
    list->addView(cell);
}

void DeckOptionsActivity::steps(const std::string& title, const std::string& key)
{
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(values.value(key, ""));
    cell->registerClickAction([this, title, key](brls::View*) {
        keyboard::text(title + " (1m 10m 1d)", values.value(key, ""), 60, [this, key](const std::string& text) {
            apply({ { "values", { { key, text } } } });
        });
        return true;
    });
    details[key] = { cell, false };
    list->addView(cell);
}

void DeckOptionsActivity::choice(const std::string& title, const std::string& key,
                                 const std::vector<std::string>& options, const std::vector<std::string>& names)
{
    int selected = 0;
    for (size_t i = 0; i < options.size(); i++)
        if (values.value(key, "") == options[i])
            selected = (int)i;
    auto* cell = new brls::SelectorCell();
    cell->init(title, names, selected, [this, key, options](int index) {
        if (index >= 0 && index < (int)options.size())
            apply({ { "values", { { key, options[index] } } } });
    });
    choices[key] = { cell, options };
    list->addView(cell);
}

void DeckOptionsActivity::toggle(const std::string& title, const std::string& key)
{
    auto* cell = new brls::BooleanCell();
    cell->init(title, values.value(key, false), [this, key](bool on) { apply({ { "values", { { key, on } } } }); });
    toggles[key] = cell;
    list->addView(cell);
}

void DeckOptionsActivity::showSchedulerRows()
{
    for (brls::View* row : fsrsRows)
        row->setVisibility(fsrs ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    for (brls::View* row : sm2Rows)
        row->setVisibility(fsrs ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

// Every row shows what the backend saved (a refused change goes back too);
// rows are updated, not rebuilt, so the focused one stays.
void DeckOptionsActivity::refresh(const anki::json& options)
{
    values = options["values"];
    fsrs   = options.value("fsrs", false);
    for (auto& [key, row] : details)
        row.first->setDetailText(formatValue(values[key], row.second));
    for (auto& [key, cell] : toggles)
        cell->setOn(values.value(key, false), false);
    for (auto& [key, choice] : choices)
        for (size_t i = 0; i < choice.second.size(); i++)
            if (values.value(key, "") == choice.second[i])
                choice.first->setSelection((int)i, true);
    fsrsCell->setOn(fsrs, false);
    showSchedulerRows();
}

// Rows show the backend's values: after a change, the saved values are shown
// again (a refused change rebuilds the list, so switches go back too).
void DeckOptionsActivity::apply(anki::json args)
{
    args["deck_id"] = deckId;
    brls::async([this, args, token = alive] {
        std::string error;
        try
        {
            anki::call("set_deck_options", args);
        }
        catch (const anki::Error& e)
        {
            error = e.kind == "args" ? t("invalid") : std::string(e.what());
        }
        brls::sync([this, token, error] {
            if (!*token)
                return;
            if (!error.empty())
                brls::Application::notify(error);
            load();
        });
    });
}
