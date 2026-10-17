//! JSON command API used by the Switch frontend (see `ankinx_call` in lib.rs).
//!
//! The frontend sends `(command, json args)` and gets back `{"ok": ...}` or
//! `{"error": "...", "kind": "..."}`. This layer owns the [Collection] and
//! mirrors what Anki's own backend does (backend/sync.rs, the reviewer), so the
//! C++ side never handles protobuf, scheduling states or sync details.

use std::io::Write;
use std::path::Path;
use std::path::PathBuf;
use std::sync::Mutex;
use std::thread::JoinHandle;
use std::time::Instant;

use anki::card_rendering::extract_av_tags;
use anki::collection::CollectionBuilder;
use anki::decks::tree::DeckCollapseScope;
use anki::error::SyncErrorKind;
use anki::prelude::*;
use anki::scheduler::answering::CardAnswer;
use anki::scheduler::answering::Rating;
use anki::scheduler::states::SchedulingStates;
use anki::storage::SchemaVersion;
use anki::sync::collection::normal::SyncActionRequired;
use anki::sync::collection::protocol::EmptyInput;
use anki::sync::collection::protocol::SyncProtocol;
use anki::sync::http_client::HttpSyncClient;
use anki::sync::request::IntoSyncRequest;
use anki::sync::login::sync_login;
use anki::sync::login::SyncAuth;
use anki_proto::card_rendering::av_tag;
use anki_proto::decks::DeckTreeNode;
use futures::future::AbortHandle;
use futures::future::Abortable;
use reqwest::Client;
use serde::Deserialize;
use serde::Serialize;
use serde_json::json;
use serde_json::Value;

const COLLECTION_FILE: &str = "collection.anki2";
const MEDIA_FOLDER: &str = "collection.media";
const MEDIA_DB: &str = "collection.mdb";
const BACKUP_FOLDER: &str = "backups";
const SYNC_FILE: &str = "sync.json";
const CRASH_LOG: &str = "crash.log";
const MAX_BACKUPS: usize = 10;
const BACKUP_INTERVAL_SECS: u64 = 30 * 60;

/// Saved AnkiWeb login (the password itself is never stored).
#[derive(Serialize, Deserialize, Default, Clone)]
struct SavedLogin {
    username: String,
    hkey: String,
    endpoint: Option<String>,
}

impl SavedLogin {
    fn auth(&self) -> Result<SyncAuth> {
        anki_proto::sync::SyncAuth {
            hkey: self.hkey.clone(),
            endpoint: self.endpoint.clone(),
            io_timeout_secs: None,
        }
        .try_into()
    }
}

/// The card currently on screen, and what answering it will do.
struct Shown {
    card_id: CardId,
    states: SchedulingStates,
    shown_at: Instant,
    /// Set when the card asks for a typed answer ({{type:Field}}).
    typing: Option<Typing>,
    /// The answer side as sent by `next_card`, before type-answer comparison.
    answer: String,
}

/// A card's {{type:...}} field, as Anki's reviewer prepares it
/// (qt/aqt/reviewer.py, typeAnsQuestionFilter).
struct Typing {
    expected: String,
    /// False for {{type:nc:...}}: compare ignoring combining characters.
    combining: bool,
    font: String,
    size: u32,
}

/// The "[[type:...]]" placeholder Anki's renderer leaves for the reviewer.
const TYPE_PATTERN_START: &str = "[[type:";

/// Find the first "[[type:...]]": (start, end, inner).
fn find_type_pattern(html: &str) -> Option<(usize, usize, &str)> {
    let start = html.find(TYPE_PATTERN_START)?;
    let inner_start = start + TYPE_PATTERN_START.len();
    let len = html[inner_start..].find("]]")?;
    let inner = &html[inner_start..inner_start + len];
    (!inner.is_empty()).then_some((start, inner_start + len + 2, inner))
}

/// Replace every "[[type:...]]" with `with`.
fn replace_type_patterns(html: &str, with: &str) -> String {
    let mut out = String::new();
    let mut rest = html;
    while let Some((start, end, _)) = find_type_pattern(rest) {
        out.push_str(&rest[..start]);
        out.push_str(with);
        rest = &rest[end..];
    }
    out.push_str(rest);
    out
}

/// Marker in the question's answer box that the app replaces with the text
/// typed so far.
const TYPED_MARKER: &str = "ANKINX_TYPED_ANSWER";

/// Question side of Anki's type-answer filter: work out the expected answer
/// and put an answer box where "[[type:...]]" is. The box is a link the app
/// handles ("anki-typeans:"), standing in for Anki's <input>.
fn prepare_type_answer(
    col: &mut Collection,
    card: &anki::card::Card,
    question: &str,
    tr: &I18n,
) -> Result<(String, Option<Typing>)> {
    use anki::services::CardRenderingService;
    let Some((_, _, inner)) = find_type_pattern(question) else {
        return Ok((question.to_string(), None));
    };
    let mut field = inner;
    let mut cloze = false;
    let mut combining = true;
    if let Some(rest) = field.strip_prefix("cloze:") {
        cloze = true;
        field = rest;
    }
    if let Some(rest) = field.strip_prefix("nc:") {
        combining = false;
        field = rest;
    }
    let note = col.storage.get_note(card.note_id())?.or_not_found(card.note_id())?;
    let notetype = col.get_notetype(note.notetype_id)?.or_not_found(note.notetype_id)?;
    let mut found = None;
    for (index, f) in notetype.fields.iter().enumerate() {
        if f.name == field {
            let mut value = note.fields().get(index).cloned().unwrap_or_default();
            if cloze {
                value = col
                    .extract_cloze_for_typing(anki_proto::card_rendering::ExtractClozeForTypingRequest {
                        text: value,
                        ordinal: card.template_idx() as u32 + 1,
                    })?
                    .val;
            }
            found = Some((value, f.config.font_name.clone(), f.config.font_size));
            break;
        }
    }
    let field_exists = found.is_some();
    let Some((expected, font, size)) = found.filter(|(value, ..)| !value.is_empty()) else {
        // As Anki: a cloze with nothing to type or an unknown field gets a
        // warning; an empty field just loses the box.
        let warning = if cloze {
            tr.studying_please_run_toolsempty_cards().to_string()
        } else if !field_exists {
            tr.studying_type_answer_unknown_field(field).to_string()
        } else {
            String::new()
        };
        return Ok((replace_type_patterns(question, &without_bidi_isolates(&warning)), None));
    };
    let box_html = format!(
        "<center><a href=\"anki-typeans:\" id=\"typeans\" class=\"typeans-box\" \
         style=\"font-family: '{font}'; font-size: {size}px;\">{TYPED_MARKER}</a></center>"
    );
    Ok((
        replace_type_patterns(question, &box_html),
        Some(Typing { expected, combining, font, size }),
    ))
}

/// Answer side of Anki's type-answer filter (typeAnsAnswerFilter): the
/// comparison of `typed` with the expected answer replaces "[[type:...]]".
fn compare_type_answer(col: &mut Collection, answer: &str, typing: Option<&Typing>, typed: &str) -> Result<String> {
    use anki::services::CardRenderingService;
    let Some(typing) = typing else {
        return Ok(replace_type_patterns(answer, ""));
    };
    let without_hr = answer.replace("<hr id=answer>", "");
    let had_hr = without_hr.len() != answer.len();
    if had_hr && find_type_pattern(&without_hr).is_none() {
        return Ok(answer.to_string());
    }
    let compared = col
        .compare_answer(anki_proto::card_rendering::CompareAnswerRequest {
            expected: typing.expected.clone(),
            provided: typed.to_string(),
            combining: typing.combining,
        })?
        .val;
    let mut block = format!(
        "<div style=\"font-family: '{}'; font-size: {}px\">{compared}</div>",
        typing.font, typing.size
    );
    if had_hr {
        // As Anki: keep the separator above the comparison with {{FrontSide}}.
        block = format!("<hr id=answer>{block}");
    }
    Ok(replace_type_patterns(&without_hr, &block))
}

struct MediaSync {
    abort: AbortHandle,
    task: JoinHandle<Result<()>>,
}

struct State {
    base: PathBuf,
    tr: I18n,
    col: Option<Collection>,
    shown: Option<Shown>,
    login: Option<SavedLogin>,
    media_sync: Option<MediaSync>,
    /// Error from the last finished media sync, reported once.
    media_error: Option<String>,
    runtime: tokio::runtime::Runtime,
    client: Client,
}

static STATE: Mutex<Option<State>> = Mutex::new(None);

/// Error returned to the frontend: a user-facing message plus a machine kind.
pub struct ApiError {
    message: String,
    kind: &'static str,
}

impl ApiError {
    fn new(kind: &'static str, message: impl Into<String>) -> Self {
        ApiError {
            message: without_bidi_isolates(&message.into()),
            kind,
        }
    }

    fn anki(err: AnkiError, tr: &I18n) -> Self {
        let kind = match &err {
            AnkiError::SyncError { source } if source.kind == SyncErrorKind::AuthFailed => "auth",
            AnkiError::SyncError { .. } => "sync",
            AnkiError::NetworkError { .. } => "network",
            AnkiError::Interrupted => "interrupted",
            AnkiError::CollectionNotOpen => "not_open",
            _ => "backend",
        };
        ApiError::new(kind, err.message(tr))
    }

    pub fn to_json(&self) -> Value {
        json!({ "error": self.message, "kind": self.kind })
    }
}

type ApiResult = std::result::Result<Value, ApiError>;

/// Entry point: run `cmd` with JSON `args`.
pub fn call(cmd: &str, args: &str) -> ApiResult {
    let args: Value = if args.trim().is_empty() {
        Value::Null
    } else {
        serde_json::from_str(args).map_err(|e| ApiError::new("args", e.to_string()))?
    };

    let mut guard = STATE.lock().unwrap_or_else(|e| e.into_inner());
    if cmd == "init" {
        return init(&mut guard, &args);
    }
    if cmd == "shutdown" {
        return shutdown(&mut guard);
    }
    let state = guard
        .as_mut()
        .ok_or_else(|| ApiError::new("not_initialized", "init has not been called"))?;
    match cmd {
        "status" => state.status(),
        "open" => state.open(),
        "close" => state.close(),
        "deck_tree" => state.deck_tree(),
        "set_collapsed" => state.set_collapsed(&args),
        "select_deck" => state.select_deck(&args),
        "next_card" => state.next_card(&args),
        "answer" => state.answer(&args),
        "type_answer" => state.type_answer(&args),
        "undo" => state.undo(),
        "login" => state.login(&args),
        "logout" => state.logout(),
        "sync" => state.sync(),
        "full_sync" => state.full_sync(&args),
        "media_sync_status" => state.media_sync_status(),
        _ => Err(ApiError::new("unknown_command", format!("unknown command: {cmd}"))),
    }
}

fn str_arg<'a>(args: &'a Value, key: &str) -> std::result::Result<&'a str, ApiError> {
    args.get(key)
        .and_then(Value::as_str)
        .ok_or_else(|| ApiError::new("args", format!("missing string argument '{key}'")))
}

fn i64_arg(args: &Value, key: &str) -> std::result::Result<i64, ApiError> {
    args.get(key)
        .and_then(Value::as_i64)
        .ok_or_else(|| ApiError::new("args", format!("missing integer argument '{key}'")))
}

/// `init {base_dir, langs: [..]}`: set the data folder and UI languages.
fn init(slot: &mut Option<State>, args: &Value) -> ApiResult {
    if let Some(state) = slot.as_mut() {
        return state.status();
    }
    let base = PathBuf::from(str_arg(args, "base_dir")?);
    let langs: Vec<String> = args
        .get("langs")
        .and_then(Value::as_array)
        .map(|a| a.iter().filter_map(|v| v.as_str().map(String::from)).collect())
        .unwrap_or_default();
    for dir in [base.clone(), base.join(MEDIA_FOLDER), base.join(BACKUP_FOLDER)] {
        std::fs::create_dir_all(&dir)
            .map_err(|e| ApiError::new("io", format!("{}: {e}", dir.display())))?;
    }
    log_panics_to(base.join(CRASH_LOG));
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(1)
        .enable_all()
        .build()
        .map_err(|e| ApiError::new("runtime", e.to_string()))?;
    // http1 only, as in Anki's backend (AnkiWeb's proxies don't do http2).
    let client = Client::builder()
        .http1_only()
        .build()
        .map_err(|e| ApiError::new("network", e.to_string()))?;
    let login = std::fs::read(base.join(SYNC_FILE))
        .ok()
        .and_then(|data| serde_json::from_slice(&data).ok());
    *slot = Some(State {
        base,
        tr: I18n::new(&langs),
        col: None,
        shown: None,
        login,
        media_sync: None,
        media_error: None,
        runtime,
        client,
    });
    slot.as_mut().unwrap().status()
}

/// Anki's `Collection::full_download` (rslib/src/sync/collection/download.rs),
/// with its file handling changed for Horizon, where a file that is open for
/// writing can't be opened again ("target locked"). Anki writes the download
/// through a second handle to a `NamedTempFile` it still holds open, then has
/// SQLite open it a third time; on the Switch the first of those fails. Here
/// each step opens and closes the file in turn. The collection is replaced
/// by rename, as in Anki (shim/horizon_fixes.h deletes the old file first).
async fn full_download(col: Collection, col_path: &Path, auth: SyncAuth, client: Client) -> Result<()> {
    col.close(None)?;
    let request = EmptyInput::default().try_into_sync_request()?;
    let data = HttpSyncClient::new(auth, client).download(request).await?.data;

    let temp = col_path.with_extension("download");
    let io = |op: &str, e: std::io::Error| {
        AnkiError::from(std::io::Error::new(e.kind(), format!("{op} {}: {e}", temp.display())))
    };
    let _ = std::fs::remove_file(&temp); // left over from an interrupted download
    {
        let mut file = std::fs::File::create(&temp).map_err(|e| io("create", e))?;
        file.write_all(&data).map_err(|e| io("write", e))?;
        file.sync_all().map_err(|e| io("sync", e))?;
    }
    drop(data);
    // Check that it's a valid collection, as Anki does.
    let checked = CollectionBuilder::new(&temp).set_check_integrity(true).build();
    let checked = checked.and_then(|col| col.close(None));
    // Mark it as in sync: Anki runs "update col set ls=mod" here through its
    // (crate-private) storage handle.
    let marked = checked.and_then(|()| sqlite_exec(&temp, "update col set ls=mod"));
    if let Err(e) = marked {
        let _ = std::fs::remove_file(&temp);
        return Err(e);
    }
    std::fs::rename(&temp, col_path).map_err(|e| io("rename", e))
}

/// Run `sql` on the database at `path` with a short-lived SQLite connection.
fn sqlite_exec(path: &Path, sql: &str) -> Result<()> {
    use libsqlite3_sys as ffi;
    use std::ffi::CString;
    let fail = |what: &str| AnkiError::from(std::io::Error::other(format!("{what}: {}", path.display())));
    let c_path = CString::new(path.to_string_lossy().as_bytes()).map_err(|_| fail("bad path"))?;
    let c_sql = CString::new(sql).map_err(|_| fail("bad sql"))?;
    unsafe {
        let mut db = std::ptr::null_mut();
        let rc = ffi::sqlite3_open_v2(c_path.as_ptr(), &mut db, ffi::SQLITE_OPEN_READWRITE, std::ptr::null());
        let result = if rc != ffi::SQLITE_OK {
            Err(fail(&format!("sqlite open failed ({rc})")))
        } else {
            let rc = ffi::sqlite3_exec(db, c_sql.as_ptr(), None, std::ptr::null_mut(), std::ptr::null_mut());
            if rc == ffi::SQLITE_OK {
                Ok(())
            } else {
                Err(fail(&format!("sqlite exec failed ({rc})")))
            }
        };
        ffi::sqlite3_close(db); // also when open failed, as SQLite requires
        result
    }
}

/// Append panics to `path` before the default report. On the Switch, panics
/// abort the process (panic=abort) and the system only says "The software was
/// closed because an error occurred", so this file is the only trace.
fn log_panics_to(path: PathBuf) {
    static INSTALLED: std::sync::Once = std::sync::Once::new();
    INSTALLED.call_once(|| {
        let default_hook = std::panic::take_hook();
        std::panic::set_hook(Box::new(move |info| {
            use std::io::Write;
            let thread = std::thread::current();
            let entry = format!(
                "{} AnkiNX {} (Anki {}), thread '{}': {info}\n",
                chrono::Local::now().format("%Y-%m-%d %H:%M:%S"),
                env!("CARGO_PKG_VERSION"),
                anki::version::version(),
                thread.name().unwrap_or("?"),
            );
            if let Ok(mut file) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
                let _ = file.write_all(entry.as_bytes());
            }
            default_hook(info);
        }));
    });
}

/// `shutdown`: close the collection (with backup), then stop the async runtime
/// and wait for its threads. Call before the app exits: on the Switch, libnx
/// shuts the socket service down after main() returns, and a runtime thread
/// still waiting on a socket then fails and aborts the process ("The software
/// was closed because an error occurred"). Afterwards, `init` starts over.
fn shutdown(slot: &mut Option<State>) -> ApiResult {
    let Some(mut state) = slot.take() else {
        return Ok(json!({}));
    };
    let closed = state.close();
    let State {
        runtime, client, ..
    } = state;
    drop(client); // its connection pool lives on the runtime
    runtime.shutdown_timeout(std::time::Duration::from_secs(3));
    closed
}

impl State {
    fn err(&self, err: AnkiError) -> ApiError {
        ApiError::anki(err, &self.tr)
    }

    fn col(&mut self) -> std::result::Result<&mut Collection, ApiError> {
        self.col
            .as_mut()
            .ok_or_else(|| ApiError::new("not_open", "the collection is not open"))
    }

    fn builder(&self) -> CollectionBuilder {
        let mut builder = CollectionBuilder::new(self.base.join(COLLECTION_FILE));
        builder
            .set_media_paths(self.base.join(MEDIA_FOLDER), self.base.join(MEDIA_DB))
            .set_tr(self.tr.clone());
        builder
    }

    fn status(&mut self) -> ApiResult {
        // Local changes not yet on AnkiWeb (Anki's offline check, no network):
        // "none", "normal" or "full"; null when the collection is closed.
        let sync_needed = self.col.as_mut().and_then(|col| col.sync_status_offline().ok()).map(|r| {
            use anki_proto::sync::sync_status_response::Required;
            match r {
                Required::NoChanges => "none",
                Required::NormalSync => "normal",
                Required::FullSync => "full",
            }
        });
        Ok(json!({
            "base_dir": self.base.to_string_lossy(),
            "media_dir": self.base.join(MEDIA_FOLDER).to_string_lossy(),
            "collection_exists": self.base.join(COLLECTION_FILE).exists(),
            "open": self.col.is_some(),
            "username": self.login.as_ref().map(|l| l.username.clone()),
            "sync_needed": sync_needed,
        }))
    }

    /// Open (or create) the collection.
    fn open(&mut self) -> ApiResult {
        if self.col.is_none() {
            self.col = Some(self.builder().build().map_err(|e| self.err(e))?);
        }
        self.status()
    }

    /// Close cleanly, making a backup if one is due. Safe to call when closed.
    fn close(&mut self) -> ApiResult {
        self.stop_media_sync();
        self.shown = None;
        let Some(mut col) = self.col.take() else {
            return Ok(json!({}));
        };
        let backups = self.base.join(BACKUP_FOLDER);
        let modified = col.sync_meta().map(|m| m.modified.0).unwrap_or(i64::MAX);
        if !backup_due(&backups, modified) {
            col.close(None).map_err(|e| self.err(e))?;
            return Ok(json!({}));
        }
        // Not Collection::maybe_backup(): it reads the database file while the
        // collection is still open, which Horizon refuses. Not export_colpkg()
        // either: it writes through a temp file it keeps open (see
        // full_download). The collection is closed first, then packaged.
        col.close(Some(SchemaVersion::V18)).map_err(|e| self.err(e))?;
        let name = chrono::Local::now()
            .format("backup-%Y-%m-%d-%H.%M.%S.colpkg")
            .to_string();
        match write_backup(&self.base.join(COLLECTION_FILE), &backups.join(name)) {
            Ok(()) => prune_backups(&backups, MAX_BACKUPS),
            Err(e) => eprintln!("backup failed: {e}"),
        }
        Ok(json!({}))
    }

    fn deck_tree(&mut self) -> ApiResult {
        let tree = self
            .col()?
            .deck_tree(Some(TimestampSecs::now()))
            .map_err(|e| ApiError::anki(e, &self.tr))?;
        let mut decks = Vec::new();
        flatten_tree(&tree, &mut decks);
        Ok(json!({ "decks": decks }))
    }

    fn set_collapsed(&mut self, args: &Value) -> ApiResult {
        let did = DeckId(i64_arg(args, "deck_id")?);
        let collapsed = args.get("collapsed").and_then(Value::as_bool).unwrap_or(true);
        let tr = self.tr.clone();
        self.col()?
            .set_deck_collapsed(did, collapsed, DeckCollapseScope::Reviewer)
            .map_err(|e| ApiError::anki(e, &tr))?;
        Ok(json!({}))
    }

    fn select_deck(&mut self, args: &Value) -> ApiResult {
        let did = DeckId(i64_arg(args, "deck_id")?);
        let tr = self.tr.clone();
        self.shown = None;
        self.col()?
            .set_current_deck(did)
            .map_err(|e| ApiError::anki(e, &tr))?;
        Ok(json!({}))
    }

    /// The next card of the current deck, fully rendered, or `{"done": true}`.
    /// `args.night_mode` only affects the returned body classes.
    fn next_card(&mut self, args: &Value) -> ApiResult {
        let tr = self.tr.clone();
        let err = |e| ApiError::anki(e, &tr);
        let col = self.col()?;
        let queued = col.get_queued_cards(1, false).map_err(err)?;
        let counts = json!({
            "new": queued.new_count,
            "learn": queued.learning_count,
            "review": queued.review_count,
        });
        let Some(card) = queued.cards.into_iter().next() else {
            self.shown = None;
            return Ok(json!({ "done": true, "counts": counts }));
        };

        let card_id = card.card.id();
        let ord = card.card.template_idx();
        let render = col.render_existing_card(card_id, false, false).map_err(err)?;
        let (question, q_av) = extract_av_tags(render.question().into_owned(), true, &tr);
        let (answer, a_av) = extract_av_tags(render.answer().into_owned(), false, &tr);
        let (question, typing) = prepare_type_answer(col, &card.card, &question, &tr).map_err(err)?;
        let question = native_hints(&play_links(&question, "q"));
        let mut answer = native_hints(&play_links(&answer, "a"));
        // Image Occlusion: shapes for the app to draw over the image.
        let mut question = question;
        let mut occlusions = Value::Null;
        if let Some(q) = crate::occlusion::extract(&question) {
            let a = crate::occlusion::extract(&answer);
            question = q.html;
            let (a_shapes, can_toggle) = match a {
                Some(a) => {
                    answer = a.html;
                    (a.shapes, a.can_toggle)
                }
                None => (vec![], false),
            };
            occlusions = json!({ "question": q.shapes, "answer": a_shapes, "can_toggle": can_toggle });
        }
        // Until something is typed, the answer compares against nothing, as in
        // Anki when Show Answer is pressed with an empty box.
        let shown_answer = compare_type_answer(col, &answer, typing.as_ref(), "").map_err(err)?;
        let buttons: Vec<String> = col
            .describe_next_states(&card.states)
            .map_err(err)?
            .iter()
            .map(|label| without_bidi_isolates(label))
            .collect();

        let night = args.get("night_mode").and_then(Value::as_bool).unwrap_or(false);
        let mut body_class = format!("card card{}", ord + 1);
        if night {
            body_class.push_str(" nightMode night_mode");
        }

        let type_answer = typing.is_some();
        self.shown = Some(Shown {
            card_id,
            states: card.states,
            shown_at: Instant::now(),
            typing,
            answer,
        });
        Ok(json!({
            "done": false,
            "card_id": card_id.0,
            // "New", "Learning" or "Review": which count the card belongs to.
            "queue": format!("{:?}", card.kind),
            "counts": counts,
            "question": question,
            // Image Occlusion shapes (see occlusion.rs), or null.
            "occlusions": occlusions,
            "answer": shown_answer,
            // The question has an answer box (see TYPED_MARKER); get the
            // answer side for what was typed with `type_answer`.
            "type_answer": type_answer,
            "typed_marker": TYPED_MARKER,
            "css": render.css,
            "body_class": body_class,
            "question_av": av_json(&q_av),
            "answer_av": av_json(&a_av),
            // Interval labels for Again, Hard, Good, Easy.
            "buttons": buttons,
        }))
    }

    /// `type_answer {typed}`: the answer side of the shown card, comparing what
    /// was typed with the expected answer (Anki's coloured comparison).
    fn type_answer(&mut self, args: &Value) -> ApiResult {
        let typed = args.get("typed").and_then(Value::as_str).unwrap_or("");
        let tr = self.tr.clone();
        let shown = self
            .shown
            .take()
            .ok_or_else(|| ApiError::new("no_card", "no card is being shown"))?;
        let html = self
            .col()
            .and_then(|col| {
                compare_type_answer(col, &shown.answer, shown.typing.as_ref(), typed)
                    .map_err(|e| ApiError::anki(e, &tr))
            });
        self.shown = Some(shown);
        Ok(json!({ "answer": html? }))
    }

    /// `answer {ease: 1..4}` for the card returned by the last `next_card`.
    fn answer(&mut self, args: &Value) -> ApiResult {
        let rating = match i64_arg(args, "ease")? {
            1 => Rating::Again,
            2 => Rating::Hard,
            3 => Rating::Good,
            4 => Rating::Easy,
            other => return Err(ApiError::new("args", format!("invalid ease {other}"))),
        };
        let shown = self
            .shown
            .take()
            .ok_or_else(|| ApiError::new("no_card", "no card is being shown"))?;
        let states = &shown.states;
        let new_state = match rating {
            Rating::Again => states.again,
            Rating::Hard => states.hard,
            Rating::Good => states.good,
            Rating::Easy => states.easy,
        };
        let mut answer = CardAnswer {
            card_id: shown.card_id,
            current_state: states.current,
            new_state,
            rating,
            answered_at: TimestampMillis::now(),
            milliseconds_taken: shown.shown_at.elapsed().as_millis().min(u32::MAX as u128) as u32,
            custom_data: None,
            from_queue: true,
        };
        let tr = self.tr.clone();
        self.col()?
            .answer_card(&mut answer)
            .map_err(|e| ApiError::anki(e, &tr))?;
        Ok(json!({}))
    }

    fn undo(&mut self) -> ApiResult {
        let tr = self.tr.clone();
        self.shown = None;
        let col = self.col()?;
        if col.can_undo().is_none() {
            return Ok(json!({ "undone": false }));
        }
        col.undo().map_err(|e| ApiError::anki(e, &tr))?;
        Ok(json!({ "undone": true }))
    }

    // ----- AnkiWeb sync (mirrors rslib/src/backend/sync.rs) -----

    /// `login {username, password, endpoint?}`: store the AnkiWeb host key.
    fn login(&mut self, args: &Value) -> ApiResult {
        let username = str_arg(args, "username")?.trim().to_string();
        let password = str_arg(args, "password")?.to_string();
        let endpoint = args
            .get("endpoint")
            .and_then(Value::as_str)
            .filter(|s| !s.is_empty())
            .map(String::from);
        let auth = self
            .runtime
            .block_on(sync_login(
                username.clone(),
                password,
                endpoint.clone(),
                self.client.clone(),
            ))
            .map_err(|e| self.err(e))?;
        let login = SavedLogin {
            username,
            hkey: auth.hkey,
            endpoint,
        };
        self.save_login(Some(login))?;
        self.status()
    }

    fn logout(&mut self) -> ApiResult {
        self.save_login(None)?;
        self.status()
    }

    fn save_login(&mut self, login: Option<SavedLogin>) -> std::result::Result<(), ApiError> {
        let path = self.base.join(SYNC_FILE);
        let io = |e: std::io::Error| ApiError::new("io", format!("{}: {e}", path.display()));
        match &login {
            Some(l) => std::fs::write(&path, serde_json::to_vec(l).unwrap()).map_err(io)?,
            None if path.exists() => std::fs::remove_file(&path).map_err(io)?,
            None => {}
        }
        self.login = login;
        Ok(())
    }

    fn require_login(&self) -> std::result::Result<SavedLogin, ApiError> {
        self.login
            .clone()
            .ok_or_else(|| ApiError::new("auth", "log in to AnkiWeb first"))
    }

    /// Normal sync. Returns `required`: "none", or when the collections have
    /// diverged "full_sync" (user chooses), "full_upload" or "full_download".
    /// Media sync starts in the background unless a full sync is required.
    fn sync(&mut self) -> ApiResult {
        let mut login = self.require_login()?;
        let auth = login.auth().map_err(|e| self.err(e))?;
        self.shown = None;
        let client = self.client.clone();
        let tr = self.tr.clone();
        let col = self
            .col
            .as_mut()
            .ok_or_else(|| ApiError::new("not_open", "the collection is not open"))?;
        let output = self
            .runtime
            .block_on(col.normal_sync(auth, client))
            .map_err(|e| ApiError::anki(e, &tr))?;

        // AnkiWeb may move the account to another server.
        if let Some(endpoint) = output.new_endpoint.clone() {
            login.endpoint = Some(endpoint);
            self.save_login(Some(login))?;
        }
        let required = match output.required {
            SyncActionRequired::NoChanges | SyncActionRequired::NormalSyncRequired => "none",
            SyncActionRequired::FullSyncRequired {
                upload_ok: false, ..
            } => "full_download",
            SyncActionRequired::FullSyncRequired {
                download_ok: false, ..
            } => "full_upload",
            SyncActionRequired::FullSyncRequired { .. } => "full_sync",
        };
        if required == "none" {
            self.start_media_sync()?;
        }
        Ok(json!({ "required": required, "server_message": output.server_message }))
    }

    /// `full_sync {upload: bool}`: replace one side with the other.
    fn full_sync(&mut self, args: &Value) -> ApiResult {
        let upload = args
            .get("upload")
            .and_then(Value::as_bool)
            .ok_or_else(|| ApiError::new("args", "missing boolean argument 'upload'"))?;
        let auth = self
            .require_login()?
            .auth()
            .map_err(|e| self.err(e))?;
        self.stop_media_sync();
        self.shown = None;
        let col = self
            .col
            .take()
            .ok_or_else(|| ApiError::new("not_open", "the collection is not open"))?;
        let mut builder = col.as_builder();
        let client = self.client.clone();
        let result = if upload {
            self.runtime.block_on(col.full_upload(auth, client))
        } else {
            let col_path = self.base.join(COLLECTION_FILE);
            self.runtime
                .block_on(full_download(col, &col_path, auth, client))
        };
        // Re-open regardless of the outcome, as Anki's backend does.
        self.col = Some(builder.build().map_err(|e| self.err(e))?);
        result.map_err(|e| self.err(e))?;
        self.start_media_sync()?;
        Ok(json!({}))
    }

    fn start_media_sync(&mut self) -> std::result::Result<(), ApiError> {
        if self.media_sync.as_ref().is_some_and(|m| !m.task.is_finished()) {
            return Ok(());
        }
        self.collect_media_sync();
        let auth = self.require_login()?.auth().map_err(|e| self.err(e))?;
        let tr = self.tr.clone();
        let col = self.col()?;
        let manager = col.media().map_err(|e| ApiError::anki(e, &tr))?;
        let progress = col.new_progress_handler();
        let (abort, registration) = AbortHandle::new_pair();
        let handle = self.runtime.handle().clone();
        let client = self.client.clone();
        let task = std::thread::spawn(move || {
            let fut = Abortable::new(manager.sync_media(progress, auth, client, None), registration);
            handle.block_on(fut).unwrap_or(Err(AnkiError::Interrupted))
        });
        self.media_sync = Some(MediaSync { abort, task });
        Ok(())
    }

    /// Record the result of a finished media sync, if any.
    fn collect_media_sync(&mut self) {
        if self.media_sync.as_ref().is_some_and(|m| m.task.is_finished()) {
            let sync = self.media_sync.take().unwrap();
            self.media_error = match sync.task.join() {
                Ok(Ok(())) | Ok(Err(AnkiError::Interrupted)) => None,
                Ok(Err(e)) => Some(e.message(&self.tr)),
                Err(_) => Some("media sync crashed".into()),
            };
        }
    }

    fn stop_media_sync(&mut self) {
        if let Some(sync) = self.media_sync.take() {
            sync.abort.abort();
            let _ = sync.task.join();
        }
    }

    fn media_sync_status(&mut self) -> ApiResult {
        self.collect_media_sync();
        Ok(json!({
            "active": self.media_sync.is_some(),
            "error": self.media_error.take(),
        }))
    }
}

/// Back up when the collection changed after the newest backup, and that
/// backup is at least [BACKUP_INTERVAL_SECS] old (Anki's default interval).
fn backup_due(backups: &std::path::Path, modified_ms: i64) -> bool {
    let newest = std::fs::read_dir(backups)
        .into_iter()
        .flatten()
        .filter_map(|e| e.ok())
        .filter(|e| e.file_name().to_string_lossy().ends_with(".colpkg"))
        .filter_map(|e| e.metadata().ok()?.modified().ok())
        .max();
    let Some(newest) = newest else {
        return true;
    };
    let newest_ms = newest
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0);
    let age_secs = newest.elapsed().map(|d| d.as_secs()).unwrap_or(u64::MAX);
    modified_ms > newest_ms && age_secs >= BACKUP_INTERVAL_SECS
}

/// Package the closed collection at `col_path` as a .colpkg, like Anki's
/// backups and "Export collection" without media
/// (rslib/src/import_export/package/colpkg/export.rs): "meta" (format version),
/// "collection.anki21b" (the database, zstd-compressed) and "media" (an empty
/// list). Anki also adds a stub collection.anki2 that only tells pre-2.1.50
/// versions the file is too new; its importer doesn't read it, so it's left out.
/// Written to a ".part" file, closed, then renamed, so a cut-off backup is never
/// mistaken for a complete one.
fn write_backup(col_path: &Path, out_path: &Path) -> std::io::Result<()> {
    use anki_proto::import_export::package_metadata::Version;
    use anki_proto::import_export::MediaEntries;
    use anki_proto::import_export::PackageMetadata;
    use prost::Message;
    use zip::write::SimpleFileOptions;
    use zip::CompressionMethod;

    let part = out_path.with_extension("colpkg.part");
    let result = (|| {
        let stored = SimpleFileOptions::default().compression_method(CompressionMethod::Stored);
        let mut zip = zip::ZipWriter::new(std::fs::File::create(&part)?);
        zip.start_file("meta", stored)?;
        zip.write_all(&PackageMetadata { version: Version::Latest as i32 }.encode_to_vec())?;
        zip.start_file("collection.anki21b", stored)?;
        let mut encoder = zstd::Encoder::new(&mut zip, 0)?;
        std::io::copy(&mut std::fs::File::open(col_path)?, &mut encoder)?;
        encoder.finish()?;
        zip.start_file("media", stored)?;
        zip.write_all(&zstd::encode_all(&MediaEntries::default().encode_to_vec()[..], 0)?)?;
        let file = zip.finish()?;
        file.sync_all()
    })();
    match result {
        Ok(()) => std::fs::rename(&part, out_path),
        Err(e) => {
            let _ = std::fs::remove_file(&part);
            Err(e)
        }
    }
}

/// Keep the `keep` newest backups (names sort by timestamp).
fn prune_backups(backups: &std::path::Path, keep: usize) {
    let mut names: Vec<_> = std::fs::read_dir(backups)
        .into_iter()
        .flatten()
        .filter_map(|e| e.ok())
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n.starts_with("backup-") && n.ends_with(".colpkg"))
        .collect();
    names.sort();
    let excess = names.len().saturating_sub(keep);
    for name in &names[..excess] {
        let _ = std::fs::remove_file(backups.join(name));
    }
}

/// Fluent wraps placeables in Unicode bidi isolation marks (U+2068..U+2069),
/// which Anki's own UIs strip; the Switch fonts would draw them as boxes.
fn without_bidi_isolates(text: &str) -> String {
    text.chars()
        .filter(|c| !matches!(c, '\u{2066}'..='\u{2069}'))
        .collect()
}

/// Depth-first list of decks below the (unnamed) root, skipping the children
/// of collapsed decks.
fn flatten_tree(node: &DeckTreeNode, out: &mut Vec<Value>) {
    for child in &node.children {
        out.push(json!({
            "id": child.deck_id,
            "name": child.name,
            "level": child.level,
            "collapsed": child.collapsed,
            "has_children": !child.children.is_empty(),
            "filtered": child.filtered,
            "new": child.new_count,
            "learn": child.learn_count,
            "review": child.review_count,
        }));
        if !child.collapsed {
            flatten_tree(child, out);
        }
    }
}

/// `{{hint:Field}}` renders a link whose onclick script reveals a hidden div
/// (rslib template_filters.rs, hint_filter). The card view runs no JavaScript,
/// so point the link at `anki-hint:<div id>` and let the frontend reveal it.
fn native_hints(html: &str) -> String {
    const LINK: &str = "<a class=hint href=\"#\"";
    const GET: &str = "document.getElementById('";
    let mut out = String::with_capacity(html.len());
    let mut rest = html;
    while let Some(start) = rest.find(LINK) {
        let after = &rest[start + LINK.len()..];
        // The div id is in the onclick handler that follows the href.
        let id = after
            .find(GET)
            .map(|i| &after[i + GET.len()..])
            .and_then(|s| s.find('\'').map(|end| &s[..end]))
            .filter(|id| id.chars().all(|c| c.is_ascii_alphanumeric()));
        out.push_str(&rest[..start]);
        match id {
            Some(id) => out.push_str(&format!("<a class=hint href=\"anki-hint:{id}\"")),
            None => out.push_str(LINK),
        }
        rest = after;
    }
    out.push_str(rest);
    out
}

/// Replace `[anki:play:q:0]` markers with links the card view can activate.
fn play_links(html: &str, side: &str) -> String {
    let marker = format!("[anki:play:{side}:");
    let mut out = String::with_capacity(html.len());
    let mut rest = html;
    while let Some(start) = rest.find(&marker) {
        let after = &rest[start + marker.len()..];
        let Some(end) = after.find(']') else { break };
        let index = &after[..end];
        if index.chars().all(|c| c.is_ascii_digit()) {
            out.push_str(&rest[..start]);
            out.push_str(&format!(
                r#"<a class="replay-button soundLink" href="playsound:{side}:{index}">&#9654;</a>"#
            ));
        } else {
            out.push_str(&rest[..start + marker.len() + end + 1]);
        }
        rest = &after[end + 1..];
    }
    out.push_str(rest);
    out
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn hint_links_point_at_their_div() {
        // Exactly what rslib's hint filter emits.
        let html = "<a class=hint href=\"#\"\nonclick=\"this.style.display='none';\n\
                    document.getElementById('hint83fe48607f0f3a66').style.display='block';\n\
                    return false;\" draggable=false>\nfield</a>\n\
                    <div id=\"hint83fe48607f0f3a66\" class=hint style=\"display: none\">foo</div>";
        let out = native_hints(html);
        assert!(out.starts_with("<a class=hint href=\"anki-hint:hint83fe48607f0f3a66\""), "{out}");
        assert!(out.ends_with(r#"<div id="hint83fe48607f0f3a66" class=hint style="display: none">foo</div>"#));
        assert_eq!(native_hints("no hints"), "no hints");
    }
}

fn av_json(tags: &[anki_proto::card_rendering::AvTag]) -> Value {
    tags.iter()
        .map(|tag| match &tag.value {
            Some(av_tag::Value::SoundOrVideo(file)) => json!({ "kind": "sound", "file": file }),
            Some(av_tag::Value::Tts(tts)) => json!({
                "kind": "tts",
                "text": tts.field_text,
                "lang": tts.lang,
            }),
            None => json!({ "kind": "none" }),
        })
        .collect()
}
