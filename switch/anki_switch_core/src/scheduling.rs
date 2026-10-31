//! Custom scheduling: the collection's JavaScript that changes what each
//! answer button does (Anki's deck options > Advanced > Custom scheduling,
//! the `cardStateCustomizer` config key, for every deck). As in Anki's
//! reviewer (ts/reviewer/answering.ts), the app's page gets the shown card's
//! scheduling states in protobuf's JSON form, runs the code on them, and the
//! states the code left come back in the same form. The page reads and
//! writes them as protobuf-es does (app/js/src/scheduling.js); this is the
//! backend's end: the states and the scheduling context as that JSON, and
//! the states read back from it.

use anki::scheduler::states::SchedulingStates;
use anki_proto::cards::FsrsMemoryState;
use anki_proto::scheduler::scheduling_state::filtered;
use anki_proto::scheduler::scheduling_state::normal;
use anki_proto::scheduler::scheduling_state::Filtered;
use anki_proto::scheduler::scheduling_state::Kind;
use anki_proto::scheduler::scheduling_state::Learning;
use anki_proto::scheduler::scheduling_state::New;
use anki_proto::scheduler::scheduling_state::Normal;
use anki_proto::scheduler::scheduling_state::Preview;
use anki_proto::scheduler::scheduling_state::Relearning;
use anki_proto::scheduler::scheduling_state::ReschedulingFilter;
use anki_proto::scheduler::scheduling_state::Review;
use anki_proto::scheduler::SchedulingContext;
use anki_proto::scheduler::SchedulingState;
use serde_json::json;
use serde_json::Map;
use serde_json::Value;

/// Each answer's custom data (Again, Hard, Good, Easy), as the code left
/// it; None keeps the card's.
pub type CustomData = [Option<String>; 4];

/// The states as protobuf-es writes them (`toJson({emitDefaultValues:
/// true})`): camelCase names, a oneof as its member's name, every number
/// (zero too), no unset message. `current` carries the card's custom data,
/// as Anki's reviewer fills it in (V3CardInfo.from_queue).
pub fn states_json(states: &SchedulingStates, custom_data: &str) -> Value {
    let mut current = SchedulingState::from(states.current);
    current.custom_data = Some(custom_data.to_string());
    json!({
        "current": state_json(&current),
        "again": state_json(&states.again.into()),
        "hard": state_json(&states.hard.into()),
        "good": state_json(&states.good.into()),
        "easy": state_json(&states.easy.into()),
    })
}

/// The scheduling context as protobuf-es writes it: the seed (a uint64) as
/// a string; decay and desired retention only if the card has them.
pub fn context_json(context: &SchedulingContext) -> Value {
    let mut out = json!({ "deckName": context.deck_name, "seed": context.seed.to_string() });
    if let Some(decay) = context.decay {
        out["decay"] = float(decay);
    }
    if let Some(retention) = context.desired_retention {
        out["desiredRetention"] = float(retention);
    }
    out
}

/// The states the page handed back, in the form `states_json` writes (a
/// number left out is zero, as in protobuf), and each answer's custom data.
pub fn read_states(json: &Value) -> Result<(SchedulingStates, CustomData), String> {
    let fields = message(json, "SchedulingStates", &["current", "again", "hard", "good", "easy"])?;
    let read = |key: &str| fields.get(key).map(read_state).transpose();
    let mut states = anki_proto::scheduler::SchedulingStates {
        current: read("current")?,
        again: read("again")?,
        hard: read("hard")?,
        good: read("good")?,
        easy: read("easy")?,
    };
    let custom_data = [&mut states.again, &mut states.hard, &mut states.good, &mut states.easy]
        .map(|state| state.as_mut().and_then(|state| state.custom_data.take()));
    Ok((states.into(), custom_data))
}

fn state_json(state: &SchedulingState) -> Value {
    let mut out = Map::new();
    match &state.kind {
        Some(Kind::Normal(normal)) => {
            out.insert("normal".into(), normal_json(normal));
        }
        Some(Kind::Filtered(filtered)) => {
            out.insert("filtered".into(), filtered_json(filtered));
        }
        None => {}
    }
    if let Some(data) = &state.custom_data {
        out.insert("customData".into(), data.as_str().into());
    }
    Value::Object(out)
}

fn normal_json(normal: &Normal) -> Value {
    match &normal.kind {
        Some(normal::Kind::New(new)) => json!({ "new": { "position": new.position } }),
        Some(normal::Kind::Learning(learning)) => json!({ "learning": learning_json(learning) }),
        Some(normal::Kind::Review(review)) => json!({ "review": review_json(review) }),
        Some(normal::Kind::Relearning(relearning)) => {
            let mut inner = Map::new();
            if let Some(review) = &relearning.review {
                inner.insert("review".into(), review_json(review));
            }
            if let Some(learning) = &relearning.learning {
                inner.insert("learning".into(), learning_json(learning));
            }
            json!({ "relearning": inner })
        }
        None => json!({}),
    }
}

fn learning_json(learning: &Learning) -> Value {
    let mut out = json!({
        "remainingSteps": learning.remaining_steps,
        "scheduledSecs": learning.scheduled_secs,
        "elapsedSecs": learning.elapsed_secs,
    });
    if let Some(memory) = &learning.memory_state {
        out["memoryState"] = memory_json(memory);
    }
    out
}

fn review_json(review: &Review) -> Value {
    let mut out = json!({
        "scheduledDays": review.scheduled_days,
        "elapsedDays": review.elapsed_days,
        "easeFactor": float(review.ease_factor),
        "lapses": review.lapses,
        "leeched": review.leeched,
    });
    if let Some(memory) = &review.memory_state {
        out["memoryState"] = memory_json(memory);
    }
    out
}

fn filtered_json(filtered: &Filtered) -> Value {
    match &filtered.kind {
        Some(filtered::Kind::Preview(preview)) => {
            json!({ "preview": { "scheduledSecs": preview.scheduled_secs, "finished": preview.finished } })
        }
        Some(filtered::Kind::Rescheduling(rescheduling)) => {
            let mut inner = Map::new();
            if let Some(original) = &rescheduling.original_state {
                inner.insert("originalState".into(), normal_json(original));
            }
            json!({ "rescheduling": inner })
        }
        None => json!({}),
    }
}

fn memory_json(memory: &FsrsMemoryState) -> Value {
    json!({ "stability": float(memory.stability), "difficulty": float(memory.difficulty) })
}

/// A float as protobuf's JSON has it: a number, or "NaN", "Infinity" or
/// "-Infinity" (which JSON numbers can't be).
fn float(value: f32) -> Value {
    if value.is_nan() {
        "NaN".into()
    } else if value.is_infinite() {
        (if value > 0.0 { "Infinity" } else { "-Infinity" }).into()
    } else {
        json!(value as f64)
    }
}

/// A message's fields: an object without keys the message doesn't have.
fn message<'a>(json: &'a Value, name: &str, keys: &[&str]) -> Result<&'a Map<String, Value>, String> {
    let fields = json
        .as_object()
        .ok_or_else(|| format!("{name}: {json} is not an object"))?;
    match fields.keys().find(|key| !keys.contains(&key.as_str())) {
        Some(key) => Err(format!("{name}: unknown field {key:?}")),
        None => Ok(fields),
    }
}

/// A oneof's member, if one is set (all its keys are the members').
fn member<'a>(fields: &'a Map<String, Value>, name: &str) -> Result<Option<(&'a str, &'a Value)>, String> {
    if fields.len() > 1 {
        return Err(format!("{name}: more than one of {:?}", fields.keys().collect::<Vec<_>>()));
    }
    Ok(fields.iter().next().map(|(key, value)| (key.as_str(), value)))
}

fn read_state(json: &Value) -> Result<SchedulingState, String> {
    let fields = message(json, "SchedulingState", &["normal", "filtered", "customData"])?;
    let kind = match (fields.get("normal"), fields.get("filtered")) {
        (Some(_), Some(_)) => return Err("SchedulingState: both normal and filtered".into()),
        (Some(normal), None) => Some(Kind::Normal(read_normal(normal)?)),
        (None, Some(filtered)) => Some(Kind::Filtered(read_filtered(filtered)?)),
        (None, None) => None,
    };
    let custom_data = match fields.get("customData") {
        None => None,
        Some(Value::String(data)) => Some(data.clone()),
        Some(other) => return Err(format!("customData: {other} is not a string")),
    };
    Ok(SchedulingState { kind, custom_data })
}

fn read_normal(json: &Value) -> Result<Normal, String> {
    let fields = message(json, "Normal", &["new", "learning", "review", "relearning"])?;
    let kind = match member(fields, "Normal")? {
        Some(("new", new)) => {
            let new = message(new, "New", &["position"])?;
            Some(normal::Kind::New(New {
                position: uint32(new, "position")?,
            }))
        }
        Some(("learning", learning)) => Some(normal::Kind::Learning(read_learning(learning)?)),
        Some(("review", review)) => Some(normal::Kind::Review(read_review(review)?)),
        Some(("relearning", relearning)) => {
            let relearning = message(relearning, "Relearning", &["review", "learning"])?;
            Some(normal::Kind::Relearning(Relearning {
                review: relearning.get("review").map(read_review).transpose()?,
                learning: relearning.get("learning").map(read_learning).transpose()?,
            }))
        }
        Some((other, _)) => return Err(format!("Normal: unknown field {other:?}")),
        None => None,
    };
    Ok(Normal { kind })
}

fn read_learning(json: &Value) -> Result<Learning, String> {
    let fields = message(json, "Learning", &["remainingSteps", "scheduledSecs", "elapsedSecs", "memoryState"])?;
    Ok(Learning {
        remaining_steps: uint32(fields, "remainingSteps")?,
        scheduled_secs: uint32(fields, "scheduledSecs")?,
        elapsed_secs: uint32(fields, "elapsedSecs")?,
        memory_state: fields.get("memoryState").map(read_memory).transpose()?,
    })
}

fn read_review(json: &Value) -> Result<Review, String> {
    let keys = ["scheduledDays", "elapsedDays", "easeFactor", "lapses", "leeched", "memoryState"];
    let fields = message(json, "Review", &keys)?;
    Ok(Review {
        scheduled_days: uint32(fields, "scheduledDays")?,
        elapsed_days: uint32(fields, "elapsedDays")?,
        ease_factor: float32(fields, "easeFactor")?,
        lapses: uint32(fields, "lapses")?,
        leeched: boolean(fields, "leeched")?,
        memory_state: fields.get("memoryState").map(read_memory).transpose()?,
    })
}

fn read_filtered(json: &Value) -> Result<Filtered, String> {
    let fields = message(json, "Filtered", &["preview", "rescheduling"])?;
    let kind = match member(fields, "Filtered")? {
        Some(("preview", preview)) => {
            let preview = message(preview, "Preview", &["scheduledSecs", "finished"])?;
            Some(filtered::Kind::Preview(Preview {
                scheduled_secs: uint32(preview, "scheduledSecs")?,
                finished: boolean(preview, "finished")?,
            }))
        }
        Some(("rescheduling", rescheduling)) => {
            let rescheduling = message(rescheduling, "ReschedulingFilter", &["originalState"])?;
            Some(filtered::Kind::Rescheduling(ReschedulingFilter {
                original_state: rescheduling.get("originalState").map(read_normal).transpose()?,
            }))
        }
        Some((other, _)) => return Err(format!("Filtered: unknown field {other:?}")),
        None => None,
    };
    Ok(Filtered { kind })
}

fn read_memory(json: &Value) -> Result<FsrsMemoryState, String> {
    let fields = message(json, "FsrsMemoryState", &["stability", "difficulty"])?;
    Ok(FsrsMemoryState {
        stability: float32(fields, "stability")?,
        difficulty: float32(fields, "difficulty")?,
    })
}

fn uint32(fields: &Map<String, Value>, key: &str) -> Result<u32, String> {
    match fields.get(key) {
        None => Ok(0),
        Some(value) => value
            .as_u64()
            .and_then(|n| u32::try_from(n).ok())
            .ok_or_else(|| format!("{key}: {value} is not a whole number from 0 to 4294967295")),
    }
}

fn float32(fields: &Map<String, Value>, key: &str) -> Result<f32, String> {
    match fields.get(key) {
        None => Ok(0.0),
        Some(Value::String(text)) => match text.as_str() {
            "NaN" => Ok(f32::NAN),
            "Infinity" => Ok(f32::INFINITY),
            "-Infinity" => Ok(f32::NEG_INFINITY),
            _ => Err(format!("{key}: {text:?} is not a number")),
        },
        Some(value) => value
            .as_f64()
            .filter(|n| n.abs() <= f32::MAX as f64)
            .map(|n| n as f32)
            .ok_or_else(|| format!("{key}: {value} is not a float")),
    }
}

fn boolean(fields: &Map<String, Value>, key: &str) -> Result<bool, String> {
    match fields.get(key) {
        None => Ok(false),
        Some(value) => value
            .as_bool()
            .ok_or_else(|| format!("{key}: {value} is not true or false")),
    }
}

#[cfg(test)]
mod tests {
    use anki::scheduler::states::CardState;

    use super::*;

    fn learning(remaining_steps: u32, scheduled_secs: u32) -> Learning {
        Learning {
            remaining_steps,
            scheduled_secs,
            elapsed_secs: 0,
            memory_state: None,
        }
    }

    fn review(scheduled_days: u32, ease_factor: f32) -> Review {
        Review {
            scheduled_days,
            elapsed_days: 3,
            ease_factor,
            lapses: 1,
            leeched: false,
            memory_state: Some(FsrsMemoryState {
                stability: 12.5,
                difficulty: 5.25,
            }),
        }
    }

    fn normal(kind: normal::Kind) -> SchedulingState {
        SchedulingState {
            kind: Some(Kind::Normal(Normal { kind: Some(kind) })),
            custom_data: None,
        }
    }

    fn filtered(kind: filtered::Kind) -> SchedulingState {
        SchedulingState {
            kind: Some(Kind::Filtered(Filtered { kind: Some(kind) })),
            custom_data: None,
        }
    }

    /// One of each kind of state: new, learning, review, relearning,
    /// preview, and rescheduling with its original state.
    fn example() -> SchedulingStates {
        anki_proto::scheduler::SchedulingStates {
            current: Some(normal(normal::Kind::New(New { position: 7 }))),
            again: Some(normal(normal::Kind::Learning(learning(2, 60)))),
            hard: Some(normal(normal::Kind::Relearning(Relearning {
                review: Some(review(1, 1.85)),
                learning: Some(learning(1, 600)),
            }))),
            good: Some(filtered(filtered::Kind::Rescheduling(ReschedulingFilter {
                original_state: Some(Normal {
                    kind: Some(normal::Kind::Review(review(4, 2.5))),
                }),
            }))),
            easy: Some(filtered(filtered::Kind::Preview(Preview {
                scheduled_secs: 0,
                finished: true,
            }))),
        }
        .into()
    }

    fn states_equal(a: &SchedulingStates, b: &SchedulingStates) -> bool {
        let all = |s: &SchedulingStates| -> [CardState; 5] { [s.current, s.again, s.hard, s.good, s.easy] };
        all(a) == all(b)
    }

    #[test]
    fn json_as_protobuf_es_writes_it() {
        let json = states_json(&example(), r#"{"seed":2104}"#);
        assert_eq!(json["current"], json!({ "normal": { "new": { "position": 7 } }, "customData": "{\"seed\":2104}" }));
        // Zero values are written; unset messages (memory states) are not.
        assert_eq!(
            json["again"],
            json!({ "normal": { "learning": { "remainingSteps": 2, "scheduledSecs": 60, "elapsedSecs": 0 } } })
        );
        let relearning = &json["hard"]["normal"]["relearning"];
        assert_eq!(relearning["learning"]["scheduledSecs"], 600);
        // A float as JavaScript reads the protobuf float: 1.85f32, not 1.85.
        assert_eq!(relearning["review"]["easeFactor"], json!(1.850000023841858));
        assert_eq!(relearning["review"]["memoryState"], json!({ "stability": 12.5, "difficulty": 5.25 }));
        let original = &json["good"]["filtered"]["rescheduling"]["originalState"];
        assert_eq!(original["review"]["scheduledDays"], 4);
        assert_eq!(json["easy"], json!({ "filtered": { "preview": { "scheduledSecs": 0, "finished": true } } }));
        // Only current has custom data.
        assert!(json["again"].get("customData").is_none());
    }

    #[test]
    fn read_back() {
        let states = example();
        let mut json = states_json(&states, "");
        let (read, custom_data) = read_states(&json).unwrap();
        assert!(states_equal(&read, &states), "{read:?}");
        assert_eq!(custom_data, [None, None, None, None]);

        // A changed interval and custom data, as the page hands them back.
        json["good"]["filtered"]["rescheduling"]["originalState"]["review"]["scheduledDays"] = json!(10);
        json["easy"]["customData"] = json!(r#"{"foo":"hello world"}"#);
        json["again"]["customData"] = json!("{}");
        let (read, custom_data) = read_states(&json).unwrap();
        let back = states_json(&read, "");
        assert_eq!(back["good"]["filtered"]["rescheduling"]["originalState"]["review"]["scheduledDays"], 10);
        assert_eq!(custom_data[0].as_deref(), Some("{}"));
        assert_eq!(custom_data[1], None);
        assert_eq!(custom_data[3].as_deref(), Some(r#"{"foo":"hello world"}"#));

        // A oneof changed to another member; numbers left out are zero.
        json["again"] = json!({ "normal": { "review": { "scheduledDays": 2 } } });
        let (read, _) = read_states(&json).unwrap();
        let again: SchedulingState = read.again.into();
        assert_eq!(
            again.kind,
            Some(Kind::Normal(Normal {
                kind: Some(normal::Kind::Review(Review {
                    scheduled_days: 2,
                    elapsed_days: 0,
                    ease_factor: 0.0,
                    lapses: 0,
                    leeched: false,
                    memory_state: None
                }))
            }))
        );
    }

    #[test]
    fn floats_beyond_json() {
        let mut json = states_json(&example(), "");
        json["hard"]["normal"]["relearning"]["review"]["easeFactor"] = json!("Infinity");
        json["hard"]["normal"]["relearning"]["review"]["memoryState"]["stability"] = json!("NaN");
        let (read, _) = read_states(&json).unwrap();
        let back = states_json(&read, "");
        let review = &back["hard"]["normal"]["relearning"]["review"];
        assert_eq!(review["easeFactor"], "Infinity");
        assert_eq!(review["memoryState"]["stability"], "NaN");
    }

    #[test]
    fn what_is_not_the_schema_is_refused() {
        let base = states_json(&example(), "");
        let refused = |change: &dyn Fn(&mut Value)| {
            let mut json = base.clone();
            change(&mut json);
            read_states(&json).expect_err(&json.to_string())
        };
        refused(&|j| j["again"]["normal"]["learning"]["scheduledSecs"] = json!(1.5));
        refused(&|j| j["again"]["normal"]["learning"]["scheduledSecs"] = json!(-1));
        refused(&|j| j["again"]["normal"]["learning"]["scheduledSecs"] = json!(4294967296u64));
        refused(&|j| j["again"]["normal"]["learning"]["scheduledSecs"] = json!("60"));
        refused(&|j| j["again"]["normal"]["learning"]["bogus"] = json!(1));
        refused(&|j| j["again"]["normal"]["review"] = json!({}));
        refused(&|j| j["again"]["filtered"] = json!({}));
        refused(&|j| j["again"]["customData"] = json!(5));
        refused(&|j| j["hard"]["normal"]["relearning"]["review"]["leeched"] = json!(0));
        refused(&|j| j["hard"]["normal"]["relearning"]["review"]["easeFactor"] = json!(1e39));
        refused(&|j| j["hard"]["normal"]["relearning"]["review"]["easeFactor"] = json!("fast"));
        refused(&|j| j["easy"] = json!(null));
        refused(&|j| j["extra"] = json!({}));
        assert!(read_states(&json!([])).is_err());
    }

    #[test]
    fn context() {
        let mut context = SchedulingContext {
            deck_name: "Spanish::Verbs".into(),
            seed: u64::MAX,
            decay: None,
            desired_retention: None,
        };
        assert_eq!(
            context_json(&context),
            json!({ "deckName": "Spanish::Verbs", "seed": "18446744073709551615" })
        );
        context.desired_retention = Some(0.9);
        assert_eq!(context_json(&context)["desiredRetention"], json!(0.9f32 as f64));
    }
}
