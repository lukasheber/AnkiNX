//! Saving deck option presets through Anki's own update.

use anki::deckconfig::DeckConfig;
use anki::deckconfig::UpdateDeckConfigsRequest;
use anki::prelude::DeckId;
use anki_proto::deck_config::DeckConfigsForUpdate;
use anki_proto::deck_config::UpdateDeckConfigsMode;

/// The update that saves `configs` (the deck gets the last one) and leaves
/// everything else as `info` (Anki's `get_deck_configs_for_update`) has it.
///
/// The request also carries collection-wide settings (FSRS, the load
/// balancer, fuzz, the FSRS-7 same-day options...): passing them on unchanged
/// is what keeps a save here from resetting settings made on the desktop. It
/// goes through Anki's own conversion from the protobuf request, which fills
/// in the parts our crate can't name (the stored fuzz configuration).
pub fn update_request(
    info: &DeckConfigsForUpdate,
    target_deck_id: DeckId,
    configs: Vec<DeckConfig>,
    mode: UpdateDeckConfigsMode,
) -> UpdateDeckConfigsRequest {
    anki_proto::deck_config::UpdateDeckConfigsRequest {
        target_deck_id: target_deck_id.0,
        configs: configs.into_iter().map(Into::into).collect(),
        removed_config_ids: vec![],
        mode: mode as i32,
        card_state_customizer: info.card_state_customizer.clone(),
        limits: info.current_deck.clone().unwrap_or_default().limits,
        new_cards_ignore_review_limit: info.new_cards_ignore_review_limit,
        fsrs: info.fsrs,
        apply_all_parent_limits: info.apply_all_parent_limits,
        fsrs_reschedule: false,
        fsrs_health_check: info.fsrs_health_check,
        load_balancer_enabled: info.load_balancer_enabled,
        fsrs_short_term_with_steps_enabled: info.fsrs_short_term_with_steps_enabled,
        fsrs_learning_queues_disabled: info.fsrs_learning_queues_disabled,
        review_fuzz_enabled: Some(info.review_fuzz_enabled),
        review_fuzz_base: Some(info.review_fuzz_base),
        review_fuzz_factor_short: Some(info.review_fuzz_factor_short),
        review_fuzz_factor_mid: Some(info.review_fuzz_factor_mid),
        review_fuzz_factor_long: Some(info.review_fuzz_factor_long),
        rwkv_profile_settings: info.rwkv_profile_settings.clone(),
    }
    .into()
}
