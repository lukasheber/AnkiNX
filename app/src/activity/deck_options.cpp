#include "activity/deck_options.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app.hpp"
#include "util/keyboard.hpp"
#include "util/settings.hpp"
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

// A dialog whose button doesn't close it (borealis' buttons do): it stays
// until its owner closes it, so only one place ever does.
class StayingDialog : public brls::Dialog
{
  public:
    StayingDialog(brls::Box* content, const std::string& button, std::function<void()> onPress)
        : brls::Dialog(content)
    {
        setCancelable(false);
        addButton(button, [] {});
        button1->registerClickAction([onPress](brls::View*) {
            onPress();
            return true;
        });
    }
};

// An optimization's progress dialog.
struct OptimizeRun
{
    StayingDialog* dialog = nullptr;
    brls::Label* label    = nullptr;
    bool cancelling       = false;
};

// Text styled as borealis' text dialogs; `label` gets the label, to update.
brls::Box* dialogText(const std::string& text, brls::Label** label)
{
    brls::Style style = brls::Application::getStyle();
    auto* textLabel   = new brls::Label();
    textLabel->setText(text);
    textLabel->setFontSize(style["brls/dialog/fontSize"]);
    textLabel->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    textLabel->setSingleLine(false);
    auto* box = new brls::Box();
    box->addView(textLabel);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    box->setPadding(style["brls/dialog/paddingTopBottom"], style["brls/dialog/paddingLeftRight"],
                    style["brls/dialog/paddingTopBottom"], style["brls/dialog/paddingLeftRight"]);
    *label = textLabel;
    return box;
}

// Follow the optimization until it ends, then close the dialog and pass on
// its result (on the UI thread).
void watchOptimize(std::shared_ptr<OptimizeRun> run, std::function<void(const anki::json&)> done)
{
    brls::delay(500, [run, done] {
        anki::json status;
        try
        {
            status = anki::call("optimize_status");
        }
        catch (const anki::Error& e)
        {
            status = { { "active", false }, { "result", { { "outcome", "error" }, { "message", e.what() } } } };
        }
        if (status.value("active", false))
        {
            if (!run->cancelling)
            {
                std::string progress = status.value("progress", "");
                run->label->setText(progress.empty() ? t("optimizing") : t("optimizing") + "\n\n" + progress);
            }
            watchOptimize(run, done);
            return;
        }
        brls::Application::getPlatform()->disableScreenDimming(false, "optimize", "AnkiNX");
        anki::json result = status["result"].is_object() ? status["result"] : anki::json::object();
        run->dialog->close([done, result] { done(result); });
    });
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

    // As the desktop's Lapses: the fork's two collection-wide same-day
    // switches under the relearning steps (with FSRS or RWKV-Instant, and with
    // FSRS), SM-2's minimum interval without FSRS.
    list->addView(header(t("lapses")));
    steps(t("relearn_steps"), "relearn_steps");
    sameDayRows.push_back(collectionToggle(t("fsrs_short_term_with_steps"), "fsrs_short_term_with_steps",
                                           options.value("fsrs_short_term_with_steps", false)));
    fsrsOnRows.push_back(collectionToggle(t("fsrs_skip_learning_queues"), "fsrs_skip_learning_queues",
                                          options.value("fsrs_skip_learning_queues", false)));
    number(t("minimum_interval"), "minimum_lapse_interval", false);
    sm2Rows.push_back(list->getChildren().back());
    number(t("leech_threshold"), "leech_threshold", false);
    choice(t("leech_action"), "leech_action", { "suspend", "tag" }, { t("leech_suspend"), t("leech_tag") });

    fillScheduler(options);

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
    // FSRS's same-day minimum interval, in Advanced as on the desktop.
    steps(label("fsrs_minimum_interval", t("fsrs_minimum_interval")), "fsrs_minimum_interval", "1s 10m 1h 1d");
    fsrsOnRows.push_back(list->getChildren().back());
    // SM-2's values, in Advanced as on the desktop (shown without FSRS).
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

// The desktop fork's wording (the backend's `labels`, from its translations),
// else AnkiNX's own.
std::string DeckOptionsActivity::label(const std::string& key, const std::string& fallback) const
{
    return labels.is_object() && labels[key].is_string() && !labels[key].get<std::string>().empty()
               ? labels[key].get<std::string>()
               : fallback;
}

brls::Label* DeckOptionsActivity::note(const std::string& text)
{
    // The fork's texts keep the line breaks of its translation files, which
    // its web page ignores: join lines, keep blank lines as paragraphs.
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
    // (descriptions: readable in both themes; text_disabled is for disabled rows)
    note->setTextColor(brls::Application::getTheme()["brls/header/subtitle"]);
    list->addView(note);
    return note;
}

// The Review scheduler's entry for what is saved: "SM-2" while FSRS is off.
int DeckOptionsActivity::schedulerIndex() const
{
    std::string current = fsrs ? values.value("scheduler", "fsrs-7") : "sm2";
    for (size_t i = 0; i < schedulerOptions.size(); i++)
        if (schedulerOptions[i] == current)
            return (int)i;
    return 0;
}

// A heading inside a section (the desktop cards' h2: "RWKV-Instant",
// "Same-Day Repeats"...), between a section header and a note.
brls::Label* DeckOptionsActivity::subheading(const std::string& text)
{
    auto* label = new brls::Label();
    label->setText(text);
    // (below a section header in size: a part of the section)
    label->setFontSize(17);
    label->setMarginTop(12);
    label->setMarginLeft(16);
    label->setMarginBottom(4);
    list->addView(label);
    return label;
}

// The rows added since `from`, into `rows` (for showing them together).
void DeckOptionsActivity::collect(size_t from, std::vector<brls::View*>& rows)
{
    for (size_t i = from; i < list->getChildren().size(); i++)
        rows.push_back(list->getChildren()[i]);
}

// As the desktop deck options' Scheduler section, then its RWKV and FSRS
// sections (ts/routes/deck-options: FsrsOptionsOuter, SchedulerOptions,
// RwkvOptions, FsrsOptions), with the desktop's wording: all rows exist, and
// showSchedulerRows() shows those the chosen schedulers use.
void DeckOptionsActivity::fillScheduler(const anki::json& options)
{
    labels = options["labels"];
    list->addView(header(label("scheduler", "Scheduler")));

    // The Review scheduler is the choice: picking one turns FSRS (Anki's
    // collection-wide switch) on, as the desktop's selector does, so there is
    // no separate on/off row. A collection still on SM-2 shows "SM-2" as the
    // current entry. FSRS-5 and FSRS-4 only for a preset that already uses
    // one, as on the desktop.
    std::string recommended = label("recommended", "");
    auto withBadge = [&recommended](const std::string& name) {
        return recommended.empty() ? name : name + " (" + recommended + ")";
    };
    schedulerOptions = { "fsrs-6", "fsrs-7", "rwkv-curve", "rwkv-instant" };
    std::vector<std::string> schedulerNames = { "FSRS-6", "FSRS-7", "RWKV-Curve", withBadge("RWKV-Instant") };
    std::string current = values.value("scheduler", "fsrs-7");
    if (current == "fsrs-5" || current == "fsrs-4")
    {
        schedulerOptions.insert(schedulerOptions.begin(), current);
        schedulerNames.insert(schedulerNames.begin(), current == "fsrs-5" ? "FSRS-5" : "FSRS-4");
    }
    if (!fsrs)
    {
        schedulerOptions.insert(schedulerOptions.begin(), "sm2");
        schedulerNames.insert(schedulerNames.begin(), "SM-2");
    }
    schedulerCell = new brls::SelectorCell();
    schedulerCell->init(label("review_scheduler", t("fsrs_version")), schedulerNames, schedulerIndex(),
                        [this](int index) {
                            if (index < 0 || index >= (int)schedulerOptions.size())
                                return;
                            const std::string& choice = schedulerOptions[index];
                            if (choice == "sm2")
                                apply({ { "fsrs", false } });
                            else
                                apply({ { "values", { { "scheduler", choice } } } });
                        });
    list->addView(schedulerCell);
    size_t from = list->getChildren().size();
    note(label("scheduler_disabled", ""));
    collect(from, sm2Rows);

    // With RWKV-Instant, the "Fallback" (due dates for syncing with clients
    // without RWKV); RWKV-Curve is the recommended one.
    from = list->getChildren().size();
    choice(label("due_dates", "Due dates"), "due_dates", { "fsrs-6", "fsrs-7", "rwkv-curve" },
           { "FSRS-6", "FSRS-7", withBadge("RWKV-Curve") });
    note(label("due_dates_description", ""));
    collect(from, dueDateRows);

    // Desired retention: the preset's ("Shared preset") and this deck's own
    // ("Deck only"), the desktop's two tabs.
    from = list->getChildren().size();
    number(label("desired_retention", t("desired_retention")) + " · " + label("shared_preset", "Shared preset"),
           "desired_retention", true);
    deckRetentionCell = new brls::DetailCell();
    deckRetentionCell->setText(label("desired_retention", t("desired_retention")) + " · " +
                               label("deck_only", "Deck only"));
    deckRetentionCell->registerClickAction([this](brls::View*) {
        keyboard::text(label("deck_only", "Deck only"), deckRetentionText(), 10, [this](const std::string& text) {
            if (text.empty())
            {
                apply({ { "deck", { { "desired_retention", nullptr } } } });
                return;
            }
            char* end     = nullptr;
            double number = std::strtod(text.c_str(), &end);
            if (*end != 0 || !std::isfinite(number))
            {
                brls::Application::notify(t("invalid"));
                return;
            }
            apply({ { "deck", { { "desired_retention", number } } } });
        });
        return true;
    });
    list->addView(deckRetentionCell);
    collect(from, retentionRows);

    fillRwkv();
    fillFsrs(options);
    deck      = options["deck"];
    rwkvModel = options["rwkv_model"];
    showDeckValues();
}

// The desktop's RWKV section: RWKV-Instant's and RWKV-Curve's options, then
// Advanced (the model, this console's; RWKV-Instant's queue updates).
void DeckOptionsActivity::fillRwkv()
{
    size_t start = list->getChildren().size();
    list->addView(header("RWKV"));

    auto titled = [this](const std::string& name, const std::string& subtitle) {
        return subtitle.empty() ? name : name + "  ·  " + subtitle;
    };
    size_t from = list->getChildren().size();
    subheading(titled("RWKV-Instant", label("rwkv_instant_subtitle", "")));
    note(label("rwkv_instant_description", ""));
    number(label("rwkv_minimum_reviews_per_day", "Minimum reviews per day"), "rwkv_minimum_reviews_per_day", false);
    respectSubdeckCell = new brls::BooleanCell();
    respectSubdeckCell->init(label("rwkv_respect_subdeck_minimums", "Also honor subdeck minimums"), false, [this](bool on) {
        apply({ { "deck", { { "rwkv_respect_subdeck_minimums", on } } } });
    });
    list->addView(respectSubdeckCell);
    // The desktop applies the switch to the subdecks when saving; here,
    // where each change saves at once, it is an action: asked, then done,
    // and only for a deck that has subdecks.
    applySubdecksCell = new brls::DetailCell();
    applySubdecksCell->setText(t("rwkv_apply_subdecks"));
    applySubdecksCell->registerClickAction([this](brls::View*) {
        bool on      = deck.value("rwkv_respect_subdeck_minimums", false);
        auto* dialog = new brls::Dialog(t(on ? "rwkv_apply_subdecks_on" : "rwkv_apply_subdecks_off"));
        dialog->addButton("hints/cancel"_i18n, [] {});
        dialog->addButton(t("rwkv_apply_subdecks_confirm"), [this, on, token = alive] {
            if (!*token)
                return;
            apply({ { "deck",
                      { { "rwkv_respect_subdeck_minimums", on },
                        { "rwkv_apply_respect_subdeck_minimums_to_children", true } } } },
                  t("rwkv_apply_subdecks_done"));
        });
        dialog->open();
        return true;
    });
    list->addView(applySubdecksCell);
    subheading(label("rwkv_same_day_repeats", "Same-Day Repeats"));
    number(label("rwkv_min_intervening_reviews", "Reviews between repeats"), "rwkv_min_intervening_reviews", false);
    number(label("rwkv_min_elapsed_secs", "Seconds between repeats"), "rwkv_min_elapsed_secs", false);
    collect(from, instantRows);

    from = list->getChildren().size();
    subheading(titled("RWKV-Curve", label("rwkv_curve_subtitle", "")));
    note(label("rwkv_curve_description", ""));
    toggle(label("rwkv_enforce_grade_order", "Keep answer buttons in order"), "rwkv_enforce_grade_order");
    collect(from, curveRows);

    subheading(label("advanced", t("advanced")));
    // The model: this console's, as the desktop's is its profile's.
    rwkvSizeCell = new brls::SelectorCell();
    rwkvSizeCell->init(label("rwkv_model", "Model"), { label("rwkv_model_small", "Small"), label("rwkv_model_tiny", "Tiny") },
                       0, [this](int index) {
                           if (index < 0 || index > 1)
                               return;
                           std::string size = index == 1 ? "tiny" : "small";
                           settings::set("rwkv_size", size);
                           setRwkvModel({ { "size", size } });
                       });
    list->addView(rwkvSizeCell);
    singleModelCell = new brls::BooleanCell();
    singleModelCell->init(label("rwkv_single_model", "Use one model for both schedulers"), false, [this](bool on) {
        settings::set("rwkv_single_model", on);
        setRwkvModel({ { "single_model", on } });
    });
    list->addView(singleModelCell);
    // (the desktop's scope note speaks of "this computer" and a rebuild prompt)
    note(t("rwkv_model_scope"));
    from = list->getChildren().size();
    subheading("RWKV-Instant");
    toggle(label("rwkv_precompute_next_card", "Precompute next card"), "rwkv_precompute_next_card");
    toggle(label("rwkv_candidate_refresh", "Faster, approximate queue updates"), "rwkv_candidate_refresh");
    number(label("rwkv_refresh_interval", "Update the RWKV queue every"), "rwkv_refresh_interval", false);
    collect(from, instantRows);
    collect(start, rwkvRows);
}

// The desktop's FSRS section: parameters (optimized on the console), the
// FSRS-7 options, and the fork's same-day options.
void DeckOptionsActivity::fillFsrs(const anki::json& options)
{
    size_t start = list->getChildren().size();
    list->addView(header("FSRS"));
    paramsRow();
    auto* optimizeCell = new brls::DetailCell();
    optimizeCell->setText(label("optimize", t("optimize")));
    optimizeCell->registerClickAction([this](brls::View*) {
        optimize();
        return true;
    });
    list->addView(optimizeCell);
    // FSRS-7's optimizer options, per preset (as in the fork's deck options).
    for (const char* key : { "fsrs_include_same_day", "fsrs_scheduling_penalties" })
    {
        toggle(t(key), key);
        fsrs7Rows.push_back(list->getChildren().back());
    }
    // Dynamic desired retention (FSRS-7): Anki can't schedule with it on
    // before an optimization has calibrated it, so turning it on without a
    // calibration offers that optimization (which turns it on).
    auto* dynamicCell = new brls::BooleanCell();
    dynamicCell->init(t("fsrs_dynamic_retention"), values.value("fsrs_dynamic_retention", false), [this](bool on) {
        if (on && !values.value("fsrs_dynamic_retention_calibrated", false))
        {
            toggles["fsrs_dynamic_retention"]->setOn(false, false);
            auto* dialog = new brls::Dialog(t("fsrs_dynamic_retention_calibrate"));
            dialog->addButton("hints/cancel"_i18n, [] {});
            dialog->addButton(label("optimize", t("optimize")), [this, token = alive] {
                brls::sync([this, token] {
                    if (*token)
                        optimize(true);
                });
            });
            dialog->open();
            return;
        }
        apply({ { "values", { { "fsrs_dynamic_retention", on } } } });
    });
    toggles["fsrs_dynamic_retention"] = dynamicCell;
    list->addView(dynamicCell);
    fsrs7Rows.push_back(dynamicCell);
    toggle(t("fsrs_dynamic_retention_clamp"), "fsrs_dynamic_retention_clamp");
    dynamicRows.push_back(list->getChildren().back());
    // The searches the optimizer uses (empty: the preset's cards).
    search(t("param_search"), "param_search");
    search(t("evaluation_search"), "evaluation_search");
    // As the desktop's: for the changes made here, not kept.
    auto* rescheduleCell = new brls::BooleanCell();
    rescheduleCell->init(label("reschedule", t("reschedule")), false, [this](bool on) {
        reschedule = on;
        rescheduleWarning->setVisibility(on ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    });
    list->addView(rescheduleCell);
    rescheduleWarning = note(label("reschedule_warning", ""));
    rescheduleWarning->setVisibility(brls::Visibility::GONE);
    collectionToggle(label("health_check", t("health_check")), "fsrs_health_check",
                     options.value("fsrs_health_check", false));
    collect(start, fsrsSectionRows);
    fsrsParams = options.value("fsrs_params", 0);
    showParams();
}

std::string DeckOptionsActivity::deckRetentionText() const
{
    return deck.is_object() && deck["desired_retention"].is_number() ? formatValue(deck["desired_retention"], true) : "";
}

// The deck's own options and this console's model choice.
void DeckOptionsActivity::showDeckValues()
{
    std::string own = deckRetentionText();
    deckRetentionCell->setDetailText(own.empty() ? t("deck_retention_preset") : own);
    respectSubdeckCell->setOn(deck.is_object() && deck.value("rwkv_respect_subdeck_minimums", false), false);
    if (rwkvModel.is_object())
    {
        rwkvSizeCell->setSelection(rwkvModel.value("size", "small") == "tiny" ? 1 : 0, true);
        singleModelCell->setOn(rwkvModel.value("single_model", false), false);
    }
}

// This console's RWKV model (Advanced): applied at once (RWKV reads the
// review history again for it).
void DeckOptionsActivity::setRwkvModel(anki::json args)
{
    brls::async([this, args, token = alive] {
        std::string error;
        try
        {
            anki::call("rwkv_set_model", args);
        }
        catch (const anki::Error& e)
        {
            error = e.what();
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

void DeckOptionsActivity::steps(const std::string& title, const std::string& key, const std::string& format)
{
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(values.value(key, ""));
    cell->registerClickAction([this, title, key, format](brls::View*) {
        keyboard::text(title + " (" + format + ")", values.value(key, ""), 60, [this, key](const std::string& text) {
            apply({ { "values", { { key, text } } } });
        });
        return true;
    });
    details[key] = { cell, false };
    list->addView(cell);
}

// A search of Anki's (empty: the default).
void DeckOptionsActivity::search(const std::string& title, const std::string& key)
{
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(values.value(key, ""));
    cell->registerClickAction([this, title, key](brls::View*) {
        keyboard::text(title, values.value(key, ""), 200, [this, key](const std::string& text) {
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

brls::BooleanCell* DeckOptionsActivity::collectionToggle(const std::string& title, const std::string& arg, bool on)
{
    auto* cell = new brls::BooleanCell();
    cell->init(title, on, [this, arg](bool on) { apply({ { arg, on } }); });
    collectionToggles[arg] = cell;
    list->addView(cell);
    return cell;
}

void DeckOptionsActivity::paramsRow()
{
    paramsCell = new brls::DetailCell();
    paramsCell->setText(t("fsrs_params"));
    paramsCell->registerClickAction([this](brls::View*) {
        if (fsrsParams == 0)
        {
            brls::Application::notify(t("fsrs_params_note"));
            return true;
        }
        auto* dialog = new brls::Dialog(t("fsrs_params_reset_confirm"));
        dialog->addButton("hints/cancel"_i18n, [] {});
        dialog->addButton(t("fsrs_params_reset"), [this, token = alive] {
            if (*token)
                apply({ { "reset_fsrs_params", true } });
        });
        dialog->open();
        return true;
    });
    list->addView(paramsCell);
}

// The parameters row and the RWKV note follow the preset's values.
void DeckOptionsActivity::showParams()
{
    paramsCell->setDetailText(t(fsrsParams == 0 ? "fsrs_params_default" : "fsrs_params_optimized"));
}

// As the desktop shows them: Due dates with RWKV-Instant; desired retention
// and the FSRS section with FSRS on or RWKV-Curve (it writes S90 with FSRS's
// parameters); the RWKV section for presets using RWKV, its Instant and Curve
// parts as used; SM-2's values without FSRS.
void DeckOptionsActivity::showSchedulerRows()
{
    bool instant = values.value("scheduler", "") == "rwkv-instant";
    // (`due_dates`: what writes the intervals, the review scheduler itself
    // unless that is RWKV-Instant, whose Fallback it is)
    std::string dueDates = values.value("due_dates", "");
    bool curve           = dueDates == "rwkv-curve";
    // The FSRS section only when FSRS is the scheduler or the Fallback.
    bool usesFsrs = fsrs && dueDates.rfind("fsrs-", 0) == 0;
    bool seven    = usesFsrs && values.value("fsrs_version", "7") == "7";
    auto show    = [](std::vector<brls::View*>& rows, bool on) {
        for (brls::View* row : rows)
            row->setVisibility(on ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    };
    show(sm2Rows, !fsrs);
    show(fsrsOnRows, fsrs);
    show(sameDayRows, fsrs || instant);
    show(dueDateRows, instant);
    show(retentionRows, fsrs || curve);
    show(rwkvRows, instant || curve);
    show(instantRows, instant);
    if (applySubdecksCell)
        applySubdecksCell->setVisibility(instant && deck.is_object() && deck.value("subdecks", 0) > 0
                                             ? brls::Visibility::VISIBLE
                                             : brls::Visibility::GONE);
    show(curveRows, curve);
    show(fsrsSectionRows, usesFsrs);
    show(fsrs7Rows, seven);
    show(dynamicRows, seven && values.value("fsrs_dynamic_retention", false));
    // (in the FSRS section, but only while its switch is on)
    if (rescheduleWarning)
        rescheduleWarning->setVisibility(usesFsrs && reschedule ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
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
    schedulerCell->setSelection(schedulerIndex(), true);
    for (auto& [arg, cell] : collectionToggles)
        cell->setOn(options.value(arg, false), false);
    fsrsParams = options.value("fsrs_params", 0);
    showParams();
    deck      = options["deck"];
    rwkvModel = options["rwkv_model"];
    showDeckValues();
    customScheduling = options.value("custom_scheduling", false);
    customSchedulingCell->setDetailText(customScheduling ? "hints/on"_i18n : "hints/off"_i18n);
    showSchedulerRows();
}

void DeckOptionsActivity::optimize(bool calibrate)
{
    try
    {
        anki::call("optimize", { { "deck_id", deckId }, { "calibrate_dynamic_retention", calibrate } });
    }
    catch (const anki::Error& e)
    {
        // Both report through the collection's one progress.
        sync_flow::showMessage(e.kind == "media_sync" ? t("optimize_wait_media") : std::string(e.what()));
        return;
    }
    auto run   = std::make_shared<OptimizeRun>();
    auto* text = dialogText(t("optimizing"), &run->label);
    run->dialog = new StayingDialog(text, "hints/cancel"_i18n, [run] {
        if (run->cancelling)
            return;
        run->cancelling = true;
        run->label->setText(t("optimize_stopping"));
        try
        {
            anki::call("optimize_cancel");
        }
        catch (const anki::Error&)
        {
        }
    });
    run->dialog->open();
    // It can take minutes: don't dim the screen meanwhile.
    brls::Application::getPlatform()->disableScreenDimming(true, "optimize", "AnkiNX");
    watchOptimize(run, [this, token = alive](const anki::json& result) {
        if (*token)
            showOptimized(result);
    });
}

void DeckOptionsActivity::showOptimized(const anki::json& result)
{
    std::string outcome = result.value("outcome", "");
    if (outcome == "cancelled")
        return;
    if (outcome != "changed")
    {
        // "Already optimal", "no reviews" (Anki's texts), or an error. An
        // optimal one may have saved a calibration of dynamic retention.
        sync_flow::showMessage(result.value("message", ""));
        load();
        return;
    }
    auto fit = [&result](const char* side, const char* key) {
        return result.contains(side) && result[side].is_object() ? result[side].value(key, "") : std::string();
    };
    std::string text = brls::getStr("ankinx/options/optimize_compare", fit("current", "log_loss"),
                                    fit("optimized", "log_loss"), fit("current", "rmse"), fit("optimized", "rmse"));
    text += "\n\n" + result.value("note", "");
    if (result["health"].is_string())
        text += "\n\n" + result["health"].get<std::string>();
    auto* dialog = new brls::Dialog(text);
    dialog->addButton(t("optimize_keep"), [] {});
    dialog->addButton(t("optimize_use"), [this, token = alive] {
        // (after the dialog has gone: another one opens)
        brls::sync([this, token] {
            if (*token)
                applyOptimized();
        });
    });
    dialog->open();
}

// Saving updates the memory state of the preset's cards: it can take a while.
void DeckOptionsActivity::applyOptimized()
{
    sync_flow::blocking(
        t("optimize_saving"),
        [id = deckId, reschedule = reschedule] {
            return anki::call("set_deck_options",
                              { { "deck_id", id }, { "apply_optimized", true }, { "reschedule", reschedule } });
        },
        [this, token = alive](const anki::json*, const anki::Error* error) {
            if (!*token)
                return;
            if (error)
                sync_flow::showMessage(error->what());
            else
                brls::Application::notify(t("optimize_saved"));
            load();
        });
}

// Rows show the backend's values: after a change, the saved values are shown
// again (a refused change rebuilds the list, so switches go back too).
void DeckOptionsActivity::apply(anki::json args, std::string done)
{
    args["deck_id"] = deckId;
    if (reschedule)
        args["reschedule"] = true;
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
