//! The desktop deck options' Display order section
//! (ts/routes/deck-options/DisplayOrder.svelte, choices.ts and
//! display-order-settings.ts of the fork): the new card gather and sort
//! orders, where new cards and learning cards go among reviews, and the
//! review sort order.
//!
//! Values are the protobuf enums' numbers. The intraday learning/review
//! order is the fork's own: kept in the preset's extra JSON under
//! "jschoreels.deckoptions", where its rslib reads it
//! (`DeckConfig::intraday_learning_mix`).

use anki::deckconfig::DeckConfigInner;
use anki::prelude::I18n;
use anki_proto::deck_config::deck_config::config::NewCardGatherPriority as Gather;
use anki_proto::deck_config::deck_config::config::NewCardSortOrder as Sort;
use anki_proto::deck_config::deck_config::config::ReviewCardOrder as ReviewOrder;
use anki_proto::deck_config::deck_config::config::ReviewMix;
use serde_json::json;
use serde_json::Map;
use serde_json::Value;

const METADATA_KEY: &str = "jschoreels.deckoptions";
const INTRADAY_MIX_KEY: &str = "intraday_learning_mix";
const LEGACY_PRIORITY_KEY: &str = "intraday_learning_before_reviews";

/// The keys of `deck_options`' values this section uses, in the desktop's order.
pub const KEYS: [&str; 6] = [
    "new_gather_order",
    "new_sort_order",
    "new_mix",
    "interday_learning_mix",
    "intraday_learning_mix",
    "review_order",
];

pub fn values(c: &DeckConfigInner) -> Value {
    json!({
        "new_gather_order": c.new_card_gather_priority,
        "new_sort_order": c.new_card_sort_order,
        "new_mix": c.new_mix,
        "interday_learning_mix": c.interday_learning_mix,
        "intraday_learning_mix": intraday_learning_mix(c) as i32,
        "review_order": c.review_order,
    })
}

fn metadata(c: &DeckConfigInner) -> Map<String, Value> {
    serde_json::from_slice::<Value>(&c.other)
        .ok()
        .and_then(|other| other.get(METADATA_KEY).and_then(Value::as_object).cloned())
        .unwrap_or_default()
}

/// As the fork reads it: its key, else the older switch, else mixed.
pub fn intraday_learning_mix(c: &DeckConfigInner) -> ReviewMix {
    let data = metadata(c);
    match data.get(INTRADAY_MIX_KEY) {
        Some(value) => value
            .as_i64()
            .and_then(|v| i32::try_from(v).ok())
            .and_then(|v| ReviewMix::try_from(v).ok())
            .unwrap_or(ReviewMix::MixWithReviews),
        None if data.get(LEGACY_PRIORITY_KEY).and_then(Value::as_bool) == Some(true) => ReviewMix::BeforeReviews,
        None => ReviewMix::MixWithReviews,
    }
}

/// As the fork's `withDisplayOrderSettings`: the mode under its key, the
/// older switch removed, the extra JSON's other keys kept.
fn set_intraday_learning_mix(c: &mut DeckConfigInner, mix: ReviewMix) {
    let mut other = match serde_json::from_slice::<Value>(&c.other) {
        Ok(Value::Object(map)) => map,
        _ => Map::new(),
    };
    let mut data = metadata(c);
    data.remove(LEGACY_PRIORITY_KEY);
    data.insert(INTRADAY_MIX_KEY.into(), Value::from(mix as i32));
    other.insert(METADATA_KEY.into(), Value::Object(data));
    c.other = serde_json::to_vec(&Value::Object(other)).unwrap();
}

/// Sort orders the desktop disables for a gather order (the same as another
/// choice, or not useful).
fn disabled_sort_orders(gather: i32) -> &'static [Sort] {
    if gather == Gather::RandomNotes as i32 {
        &[Sort::TemplateThenRandom, Sort::RandomNoteThenTemplate]
    } else if gather == Gather::RandomCards as i32 {
        &[Sort::TemplateThenRandom, Sort::RandomNoteThenTemplate, Sort::RandomCard]
    } else {
        &[]
    }
}

fn sort_disabled(gather: i32, sort: i32) -> bool {
    disabled_sort_orders(gather).iter().any(|s| *s as i32 == sort)
}

/// Whether `key` is one of this section's.
pub fn is_key(key: &str) -> bool {
    KEYS.contains(&key)
}

/// Set one of this section's values (a number of the enum's). None: not one
/// of its keys.
pub fn set(c: &mut DeckConfigInner, key: &str, value: &Value) -> Option<Result<(), String>> {
    if !is_key(key) {
        return None;
    }
    let number = value.as_i64().and_then(|v| i32::try_from(v).ok());
    let bad = || Err(format!("invalid value for {key}: {value}"));
    let Some(n) = number else { return Some(bad()) };
    let mix = ReviewMix::try_from(n).ok();
    Some(match key {
        "new_gather_order" if Gather::try_from(n).is_ok() => {
            c.new_card_gather_priority = n;
            // As the desktop: a sort order the new gather order disables goes
            // back to the default (never disabled).
            if sort_disabled(n, c.new_card_sort_order) {
                c.new_card_sort_order = Sort::Template as i32;
            }
            Ok(())
        }
        "new_sort_order" if Sort::try_from(n).is_ok() && !sort_disabled(c.new_card_gather_priority, n) => {
            c.new_card_sort_order = n;
            Ok(())
        }
        "new_mix" if mix.is_some() => {
            c.new_mix = n;
            Ok(())
        }
        "interday_learning_mix" if mix.is_some() => {
            c.interday_learning_mix = n;
            Ok(())
        }
        "intraday_learning_mix" if mix.is_some() => {
            set_intraday_learning_mix(c, mix.unwrap());
            Ok(())
        }
        "review_order" if ReviewOrder::try_from(n).is_ok() => {
            c.review_order = n;
            Ok(())
        }
        _ => bad(),
    })
}

fn choice(value: i32, label: impl AsRef<str>) -> Value {
    json!({ "value": value, "label": crate::api::without_bidi_isolates(label.as_ref()) })
}

/// Each key's choices (`value`, `label`), in the desktop's order and words:
/// the sort orders the gather order allows; the review orders FSRS has
/// (`fsrs`: the collection's switch). A saved value no choice has is added,
/// so the screen shows what is saved.
pub fn choices(c: &DeckConfigInner, fsrs: bool, tr: &I18n) -> Value {
    let gather = vec![
        choice(Gather::Deck as i32, tr.deck_config_new_gather_priority_deck()),
        choice(Gather::DeckThenRandomNotes as i32, tr.deck_config_new_gather_priority_deck_then_random_notes()),
        choice(Gather::LowestPosition as i32, tr.deck_config_new_gather_priority_position_lowest_first()),
        choice(Gather::HighestPosition as i32, tr.deck_config_new_gather_priority_position_highest_first()),
        choice(
            Gather::AscendingRetrievability as i32,
            tr.deck_config_new_gather_priority_ascending_retrievability(),
        ),
        choice(
            Gather::DescendingRetrievability as i32,
            tr.deck_config_new_gather_priority_descending_retrievability(),
        ),
        choice(Gather::RandomNotes as i32, tr.deck_config_new_gather_priority_random_notes()),
        choice(Gather::RandomCards as i32, tr.deck_config_new_gather_priority_random_cards()),
    ];
    let sort: Vec<Value> = [
        (Sort::Template, tr.deck_config_sort_order_template_then_gather()),
        (Sort::NoSort, tr.deck_config_sort_order_gather()),
        (Sort::TemplateThenRandom, tr.deck_config_sort_order_card_template_then_random()),
        (Sort::RandomNoteThenTemplate, tr.deck_config_sort_order_random_note_then_template()),
        (Sort::RandomCard, tr.deck_config_sort_order_random()),
    ]
    .into_iter()
    .filter(|(s, _)| *s as i32 == c.new_card_sort_order || !sort_disabled(c.new_card_gather_priority, *s as i32))
    .map(|(s, label)| choice(s as i32, label))
    .collect();
    let mix = vec![
        choice(ReviewMix::MixWithReviews as i32, tr.deck_config_review_mix_mix_with_reviews()),
        choice(ReviewMix::AfterReviews as i32, tr.deck_config_review_mix_show_after_reviews()),
        choice(ReviewMix::BeforeReviews as i32, tr.deck_config_review_mix_show_before_reviews()),
    ];
    let mut review = vec![
        choice(ReviewOrder::Day as i32, tr.deck_config_sort_order_due_date_then_random()),
        choice(ReviewOrder::DayThenDeck as i32, tr.deck_config_sort_order_due_date_then_deck()),
        choice(ReviewOrder::DeckThenDay as i32, tr.deck_config_sort_order_deck_then_due_date()),
        choice(ReviewOrder::IntervalsAscending as i32, tr.deck_config_sort_order_ascending_intervals()),
        choice(ReviewOrder::IntervalsDescending as i32, tr.deck_config_sort_order_descending_intervals()),
    ];
    // (with FSRS: difficulty, easy cards first, and retrievability)
    if fsrs {
        review.push(choice(ReviewOrder::EaseDescending as i32, tr.deck_config_sort_order_ascending_difficulty()));
        review.push(choice(ReviewOrder::EaseAscending as i32, tr.deck_config_sort_order_descending_difficulty()));
        review.push(choice(
            ReviewOrder::RetrievabilityAscending as i32,
            tr.deck_config_sort_order_retrievability_ascending(),
        ));
        review.push(choice(
            ReviewOrder::RetrievabilityDescending as i32,
            tr.deck_config_sort_order_retrievability_descending(),
        ));
    } else {
        review.push(choice(ReviewOrder::EaseAscending as i32, tr.deck_config_sort_order_ascending_ease()));
        review.push(choice(ReviewOrder::EaseDescending as i32, tr.deck_config_sort_order_descending_ease()));
    }
    review.push(choice(ReviewOrder::RelativeOverdueness as i32, tr.decks_relative_overdueness()));
    review.push(choice(ReviewOrder::Random as i32, tr.deck_config_sort_order_random()));
    review.push(choice(ReviewOrder::Added as i32, tr.decks_order_added()));
    review.push(choice(ReviewOrder::ReverseAdded as i32, tr.decks_latest_added_first()));
    // A retrievability order kept from FSRS, now off: shown as saved.
    if !review.iter().any(|r| r["value"] == c.review_order) {
        let label = if c.review_order == ReviewOrder::RetrievabilityDescending as i32 {
            tr.deck_config_sort_order_retrievability_descending()
        } else {
            tr.deck_config_sort_order_retrievability_ascending()
        };
        review.push(choice(c.review_order, label));
    }
    json!({
        "new_gather_order": gather,
        "new_sort_order": sort,
        "new_mix": mix.clone(),
        "interday_learning_mix": mix.clone(),
        "intraday_learning_mix": mix,
        "review_order": review,
    })
}

/// The section's wording.
pub fn labels(tr: &I18n) -> Value {
    let s = |text: std::borrow::Cow<str>| crate::api::without_bidi_isolates(&text);
    json!({
        "display_order": s(tr.deck_config_ordering_title()),
        "new_gather_order": s(tr.deck_config_new_gather_priority()),
        "new_sort_order": s(tr.deck_config_new_card_sort_order()),
        "new_mix": s(tr.deck_config_new_review_priority()),
        "interday_learning_mix": s(tr.deck_config_interday_step_priority()),
        "intraday_learning_mix": s(tr.deck_config_intraday_step_priority()),
        "review_order": s(tr.deck_config_review_sort_order()),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn config() -> DeckConfigInner {
        anki::deckconfig::DeckConfig::default().inner
    }

    #[test]
    fn intraday_mix_reads_the_forks_key_and_legacy_switch() {
        let mut c = config();
        assert_eq!(intraday_learning_mix(&c), ReviewMix::MixWithReviews);
        c.other = br#"{"jschoreels.deckoptions": {"intraday_learning_before_reviews": true}}"#.to_vec();
        assert_eq!(intraday_learning_mix(&c), ReviewMix::BeforeReviews);
        c.other = br#"{"jschoreels.deckoptions": {"intraday_learning_mix": 1, "intraday_learning_before_reviews": true}}"#
            .to_vec();
        assert_eq!(intraday_learning_mix(&c), ReviewMix::AfterReviews);
        c.other = br#"{"jschoreels.deckoptions": {"intraday_learning_mix": 99}}"#.to_vec();
        assert_eq!(intraday_learning_mix(&c), ReviewMix::MixWithReviews);
    }

    #[test]
    fn setting_intraday_mix_keeps_other_keys_and_drops_the_legacy_switch() {
        let mut c = config();
        c.other =
            br#"{"kept": 1, "jschoreels.deckoptions": {"intraday_learning_before_reviews": true, "also": 2}}"#.to_vec();
        set(&mut c, "intraday_learning_mix", &Value::from(1)).unwrap().unwrap();
        let other: Value = serde_json::from_slice(&c.other).unwrap();
        assert_eq!(
            other,
            json!({"kept": 1, "jschoreels.deckoptions": {"intraday_learning_mix": 1, "also": 2}})
        );
        assert_eq!(intraday_learning_mix(&c), ReviewMix::AfterReviews);
    }

    #[test]
    fn gather_order_resets_a_sort_order_it_disables() {
        let mut c = config();
        set(&mut c, "new_sort_order", &Value::from(Sort::RandomCard as i32)).unwrap().unwrap();
        set(&mut c, "new_gather_order", &Value::from(Gather::RandomNotes as i32)).unwrap().unwrap();
        assert_eq!(c.new_card_sort_order, Sort::RandomCard as i32, "allowed with random notes");
        set(&mut c, "new_gather_order", &Value::from(Gather::RandomCards as i32)).unwrap().unwrap();
        assert_eq!(c.new_card_sort_order, Sort::Template as i32);
        assert!(set(&mut c, "new_sort_order", &Value::from(Sort::RandomCard as i32)).unwrap().is_err());
    }

    #[test]
    fn invalid_values_are_refused() {
        let mut c = config();
        assert!(set(&mut c, "review_order", &Value::from(99)).unwrap().is_err());
        assert!(set(&mut c, "new_mix", &Value::from("before")).unwrap().is_err());
        assert!(set(&mut c, "not_ours", &Value::from(1)).is_none());
        set(&mut c, "review_order", &Value::from(ReviewOrder::RelativeOverdueness as i32)).unwrap().unwrap();
        assert_eq!(c.review_order, 12);
    }

    #[test]
    fn choices_follow_gather_order_and_fsrs() {
        let tr = I18n::template_only();
        let mut c = config();
        c.new_card_gather_priority = Gather::RandomCards as i32;
        let all = choices(&c, false, &tr);
        let sorts: Vec<i64> = all["new_sort_order"].as_array().unwrap().iter().map(|v| v["value"].as_i64().unwrap()).collect();
        assert_eq!(sorts, vec![0, 1]);
        let reviews = |all: &Value| -> Vec<i64> {
            all["review_order"].as_array().unwrap().iter().map(|v| v["value"].as_i64().unwrap()).collect()
        };
        assert_eq!(reviews(&all), vec![0, 1, 2, 3, 4, 5, 6, 12, 8, 9, 10]);
        assert_eq!(reviews(&choices(&c, true, &tr)), vec![0, 1, 2, 3, 4, 6, 5, 7, 11, 12, 8, 9, 10]);
        c.review_order = 11;
        assert_eq!(*reviews(&choices(&c, false, &tr)).last().unwrap(), 11, "the saved one stays");
    }
}
