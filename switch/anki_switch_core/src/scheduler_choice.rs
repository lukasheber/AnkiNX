//! The FSRS-7 fork's scheduler choice, as its desktop deck options make it
//! (ts/routes/deck-options/scheduler-choice.ts, ported with its tests): one
//! "Review scheduler" (FSRS-6, FSRS-7, RWKV-Curve or RWKV-Instant) and, with
//! RWKV-Instant, the "Due dates" scheduler that writes intervals (FSRS-6,
//! FSRS-7 or RWKV-Curve), kept in the preset's existing fields: the FSRS
//! version and the two RWKV switches. Choosing a scheduler also turns FSRS
//! (collection-wide) on, as the desktop's selector does.

use anki::deckconfig::DeckConfigInner;
use anki_proto::deck_config::deck_config::config::FsrsVersion;

/// The schedulers, by the desktop's names ("fsrs-7", "rwkv-instant"...).
pub fn due_date_scheduler(c: &DeckConfigInner) -> &'static str {
    if c.rwkv_review_enabled {
        return "rwkv-curve";
    }
    match FsrsVersion::try_from(c.fsrs_version).unwrap_or(FsrsVersion::Seven) {
        FsrsVersion::Four => "fsrs-4",
        FsrsVersion::Five => "fsrs-5",
        FsrsVersion::Six => "fsrs-6",
        FsrsVersion::Seven => "fsrs-7",
    }
}

pub fn scheduler(c: &DeckConfigInner) -> &'static str {
    if c.rwkv_review_instant_order_enabled {
        "rwkv-instant"
    } else {
        due_date_scheduler(c)
    }
}

fn fsrs_version(choice: &str) -> Option<FsrsVersion> {
    Some(match choice {
        "fsrs-4" => FsrsVersion::Four,
        "fsrs-5" => FsrsVersion::Five,
        "fsrs-6" => FsrsVersion::Six,
        "fsrs-7" => FsrsVersion::Seven,
        _ => return None,
    })
}

/// "Due dates": an FSRS version or "rwkv-curve". False for anything else.
pub fn set_due_date_scheduler(c: &mut DeckConfigInner, choice: &str) -> bool {
    if choice == "rwkv-curve" {
        c.rwkv_review_enabled = true;
        return true;
    }
    let Some(version) = fsrs_version(choice) else {
        return false;
    };
    c.rwkv_review_enabled = false;
    c.fsrs_version = version as i32;
    true
}

/// "Review scheduler": a due-date scheduler or "rwkv-instant" (which keeps
/// the existing due-date scheduler as its companion). False for anything else.
pub fn set_scheduler(c: &mut DeckConfigInner, choice: &str) -> bool {
    if choice == "rwkv-instant" {
        c.rwkv_review_instant_order_enabled = true;
        return true;
    }
    if !set_due_date_scheduler(c, choice) {
        return false;
    }
    c.rwkv_review_instant_order_enabled = false;
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn config(version: FsrsVersion, curve: bool, instant: bool) -> DeckConfigInner {
        DeckConfigInner {
            fsrs_version: version as i32,
            rwkv_review_enabled: curve,
            rwkv_review_instant_order_enabled: instant,
            ..Default::default()
        }
    }

    #[test]
    fn existing_instant_presets_keep_their_selected_due_date_scheduler() {
        for curve in [false, true] {
            let c = config(FsrsVersion::Six, curve, true);
            assert_eq!(scheduler(&c), "rwkv-instant");
            assert_eq!(due_date_scheduler(&c), if curve { "rwkv-curve" } else { "fsrs-6" });
        }
    }

    #[test]
    fn choosing_an_fsrs_scheduler_disables_both_rwkv_overrides_and_preserves_other_parameters() {
        let mut original = config(FsrsVersion::Seven, true, true);
        original.fsrs_params_6 = vec![1.0; 21];
        original.fsrs_params_7 = vec![2.0; 34];
        original.rwkv_review_min_intervening_reviews = 3;
        for (choice, version) in [("fsrs-6", FsrsVersion::Six), ("fsrs-7", FsrsVersion::Seven)] {
            let mut c = original.clone();
            assert!(set_scheduler(&mut c, choice));
            assert_eq!(c.fsrs_version, version as i32);
            assert!(!c.rwkv_review_enabled);
            assert!(!c.rwkv_review_instant_order_enabled);
            assert_eq!(c.fsrs_params_6, original.fsrs_params_6);
            assert_eq!(c.fsrs_params_7, original.fsrs_params_7);
            assert_eq!(c.rwkv_review_min_intervening_reviews, 3);
        }
    }

    #[test]
    fn switching_between_curve_and_instant_keeps_curve_due_dates_until_another_companion_is_selected() {
        let mut c = config(FsrsVersion::Six, false, false);
        assert!(set_scheduler(&mut c, "rwkv-curve"));
        assert!(c.rwkv_review_enabled);
        assert!(!c.rwkv_review_instant_order_enabled);
        assert!(set_scheduler(&mut c, "rwkv-instant"));
        assert!(c.rwkv_review_enabled);
        assert!(c.rwkv_review_instant_order_enabled);
        assert!(set_due_date_scheduler(&mut c, "fsrs-7"));
        assert!(!c.rwkv_review_enabled);
        assert!(c.rwkv_review_instant_order_enabled);
        assert_eq!(c.fsrs_version, FsrsVersion::Seven as i32);
    }

    #[test]
    fn fsrs_versions_on_older_presets_are_read_without_rewriting_their_configuration() {
        for (version, name) in [(FsrsVersion::Four, "fsrs-4"), (FsrsVersion::Five, "fsrs-5")] {
            let c = config(version, false, false);
            assert_eq!(scheduler(&c), name);
            assert_eq!(c.fsrs_version, version as i32);
        }
    }

    #[test]
    fn unknown_choices_change_nothing() {
        let mut c = config(FsrsVersion::Seven, true, true);
        let before = c.clone();
        assert!(!set_scheduler(&mut c, "fsrs-8"));
        assert!(!set_due_date_scheduler(&mut c, "rwkv-instant"));
        assert_eq!(c, before);
    }
}
