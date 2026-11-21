//! The desktop deck options' Easy Days section (the fork's EasyDays.svelte
//! and EasyDaysInput.svelte): how many reviews each weekday gets (the
//! preset's), and two collection-wide settings, the load balancer and the
//! fork's review fuzz, with its preview of the fuzz ranges.

use anki::deckconfig::DeckConfigInner;
use anki::deckconfig::UpdateDeckConfigsRequest;
use anki::prelude::I18n;
use anki_proto::deck_config::DeckConfigsForUpdate;
use serde_json::json;
use serde_json::Map;
use serde_json::Value;

/// The fork's defaults (rslib/src/scheduler/states/fuzz.rs, EasyDays.svelte).
const DEFAULT_FUZZ: Fuzz = Fuzz { enabled: true, base: 1.0, short: 0.15, mid: 0.1, long: 0.05 };
/// The intervals the desktop previews.
const PREVIEW_INTERVALS: [u32; 6] = [3, 7, 20, 30, 90, 180];

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

#[derive(Clone, Copy, PartialEq, Debug)]
struct Fuzz {
    enabled: bool,
    base: f32,
    short: f32,
    mid: f32,
    long: f32,
}

impl Fuzz {
    fn of(info: &DeckConfigsForUpdate) -> Fuzz {
        Fuzz {
            enabled: info.review_fuzz_enabled,
            base: info.review_fuzz_base,
            short: info.review_fuzz_factor_short,
            mid: info.review_fuzz_factor_mid,
            long: info.review_fuzz_factor_long,
        }
    }

    /// The desktop's `fuzzDelta`.
    fn delta(&self, interval: f64) -> f64 {
        if !self.enabled || interval < 2.5 {
            return 0.0;
        }
        self.base as f64
            + self.short as f64 * (interval.min(7.0) - 2.5).max(0.0)
            + self.mid as f64 * (interval.min(20.0) - 7.0).max(0.0)
            + self.long as f64 * (interval - 20.0).max(0.0)
    }

    /// The desktop's `formatFuzzBounds` (`constrainedFuzzBounds`, 1 to
    /// 36,500 days): "2-4d", or "3d" without fuzz.
    fn bounds(&self, interval: u32) -> String {
        let (minimum, maximum) = (1.0, 36500.0);
        let interval = (interval as f64).clamp(minimum, maximum);
        let delta = self.delta(interval);
        // (JavaScript's Math.round: halves up)
        let round = |x: f64| (x + 0.5).floor();
        let lower = round(interval - delta).clamp(minimum, maximum);
        let mut upper = round(interval + delta).clamp(minimum, maximum);
        if upper == lower && upper > 2.0 && upper < maximum {
            upper = lower + 1.0;
        }
        if lower == upper {
            format!("{lower}d")
        } else {
            format!("{lower}-{upper}d")
        }
    }
}

/// The collection-wide settings, as `deck_options` returns them (and
/// `set_deck_options` takes them).
pub fn collection_values(info: &DeckConfigsForUpdate) -> Value {
    json!({
        "load_balancer": info.load_balancer_enabled,
        "review_fuzz_enabled": info.review_fuzz_enabled,
        "review_fuzz_base": info.review_fuzz_base,
        "review_fuzz_factor_short": info.review_fuzz_factor_short,
        "review_fuzz_factor_mid": info.review_fuzz_factor_mid,
        "review_fuzz_factor_long": info.review_fuzz_factor_long,
    })
}

/// The desktop's preview rows for the saved fuzz: interval, default range,
/// current range.
pub fn fuzz_preview(info: &DeckConfigsForUpdate) -> Value {
    let current = Fuzz::of(info);
    PREVIEW_INTERVALS
        .iter()
        .map(|&i| json!({ "interval": format!("{i}d"), "default": DEFAULT_FUZZ.bounds(i), "current": current.bounds(i) }))
        .collect()
}

/// `set_deck_options`' collection-wide keys of this section, into the
/// request (the desktop's limits: base 0-10 days, factors 0-1); other keys
/// are left alone.
pub fn set_collection(request: &mut UpdateDeckConfigsRequest, args: &Map<String, Value>) -> Result<(), String> {
    for (key, value) in args {
        let bad = || format!("invalid value for {key}: {value}");
        let number = |max: f64| value.as_f64().filter(|n| (0.0..=max).contains(n)).map(|n| n as f32).ok_or_else(bad);
        let fuzz = &mut request.review_fuzz_config;
        match key.as_str() {
            "load_balancer" => request.load_balancer_enabled = value.as_bool().ok_or_else(bad)?,
            "review_fuzz_enabled" => fuzz.enabled = value.as_bool().ok_or_else(bad)?,
            "review_fuzz_base" => fuzz.base = number(10.0)?,
            "review_fuzz_factor_short" => fuzz.factor_short = number(1.0)?,
            "review_fuzz_factor_mid" => fuzz.factor_mid = number(1.0)?,
            "review_fuzz_factor_long" => fuzz.factor_long = number(1.0)?,
            _ => {}
        }
    }
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
        "load_balancer": s(tr.deck_config_load_balancer_enabled()),
        "review_fuzz_enabled": s(tr.deck_config_review_fuzz_enabled()),
        "review_fuzz": s(tr.deck_config_review_fuzz_title()),
        "review_fuzz_base": s(tr.deck_config_review_fuzz_base()),
        "review_fuzz_factor_short": s(tr.deck_config_review_fuzz_factor_short()),
        "review_fuzz_factor_mid": s(tr.deck_config_review_fuzz_factor_mid()),
        "review_fuzz_factor_long": s(tr.deck_config_review_fuzz_factor_long()),
        "review_fuzz_preview": s(tr.deck_config_review_fuzz_preview()),
        "default_fuzz": s(tr.deck_config_default_fuzz()),
        "current_fuzz": s(tr.deck_config_current_fuzz()),
        "selected_fuzz": s(tr.deck_config_selected_fuzz()),
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

    #[test]
    fn fuzz_bounds_as_the_desktop_previews_them() {
        // The desktop's defaults: base 1, factors 15% / 10% / 5%.
        let bounds: Vec<String> = PREVIEW_INTERVALS.iter().map(|&i| DEFAULT_FUZZ.bounds(i)).collect();
        assert_eq!(bounds, ["2-4d", "5-9d", "17-23d", "27-33d", "84-96d", "169-191d"]);
        let off = Fuzz { enabled: false, ..DEFAULT_FUZZ };
        assert_eq!(off.bounds(3), "3-4d", "no fuzz: still a 1-day spread, as the desktop shows");
        assert_eq!(off.bounds(2), "2d");
        let none = Fuzz { base: 0.0, short: 0.0, mid: 0.0, long: 0.0, ..DEFAULT_FUZZ };
        assert_eq!(none.bounds(30), "30-31d");
    }
}
