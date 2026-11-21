//! The desktop deck options' Easy Days section (EasyDays.svelte and
//! EasyDaysInput.svelte): how many reviews each weekday gets, the preset's.

use anki::deckconfig::DeckConfigInner;
use anki::prelude::I18n;
use serde_json::json;
use serde_json::Value;

/// The preset's seven percentages, Monday first (1: normal, 0.5: reduced, 0:
/// minimum). A preset without seven gets the default, as on the desktop.
pub fn easy_days(c: &DeckConfigInner) -> Vec<f32> {
    if c.easy_days_percentages.len() == 7 {
        c.easy_days_percentages.clone()
    } else {
        vec![1.0; 7]
    }
}

/// `values.easy_days`: seven numbers from 0 to 1.
pub fn set_easy_days(c: &mut DeckConfigInner, value: &Value) -> Result<(), String> {
    let days: Option<Vec<f32>> = value.as_array().filter(|a| a.len() == 7).and_then(|a| {
        a.iter()
            .map(|v| v.as_f64().filter(|p| (0.0..=1.0).contains(p)).map(|p| p as f32))
            .collect()
    });
    c.easy_days_percentages = days.ok_or_else(|| format!("invalid easy days: {value}"))?;
    Ok(())
}

/// The section's wording.
pub fn labels(tr: &I18n) -> Value {
    let s = |text: std::borrow::Cow<str>| crate::api::without_bidi_isolates(&text);
    json!({
        "easy_days": s(tr.deck_config_easy_days_title()),
        "easy_days_weekdays": [
            s(tr.deck_config_easy_days_monday()),
            s(tr.deck_config_easy_days_tuesday()),
            s(tr.deck_config_easy_days_wednesday()),
            s(tr.deck_config_easy_days_thursday()),
            s(tr.deck_config_easy_days_friday()),
            s(tr.deck_config_easy_days_saturday()),
            s(tr.deck_config_easy_days_sunday()),
        ],
        // (the levels, lowest first, as the desktop's slider)
        "easy_days_levels": [
            s(tr.deck_config_easy_days_minimum()),
            s(tr.deck_config_easy_days_reduced()),
            s(tr.deck_config_easy_days_normal()),
        ],
        "easy_days_no_normal_days": s(tr.deck_config_easy_days_no_normal_days()),
        "easy_days_change": s(tr.deck_config_easy_days_change()),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn easy_days_default_and_validation() {
        let mut c = anki::deckconfig::DeckConfig::default().inner;
        c.easy_days_percentages.clear();
        assert_eq!(easy_days(&c), vec![1.0; 7]);
        set_easy_days(&mut c, &json!([1, 0.5, 0, 1, 1, 1, 1])).unwrap();
        assert_eq!(easy_days(&c), vec![1.0, 0.5, 0.0, 1.0, 1.0, 1.0, 1.0]);
        assert!(set_easy_days(&mut c, &json!([1, 1, 1])).is_err());
        assert!(set_easy_days(&mut c, &json!([1, 1, 1, 1, 1, 1, 1.5])).is_err());
    }
}
