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
    size_t firstFsrsRow = list->getChildren().size();
    // Presets set to RWKV in Anki desktop (the FSRS-7 fork) keep the setting;
    // AnkiNX schedules them with their FSRS version.
    rwkvNote = new brls::Label();
    rwkvNote->setFontSize(16);
    rwkvNote->setMarginLeft(16);
    rwkvNote->setMarginBottom(8);
    list->addView(rwkvNote);
    // FSRS-7 or FSRS-6, per preset; FSRS-5 and FSRS-4 only for a preset that
    // already uses one (Anki desktop's fork offers 7 and 6).
    std::vector<std::string> versions = { "7", "6" }, versionNames = { "FSRS-7", "FSRS-6" };
    std::string version = values.value("fsrs_version", "7");
    if (version == "5" || version == "4")
    {
        versions.push_back(version);
        versionNames.push_back("FSRS-" + version);
    }
    choice(t("fsrs_version"), "fsrs_version", versions, versionNames);
    number(t("desired_retention"), "desired_retention", true);
    paramsRow();
    auto* optimizeCell = new brls::DetailCell();
    optimizeCell->setText(t("optimize"));
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
    steps(t("fsrs_minimum_interval"), "fsrs_minimum_interval", "1s 10m 1h 1d");
    collectionToggle(t("fsrs_short_term_with_steps"), "fsrs_short_term_with_steps",
                     options.value("fsrs_short_term_with_steps", false));
    collectionToggle(t("fsrs_skip_learning_queues"), "fsrs_skip_learning_queues",
                     options.value("fsrs_skip_learning_queues", false));
    for (size_t i = firstFsrsRow; i < list->getChildren().size(); i++)
        fsrsRows.push_back(list->getChildren()[i]);
    fsrsParams = options.value("fsrs_params", 0);
    rwkv       = options.value("rwkv", "off");
    showParams();
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
    std::string version = "FSRS-" + values.value("fsrs_version", "7");
    if (rwkv == "instant" || rwkv == "curve")
        rwkvNote->setText(brls::getStr("ankinx/options/rwkv_note_" + rwkv, version));
}

void DeckOptionsActivity::showSchedulerRows()
{
    for (brls::View* row : fsrsRows)
        row->setVisibility(fsrs ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    for (brls::View* row : sm2Rows)
        row->setVisibility(fsrs ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    bool seven = values.value("fsrs_version", "7") == "7";
    for (brls::View* row : fsrs7Rows)
        row->setVisibility(fsrs && seven ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    // The note only for presets set to RWKV.
    if (fsrs && rwkv == "off")
        rwkvNote->setVisibility(brls::Visibility::GONE);
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
    for (auto& [arg, cell] : collectionToggles)
        cell->setOn(options.value(arg, false), false);
    fsrsParams = options.value("fsrs_params", 0);
    rwkv       = options.value("rwkv", "off");
    showParams();
    customScheduling = options.value("custom_scheduling", false);
    customSchedulingCell->setDetailText(customScheduling ? "hints/on"_i18n : "hints/off"_i18n);
    showSchedulerRows();
}

void DeckOptionsActivity::optimize()
{
    try
    {
        anki::call("optimize", { { "deck_id", deckId } });
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
        [id = deckId] { return anki::call("set_deck_options", { { "deck_id", id }, { "apply_optimized", true } }); },
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
