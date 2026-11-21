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

/// What the desktop's preset selector and its Save menu do
/// (ts/routes/deck-options: ConfigSelector, SaveButton, lib.ts).
#[derive(Debug, Clone, PartialEq)]
pub enum PresetAction {
    /// Give the deck another preset.
    Select(i64),
    /// A new preset with the defaults, for the deck.
    Add(String),
    /// A copy of the deck's preset, for the deck.
    Clone(String),
    /// Rename the deck's preset.
    Rename(String),
    /// Delete the deck's preset (not the default one); the deck gets the
    /// preset before it in the list, as on the desktop. Modifies the schema:
    /// the next sync is a full one.
    Remove,
    /// The deck's preset for its subdecks too ("Save to all subdecks").
    ApplyToSubdecks,
}

/// The presets as the desktop lists them: by name, ignoring case.
pub fn sorted_presets(info: &DeckConfigsForUpdate) -> Vec<&anki_proto::deck_config::DeckConfig> {
    let mut configs: Vec<_> = info.all_config.iter().filter_map(|c| c.config.as_ref()).collect();
    configs.sort_by_key(|c| c.name.to_lowercase());
    configs
}

/// As the desktop's `ensureNewNameUnique`: a name in use gets the time
/// (seconds) appended.
fn unique_name(info: &DeckConfigsForUpdate, name: &str, now_secs: i64) -> String {
    if sorted_presets(info).iter().any(|c| c.name == name) {
        format!("{name}{now_secs}")
    } else {
        name.to_string()
    }
}

/// The update that does `action` for the deck (`info`: its
/// `get_deck_configs_for_update`). Err: a message for the user's mistake
/// (empty name, unknown preset, removing the default).
pub fn action_request(
    info: &DeckConfigsForUpdate,
    target_deck_id: DeckId,
    action: &PresetAction,
    now_secs: i64,
) -> Result<UpdateDeckConfigsRequest, String> {
    let current_id = info.current_deck.as_ref().map(|d| d.config_id).unwrap_or_default();
    let find = |id: i64| sorted_presets(info).into_iter().find(|c| c.id == id).cloned();
    let current = find(current_id).ok_or("the deck has no options preset")?;
    let named = |name: &str| {
        let name = name.trim();
        if name.is_empty() {
            Err("empty name".to_string())
        } else {
            Ok(name.to_string())
        }
    };
    let new_preset = |name: &str, config| -> Result<anki_proto::deck_config::DeckConfig, String> {
        Ok(anki_proto::deck_config::DeckConfig {
            id: 0,
            name: unique_name(info, &named(name)?, now_secs),
            mtime_secs: 0,
            usn: 0,
            config,
        })
    };
    let mut removed = vec![];
    let mut mode = UpdateDeckConfigsMode::Normal;
    let config = match action {
        PresetAction::Select(id) => find(*id).ok_or("no such preset")?,
        PresetAction::Add(name) => new_preset(name, info.defaults.as_ref().and_then(|d| d.config.clone()))?,
        PresetAction::Clone(name) => new_preset(name, current.config.clone())?,
        PresetAction::Rename(name) => {
            let name = named(name)?;
            let mut renamed = current.clone();
            if name != current.name {
                renamed.name = unique_name(info, &name, now_secs);
            }
            renamed
        }
        PresetAction::Remove => {
            if current.id == 1 {
                return Err("the default preset can't be removed".into());
            }
            let list = sorted_presets(info);
            let index = list.iter().position(|c| c.id == current.id).unwrap_or(0);
            let remaining: Vec<_> = list.into_iter().filter(|c| c.id != current.id).collect();
            removed.push(current.id);
            (*remaining.get(index.saturating_sub(1)).ok_or("no other preset")?).clone()
        }
        PresetAction::ApplyToSubdecks => {
            mode = UpdateDeckConfigsMode::ApplyToChildren;
            current.clone()
        }
    };
    let mut request = update_request(info, target_deck_id, vec![config.into()], mode);
    request.removed_config_ids = removed.into_iter().map(anki::deckconfig::DeckConfigId).collect();
    Ok(request)
}
