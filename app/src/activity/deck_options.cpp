#include "activity/deck_options.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app.hpp"
#include "util/keyboard.hpp"
#include "util/sync_flow.hpp"

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
    values           = options["values"];
    fsrs             = options.value("fsrs", false);
    labels           = options["labels"];
    orderChoicesJson = options["choices"];
    built            = true;

    // The deck's preset, chosen among all as with the desktop's selector,
    // and its menu (add, clone, rename, remove, save to all subdecks). A
    // shared one changes other decks too.
    presetCell = new brls::SelectorCell();
    presetCell->init(t("preset"), { "" }, 0, [this](int index) {
        if (index >= 0 && index < (int)presetIds.size() && presetIds[index] != presetId)
            apply({ { "preset", { { "select", presetIds[index] } } } });
    });
    list->addView(presetCell);
    auto* presetMenu = new brls::DetailCell();
    presetMenu->setText(t("preset_menu"));
    presetMenu->registerClickAction([this](brls::View*) {
        presetActions();
        return true;
    });
    list->addView(presetMenu);
    sharedNote = new brls::Label();
    sharedNote->setFontSize(16);
    sharedNote->setMarginLeft(16);
    sharedNote->setMarginBottom(8);
    list->addView(sharedNote);
    showPresets(options);

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

    fillDisplayOrder();

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

    fillEasyDays();

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
    // Custom scheduling: JavaScript for every deck, written in Anki desktop
    // (AnkiNX runs it when studying); here whether there is some, and a way
    // to delete it, as the reset button in Anki's deck options. FSRS leaves
    // it alone, as in Anki.
    customScheduling     = options.value("custom_scheduling", false);
    customSchedulingCell = new brls::DetailCell();
    customSchedulingCell->setText(t("custom_scheduling"));
    customSchedulingCell->setDetailText(customScheduling ? "hints/on"_i18n : "hints/off"_i18n);
    customSchedulingCell->registerClickAction([this](brls::View*) {
        if (!customScheduling)
        {
            brls::Application::notify(t("custom_scheduling_note"));
            return true;
        }
        auto* dialog = new brls::Dialog(t("custom_scheduling_off_confirm"));
        dialog->addButton("hints/cancel"_i18n, [] {});
        dialog->addButton(t("custom_scheduling_off"), [this, token = alive] {
            if (*token)
                apply({ { "custom_scheduling", false } });
        });
        dialog->open();
        return true;
    });
    list->addView(customSchedulingCell);
    showSchedulerRows();
    // The list arrives after the screen did: focus its first row.
    if (brls::Application::getActivitiesStack().back() == this)
        brls::Application::giveFocus(list);
}

// The desktop's wording (the backend's `labels`, from Anki's translations),
// else AnkiNX's own.
std::string DeckOptionsActivity::label(const std::string& key, const std::string& fallback) const
{
    return labels.is_object() && labels[key].is_string() && !labels[key].get<std::string>().empty()
               ? labels[key].get<std::string>()
               : fallback;
}

brls::Label* DeckOptionsActivity::note(const std::string& text)
{
    // Anki's texts keep the line breaks of its translation files, which its
    // web page ignores: join lines, keep blank lines as paragraphs.
    std::string flowed;
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] != '\n')
            flowed += text[i];
        else if (i + 1 < text.size() && text[i + 1] == '\n')
        {
            flowed += "\n\n";
            while (i + 1 < text.size() && text[i + 1] == '\n')
                i++;
        }
        else
            flowed += ' ';
    }
    auto* note = new brls::Label();
    note->setText(flowed);
    note->setFontSize(16);
    note->setMarginLeft(16);
    note->setMarginRight(16);
    note->setMarginBottom(8);
    // (the full row width: some notes wrapped at their first measure)
    note->setWidthPercentage(100);
    // (readable in both themes)
    note->setTextColor(brls::Application::getTheme()["brls/header/subtitle"]);
    list->addView(note);
    return note;
}

// The presets (the desktop's list: by name, with how many decks use each),
// the deck's selected, and the shared note.
void DeckOptionsActivity::showPresets(const anki::json& options)
{
    presetId      = options.value("preset_id", (int64_t)0);
    presetName    = options.value("preset", "");
    removeConfirm = options.value("remove_preset_confirm", "");
    std::vector<std::string> names;
    presetIds.clear();
    int selected = 0;
    if (options["presets"].is_array())
        for (const auto& entry : options["presets"])
        {
            if (entry.value("id", (int64_t)0) == presetId)
                selected = (int)names.size();
            presetIds.push_back(entry.value("id", (int64_t)0));
            names.push_back(entry.value("label", entry.value("name", "")));
        }
    if (names.empty())
        names.push_back(presetName);
    presetCell->setData(names);
    presetCell->setSelection(selected, true);
    int decks = options.value("deck_count", 1);
    sharedNote->setText(brls::getStr("ankinx/options/shared", std::to_string(decks)));
    sharedNote->setVisibility(decks > 1 ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

// The desktop's Save menu, for presets: each action saves at once.
void DeckOptionsActivity::presetActions()
{
    std::vector<std::string> names = { label("add_preset", "Add Preset"), label("clone_preset", "Clone Preset"),
                                       label("rename_preset", "Rename Preset"),
                                       label("remove_preset", "Remove Preset"),
                                       label("save_to_all_subdecks", "Save to All Subdecks") };
    auto* dropdown = new brls::Dropdown(t("preset_menu"), names, [this, token = alive](int choice) {
        // (after the menu has closed: the next step opens a keyboard or dialog)
        brls::sync([this, token, choice] {
            if (!*token)
                return;
            std::string prompt = label("preset_name_prompt", "Name");
            auto named         = [this, prompt](const std::string& title, const std::string& action,
                                        const std::string& initial) {
                keyboard::text(title + " · " + prompt, initial, 60, [this, action](const std::string& text) {
                    if (text.find_first_not_of(' ') == std::string::npos)
                        return;
                    apply({ { "preset", { { action, text } } } });
                });
            };
            switch (choice)
            {
                case 0:
                    named(label("add_preset", "Add Preset"), "add", "");
                    break;
                case 1:
                    named(label("clone_preset", "Clone Preset"), "clone", presetName);
                    break;
                case 2:
                    named(label("rename_preset", "Rename Preset"), "rename", presetName);
                    break;
                case 3:
                {
                    if (presetId == 1)
                    {
                        sync_flow::showMessage(label("default_preset_cant_be_removed", ""));
                        break;
                    }
                    // As the desktop asks: removing a preset needs a one-way sync.
                    std::string text = label("will_require_full_sync", "") + "\n\n" + removeConfirm;
                    auto* dialog     = new brls::Dialog(text);
                    dialog->addButton("hints/cancel"_i18n, [] {});
                    dialog->addButton(label("remove_preset", "Remove Preset"), [this, token] {
                        if (*token)
                            apply({ { "preset", { { "remove", true } } } });
                    });
                    dialog->open();
                    break;
                }
                case 4:
                    apply({ { "preset", { { "apply_to_subdecks", true } } } }, t("preset_saved_to_subdecks"));
                    break;
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(app::withoutStatus(dropdown)));
}

// The desktop's Display Order (DisplayOrder.svelte): its choices, in its
// order and words (the backend's choices: the sort orders the gather order
// allows, the review orders FSRS has).
void DeckOptionsActivity::fillDisplayOrder()
{
    list->addView(header(label("display_order", "Display Order")));
    for (const char* key : { "new_gather_order", "new_sort_order", "new_mix", "interday_learning_mix", "review_order" })
        orderChoice(key);
}

void DeckOptionsActivity::orderChoice(const std::string& key)
{
    auto* cell = new brls::SelectorCell();
    cell->init(label(key, key), { "" }, 0, [this, key](int index) {
        const std::vector<int>& options = orderChoices[key].second;
        if (index >= 0 && index < (int)options.size() && options[index] != values.value(key, -1))
            apply({ { "values", { { key, options[index] } } } });
    });
    orderChoices[key] = { cell, {} };
    list->addView(cell);
    showOrderChoices();
}

// Each choice row's entries and the saved one (the entries can change: the
// sort orders follow the gather order).
void DeckOptionsActivity::showOrderChoices()
{
    for (auto& [key, row] : orderChoices)
    {
        std::vector<std::string> names;
        row.second.clear();
        int selected = 0;
        if (orderChoicesJson.is_object() && orderChoicesJson[key].is_array())
            for (const auto& entry : orderChoicesJson[key])
            {
                int value = entry.value("value", 0);
                if (value == values.value(key, 0))
                    selected = (int)names.size();
                row.second.push_back(value);
                names.push_back(entry.value("label", ""));
            }
        if (names.empty())
            continue;
        row.first->setData(names);
        row.first->setSelection(selected, true);
    }
}

// The desktop's Easy Days (EasyDays.svelte): each weekday's reviews (its
// slider: Minimum, Reduced, Normal), and its two warnings.
void DeckOptionsActivity::fillEasyDays()
{
    list->addView(header(label("easy_days", "Easy Days")));
    std::vector<std::string> levels = { "Minimum", "Reduced", "Normal" };
    if (labels.is_object() && labels["easy_days_levels"].is_array() && labels["easy_days_levels"].size() == 3)
        levels = labels["easy_days_levels"].get<std::vector<std::string>>();
    const char* weekdays[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
    for (int day = 0; day < 7; day++)
    {
        std::string name = weekdays[day];
        if (labels.is_object() && labels["easy_days_weekdays"].is_array() && labels["easy_days_weekdays"].size() == 7)
            name = labels["easy_days_weekdays"][day].get<std::string>();
        auto* cell = new brls::SelectorCell();
        cell->init(name, levels, 2, [this, day](int index) {
            if (index < 0 || index > 2 || !values["easy_days"].is_array() || values["easy_days"].size() != 7)
                return;
            anki::json days = values["easy_days"];
            if (days[day].get<double>() == index / 2.0)
                return;
            days[day] = index / 2.0;
            // (existing reviews keep their dates: the desktop warns)
            easyDaysChanged = true;
            apply({ { "values", { { "easy_days", days } } } });
        });
        easyDayCells.push_back(cell);
        list->addView(cell);
    }
    noNormalDayNote    = note(label("easy_days_no_normal_days", ""));
    easyDaysChangeNote = note(label("easy_days_change", ""));
    showEasyDays();
}

void DeckOptionsActivity::showEasyDays()
{
    bool normalDay = false;
    if (values["easy_days"].is_array() && values["easy_days"].size() == 7)
        for (size_t day = 0; day < 7 && day < easyDayCells.size(); day++)
        {
            double share = values["easy_days"][day].get<double>();
            normalDay    = normalDay || share == 1.0;
            // (the nearest level: other values come only from older versions)
            easyDayCells[day]->setSelection((int)std::lround(std::fmin(std::fmax(share, 0.0), 1.0) * 2), true);
        }
    noNormalDayNote->setVisibility(normalDay ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    easyDaysChangeNote->setVisibility(easyDaysChanged ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
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
    labels           = options["labels"];
    orderChoicesJson = options["choices"];
    showOrderChoices();
    showEasyDays();
    showPresets(options);
    customScheduling = options.value("custom_scheduling", false);
    customSchedulingCell->setDetailText(customScheduling ? "hints/on"_i18n : "hints/off"_i18n);
    showSchedulerRows();
}

// Rows show the backend's values: after a change, the saved values are shown
// again (a refused change rebuilds the list, so switches go back too).
void DeckOptionsActivity::apply(anki::json args, std::string done)
{
    args["deck_id"] = deckId;
    brls::async([this, args, done, token = alive] {
        std::string error;
        try
        {
            anki::call("set_deck_options", args);
        }
        catch (const anki::Error& e)
        {
            error = e.kind == "args" ? t("invalid") : std::string(e.what());
        }
        brls::sync([this, token, error, done] {
            if (!*token)
                return;
            if (!error.empty())
                brls::Application::notify(error);
            else if (!done.empty())
                brls::Application::notify(done);
            load();
        });
    });
}
