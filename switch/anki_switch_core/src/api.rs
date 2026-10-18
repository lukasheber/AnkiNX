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
/// Rendered maths (SVG). Not in collection.media, so media sync ignores it.
const MATH_CACHE: &str = "cache/math";
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
/// Whether the note has `tag` (ignoring case, as Anki compares tags).
fn note_has_tag(col: &Collection, nid: NoteId, tag: &str) -> Result<bool> {
    Ok(col
        .storage
        .get_note(nid)?
        .is_some_and(|note| note.tags.iter().any(|t| t.eq_ignore_ascii_case(tag))))
}

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
    /// Typesets card maths; created when first needed (see math.rs).
    math: Option<crate::math::MathRenderer>,
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
        "deck_overview" => state.deck_overview(&args),
        "stats" => state.stats(&args),
        "deck_options" => state.deck_options(&args),
        "set_deck_options" => state.set_deck_options(&args),
        "custom_study_defaults" => state.custom_study_defaults(&args),
        "custom_study" => state.custom_study(&args),
        "next_card" => state.next_card(&args),
        "answer" => state.answer(&args),
        "type_answer" => state.type_answer(&args),
        "undo" => state.undo(),
        "redo" => state.redo(),
        "card_action" => state.card_action(&args),
        "card_info" => state.card_info(&args),
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
        math: None,
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
        let Some(col) = self.col.take() else {
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

    /// `deck_overview {deck_id}`: Anki's overview screen: today's counts (as
    /// in the deck list), the description as HTML, and whether the deck is
    /// filtered (no options or custom study then).
    fn deck_overview(&mut self, args: &Value) -> ApiResult {
        use anki::decks::DeckKind;
        let did = DeckId(i64_arg(args, "deck_id")?);
        let tr = self.tr.clone();
        let err = |e| ApiError::anki(e, &tr);
        let col = self.col()?;
        let deck = col
            .get_deck(did)
            .map_err(err)?
            .ok_or_else(|| ApiError::new("no_deck", "the deck no longer exists"))?;
        let tree = col.deck_tree(Some(TimestampSecs::now())).map_err(err)?;
        let node = find_deck_node(&tree, did.0);
        // Markdown descriptions are rendered by rslib; HTML ones are shown as
        // they are, as Anki's overview does.
        let description = match &deck.kind {
            DeckKind::Normal(normal) if normal.markdown_description => deck.rendered_description(),
            DeckKind::Normal(normal) => normal.description.clone(),
            _ => String::new(),
        };
        Ok(json!({
            "name": deck.human_name(),
            "new": node.map_or(0, |n| n.new_count),
            "learn": node.map_or(0, |n| n.learn_count),
            "review": node.map_or(0, |n| n.review_count),
            "description": description,
            "filtered": matches!(deck.kind, DeckKind::Filtered(_)),
        }))
    }

    /// `deck_options {deck_id}`: the deck's preset, as Anki's deck options
    /// screen shows it (its commonly changed part): preset name, how many
    /// decks use it, whether FSRS is on (collection-wide), and the values.
    fn deck_options(&mut self, args: &Value) -> ApiResult {
        let did = DeckId(i64_arg(args, "deck_id")?);
        let tr = self.tr.clone();
        let info = self.col()?.get_deck_configs_for_update(did).map_err(|e| ApiError::anki(e, &tr))?;
        let (config, use_count) = current_preset(&info)?;
        let inner = config.config.clone().unwrap_or_default();
        Ok(json!({
            "preset": config.name,
            "deck_count": use_count,
            "fsrs": info.fsrs,
            "values": deck_option_values(&inner),
        }))
    }

    /// `set_deck_options {deck_id, values?, fsrs?}`: change some values of the
    /// deck's preset (the keys `deck_options` returns) and/or turn FSRS on or
    /// off, through Anki's own update (undoable, FSRS memory states updated).
    fn set_deck_options(&mut self, args: &Value) -> ApiResult {
        use anki::deckconfig::UpdateDeckConfigsRequest;
        use anki_proto::deck_config::UpdateDeckConfigsMode;
        let did = DeckId(i64_arg(args, "deck_id")?);
        let tr = self.tr.clone();
        let err = |e| ApiError::anki(e, &tr);
        let col = self.col()?;
        let info = col.get_deck_configs_for_update(did).map_err(err)?;
        let (config, _) = current_preset(&info)?;
        let mut config: anki::deckconfig::DeckConfig = config.clone().into();
        if let Some(values) = args.get("values").and_then(Value::as_object) {
            for (key, value) in values {
                set_deck_option(&mut config.inner, key, value)?;
            }
        }
        let fsrs = args.get("fsrs").and_then(Value::as_bool).unwrap_or(info.fsrs);
        let current = info.current_deck.clone().unwrap_or_default();
        col.update_deck_configs(UpdateDeckConfigsRequest {
            target_deck_id: did,
            configs: vec![config],
            removed_config_ids: vec![],
            mode: UpdateDeckConfigsMode::Normal,
            card_state_customizer: info.card_state_customizer.clone(),
            limits: current.limits.unwrap_or_default(),
            new_cards_ignore_review_limit: info.new_cards_ignore_review_limit,
            apply_all_parent_limits: info.apply_all_parent_limits,
            fsrs,
            fsrs_reschedule: false,
            fsrs_health_check: info.fsrs_health_check,
        })
        .map_err(err)?;
        Ok(json!({}))
    }

    /// `custom_study_defaults {deck_id}`: what Anki's custom study dialog
    /// starts with: how much to extend today's limits, how many cards are
    /// available beyond them, and the deck's tags.
    fn custom_study_defaults(&mut self, args: &Value) -> ApiResult {
        let did = DeckId(i64_arg(args, "deck_id")?);
        let tr = self.tr.clone();
        let d = self.col()?.custom_study_defaults(did).map_err(|e| ApiError::anki(e, &tr))?;
        let tags: Vec<&str> = d.tags.iter().map(|t| t.name.as_str()).collect();
        Ok(json!({
            "extend_new": d.extend_new,
            "extend_review": d.extend_review,
            "available_new": d.available_new + d.available_new_in_children,
            "available_review": d.available_review + d.available_review_in_children,
            "tags": tags,
        }))
    }

    /// `custom_study {deck_id, kind, value, tags?}`, as Anki's custom study:
    /// new_limit / review_limit (cards more today), forgot / ahead / preview
    /// (days), cram_new / cram_due / cram_review / cram_all (card limit, `tags`
    /// to include). Returns the deck to study: the same one after raising a
    /// limit, else the "Custom Study Session" filtered deck.
    fn custom_study(&mut self, args: &Value) -> ApiResult {
        use anki_proto::scheduler::custom_study_request::cram::CramKind;
        use anki_proto::scheduler::custom_study_request::Cram;
        use anki_proto::scheduler::custom_study_request::Value as Study;
        use anki_proto::scheduler::CustomStudyRequest;
        let did = DeckId(i64_arg(args, "deck_id")?);
        let kind = str_arg(args, "kind")?.to_string();
        let value = i64_arg(args, "value")?;
        if !(1..=9999).contains(&value) {
            return Err(ApiError::new("args", format!("invalid value {value}")));
        }
        let tags: Vec<String> = args
            .get("tags")
            .and_then(Value::as_array)
            .map(|tags| tags.iter().filter_map(Value::as_str).map(String::from).collect())
            .unwrap_or_default();
        let cram = |kind: CramKind| {
            Study::Cram(Cram {
                kind: kind as i32,
                card_limit: value as u32,
                tags_to_include: tags.clone(),
                tags_to_exclude: vec![],
            })
        };
        let study = match kind.as_str() {
            "new_limit" => Study::NewLimitDelta(value as i32),
            "review_limit" => Study::ReviewLimitDelta(value as i32),
            "forgot" => Study::ForgotDays(value as u32),
            "ahead" => Study::ReviewAheadDays(value as u32),
            "preview" => Study::PreviewDays(value as u32),
            "cram_new" => cram(CramKind::New),
            "cram_due" => cram(CramKind::Due),
            "cram_review" => cram(CramKind::Review),
            "cram_all" => cram(CramKind::All),
            other => return Err(ApiError::new("args", format!("unknown custom study {other}"))),
        };
        let tr = self.tr.clone();
        let err = |e| ApiError::anki(e, &tr);
        let session_name = tr.custom_study_custom_study_session().to_string();
        let col = self.col()?;
        col.custom_study(CustomStudyRequest { deck_id: did.0, value: Some(study) }).map_err(err)?;
        let study_deck = if kind.ends_with("_limit") {
            did
        } else {
            col.get_deck_id(&session_name).map_err(err)?.unwrap_or(did)
        };
        let name = col.get_deck(study_deck).map_err(err)?.map(|d| d.human_name()).unwrap_or_default();
        Ok(json!({ "deck_id": study_deck.0, "name": name }))
    }

    /// `stats {deck_id?}`: what the statistics screen draws, for one deck
    /// (with its subdecks) or the whole collection: today, a 31-day forecast
    /// (overdue cards counted today), reviews per day over 26 weeks, true
    /// retention, and card counts. From rslib's graph data.
    fn stats(&mut self, args: &Value) -> ApiResult {
        use anki::services::StatsService;
        let tr = self.tr.clone();
        let err = |e| ApiError::anki(e, &tr);
        let col = self.col()?;
        let search = match args.get("deck_id").and_then(Value::as_i64) {
            Some(id) => {
                let deck = col
                    .get_deck(DeckId(id))
                    .map_err(err)?
                    .ok_or_else(|| ApiError::new("no_deck", "the deck no longer exists"))?;
                let name: String = deck
                    .human_name()
                    .chars()
                    .flat_map(|c| match c {
                        '\\' | '"' | '*' | '_' => vec!['\\', c],
                        _ => vec![c],
                    })
                    .collect();
                format!("deck:\"{name}\"")
            }
            None => String::new(),
        };
        const HEATMAP_DAYS: i32 = 26 * 7;
        let g = col
            .graphs(anki_proto::stats::GraphsRequest { search, days: HEATMAP_DAYS as u32 })
            .map_err(err)?;
        // Without the bidi isolation marks Fluent puts around numbers.
        let studied_today = without_bidi_isolates(&col.studied_today().map_err(err)?);

        let today = g.today.unwrap_or_default();
        let mut forecast = vec![0u32; 31];
        for (&day, &count) in &g.future_due.unwrap_or_default().future_due {
            if day < 31 {
                forecast[day.max(0) as usize] += count;
            }
        }
        // Reviews per day, oldest first, today last.
        let mut heatmap = vec![0u32; HEATMAP_DAYS as usize];
        for (&day, r) in &g.reviews.unwrap_or_default().count {
            let index = HEATMAP_DAYS - 1 + day;
            if (0..HEATMAP_DAYS).contains(&index) {
                heatmap[index as usize] += r.learn + r.relearn + r.young + r.mature + r.filtered;
            }
        }
        let retention = g.true_retention.unwrap_or_default();
        let period = |r: Option<anki_proto::stats::graphs_response::true_retention_stats::TrueRetention>| {
            let r = r.unwrap_or_default();
            json!([r.young_passed, r.young_failed, r.mature_passed, r.mature_failed])
        };
        let counts = g.card_counts.unwrap_or_default().excluding_inactive.unwrap_or_default();
        Ok(json!({
            "studied_today": studied_today,
            "today": {
                "answers": today.answer_count,
                "secs": today.answer_millis / 1000,
                "correct": today.correct_count,
                "mature": today.mature_count,
                "mature_correct": today.mature_correct,
                "learn": today.learn_count,
                "review": today.review_count,
                "relearn": today.relearn_count,
            },
            "forecast": forecast,
            "heatmap": heatmap,
            // [young passed, young failed, mature passed, mature failed]
            "retention": {
                "today": period(retention.today),
                "yesterday": period(retention.yesterday),
                "week": period(retention.week),
                "month": period(retention.month),
                "year": period(retention.year),
                "all_time": period(retention.all_time),
            },
            "cards": {
                "new": counts.new_cards,
                "learn": counts.learn + counts.relearn,
                "young": counts.young,
                "mature": counts.mature,
                "suspended": counts.suspended,
                "buried": counts.buried,
            },
        }))
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
        // By field, not self.col(): the maths renderer is borrowed alongside.
        let col = self
            .col
            .as_mut()
            .ok_or_else(|| ApiError::new("not_open", "the collection is not open"))?;
        let queued = col.get_queued_cards(1, false).map_err(err)?;
        let counts = json!({
            "new": queued.new_count,
            "learn": queued.learning_count,
            "review": queued.review_count,
        });
        let Some(card) = queued.cards.into_iter().next() else {
            self.shown = None;
            // What Anki's congratulations screen says.
            let info = col.congrats_info().map_err(err)?;
            let next_learn = (info.learn_remaining > 0 && info.secs_until_next_learn < 86_400)
                .then_some(info.secs_until_next_learn);
            return Ok(json!({
                "done": true,
                "counts": counts,
                "congrats": {
                    // Learning cards due later today, and in how many seconds.
                    "learn_remaining": info.learn_remaining,
                    "secs_until_next_learn": next_learn,
                    // More cards are due but today's limits stop them.
                    "review_limit_reached": info.review_remaining,
                    "new_limit_reached": info.new_remaining,
                    "buried": info.have_sched_buried || info.have_user_buried,
                    "filtered_deck": info.is_filtered_deck,
                },
            }));
        };

        let card_id = card.card.id();
        let note_id = card.card.note_id();
        let ord = card.card.template_idx();
        let proto_card = anki_proto::cards::Card::from(card.card.clone());
        let flag = proto_card.flags;
        // The home deck's "Don't play audio automatically" (filtered decks use
        // the card's original deck, as Anki does).
        let home_deck = DeckId(if proto_card.original_deck_id != 0 {
            proto_card.original_deck_id
        } else {
            proto_card.deck_id
        });
        let config = match col.get_deck(home_deck).map_err(err)?.and_then(|d| d.config_id()) {
            Some(dcid) => col.get_deck_config(dcid, true).map_err(err)?,
            None => None,
        };
        let autoplay = !config.as_ref().is_some_and(|c| c.inner.disable_autoplay);
        let timing = config.as_ref().map_or(Value::Null, |c| study_timing(&c.inner));
        let marked = note_has_tag(col, note_id, "marked").map_err(err)?;
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
        // Maths (\(...\), \[...\]) as images; created on the first card with maths.
        let night = args.get("night_mode").and_then(Value::as_bool).unwrap_or(false);
        if [&question, &answer].iter().any(|side| side.contains("\\(") || side.contains("\\[")) {
            let base = &self.base;
            let math = self
                .math
                .get_or_insert_with(|| crate::math::MathRenderer::new(base.join(MATH_CACHE)));
            // The reviewer's text colour (see the app's NIGHT_CSS).
            let color = if night { "#e6e6e6" } else { "#000000" };
            question = math.render_html(&question, color);
            answer = math.render_html(&answer, color);
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
            "note_id": note_id.0,
            // Flag colour (0 none, 1 red .. 7 purple) and the note's "marked" tag.
            "flag": flag,
            "marked": marked,
            // Play the side's audio when it is shown (deck option).
            "autoplay": autoplay,
            // Answer timer and auto advance (deck options; see study_timing).
            "timing": timing,
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
        let err = |e| ApiError::anki(e, &tr);
        let col = self.col()?;
        // Anki tags a note "leech" (and may suspend the card) when a lapse
        // reaches the deck's threshold; the reviewer tells the user.
        let note_id = col
            .storage
            .get_card(shown.card_id)
            .map_err(err)?
            .map(|card| card.note_id());
        let was_leech = match note_id {
            Some(nid) => note_has_tag(col, nid, "leech").map_err(err)?,
            None => false,
        };
        col.answer_card(&mut answer).map_err(err)?;
        let leech = match note_id {
            Some(nid) => !was_leech && note_has_tag(col, nid, "leech").map_err(err)?,
            None => false,
        };
        let suspended = col
            .storage
            .get_card(shown.card_id)
            .map_err(err)?
            .map(|card| anki_proto::cards::Card::from(card).queue == -1)
            .unwrap_or(false);
        Ok(json!({ "leech": leech, "suspended": leech && suspended }))
    }

    /// `card_action {card_id, action, flag?, days?}`: the reviewer's card menu.
    /// Actions: flag (with `flag` 0-7), toggle_mark, bury_card, bury_note,
    /// suspend_card, suspend_note, forget, set_due_date (with `days`, as in
    /// Anki: "0" today, "3-7" a random day in that range, "5!" also sets the
    /// interval). All are undoable with `undo`.
    fn card_action(&mut self, args: &Value) -> ApiResult {
        use anki::scheduler::new::ScheduleAsNewContext;
        use anki_proto::scheduler::bury_or_suspend_cards_request::Mode;
        let cid = CardId(i64_arg(args, "card_id")?);
        let action = str_arg(args, "action")?.to_string();
        let tr = self.tr.clone();
        let err = |e| ApiError::anki(e, &tr);
        let col = self.col()?;
        let card = col
            .storage
            .get_card(cid)
            .map_err(err)?
            .ok_or_else(|| ApiError::new("no_card", "the card no longer exists"))?;
        let nid = card.note_id();
        let mut result = json!({});
        match action.as_str() {
            "flag" => {
                let flag = i64_arg(args, "flag")?.clamp(0, 7) as u32;
                col.set_card_flag(&[cid], flag).map_err(err)?;
            }
            "toggle_mark" => {
                let marked = note_has_tag(col, nid, "marked").map_err(err)?;
                if marked {
                    col.remove_tags_from_notes(&[nid], "marked").map_err(err)?;
                } else {
                    col.add_tags_to_notes(&[nid], "marked").map_err(err)?;
                }
                result = json!({ "marked": !marked });
            }
            "bury_card" | "bury_note" | "suspend_card" | "suspend_note" => {
                let cids = if action.ends_with("_note") {
                    col.storage.all_card_ids_of_note_in_template_order(nid).map_err(err)?
                } else {
                    vec![cid]
                };
                let mode = if action.starts_with("bury") { Mode::BuryUser } else { Mode::Suspend };
                col.bury_or_suspend_cards(&cids, mode).map_err(err)?;
            }
            // As Anki's reviewer: back to the end of the new queue (or its
            // old position), keeping the review and lapse counts.
            "forget" => {
                col.reschedule_cards_as_new(&[cid], true, true, false, Some(ScheduleAsNewContext::Reviewer))
                    .map_err(err)?;
            }
            "set_due_date" => {
                let days = str_arg(args, "days")?.trim().to_string();
                col.set_due_date(&[cid], &days, None).map_err(err)?;
            }
            other => return Err(ApiError::new("args", format!("unknown card action '{other}'"))),
        }
        // A card that leaves the queue is no longer the one being shown.
        if !matches!(action.as_str(), "flag" | "toggle_mark") {
            self.shown = None;
        }
        Ok(result)
    }

    /// `card_info {card_id}`: Anki's card info (stats::CardStatsResponse) as
    /// JSON; dates are Unix seconds, intervals in days.
    fn card_info(&mut self, args: &Value) -> ApiResult {
        let cid = CardId(i64_arg(args, "card_id")?);
        let tr = self.tr.clone();
        let s = self.col()?.card_stats(cid).map_err(|e| ApiError::anki(e, &tr))?;
        let revlog: Vec<Value> = s
            .revlog
            .iter()
            .map(|r| {
                json!({
                    "time": r.time,
                    // 0 learning, 1 review, 2 relearning, 3 filtered, 4 manual, 5 rescheduled
                    "kind": r.review_kind,
                    "rating": r.button_chosen,
                    "interval_secs": r.interval,
                    "ease": r.ease,
                    "taken_secs": r.taken_secs,
                })
            })
            .collect();
        Ok(json!({
            "card_id": s.card_id,
            "note_id": s.note_id,
            "deck": s.deck,
            "original_deck": s.original_deck,
            "preset": s.preset,
            "notetype": s.notetype,
            "card_type": s.card_type,
            "added": s.added,
            "first_review": s.first_review,
            "latest_review": s.latest_review,
            "due_date": s.due_date,
            "due_position": s.due_position,
            "interval": s.interval,
            "ease": s.ease,
            "reviews": s.reviews,
            "lapses": s.lapses,
            "average_secs": s.average_secs,
            "total_secs": s.total_secs,
            "stability": s.memory_state.as_ref().map(|m| m.stability),
            "difficulty": s.memory_state.as_ref().map(|m| m.difficulty),
            "retrievability": s.fsrs_retrievability,
            "revlog": revlog,
        }))
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

    /// `redo`: redo what `undo` undid last, if nothing changed since.
    fn redo(&mut self) -> ApiResult {
        let tr = self.tr.clone();
        self.shown = None;
        let col = self.col()?;
        if col.can_redo().is_none() {
            return Ok(json!({ "redone": false }));
        }
        col.redo().map_err(|e| ApiError::anki(e, &tr))?;
        Ok(json!({ "redone": true }))
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
// ----- Deck options -----

type DeckConfigInner = anki::deckconfig::DeckConfigInner;

/// The deck's preset and how many decks use it.
fn current_preset(
    info: &anki_proto::deck_config::DeckConfigsForUpdate,
) -> std::result::Result<(&anki_proto::deck_config::DeckConfig, u32), ApiError> {
    let id = info.current_deck.as_ref().map(|d| d.config_id).unwrap_or_default();
    info.all_config
        .iter()
        .find_map(|c| c.config.as_ref().filter(|c| c.id == id).map(|config| (config, c.use_count)))
        .ok_or_else(|| ApiError::new("no_deck", "the deck has no options preset"))
}

/// The deck options the reviewer needs while studying: the answer timer and
/// auto advance (which the user turns on per session, as in Anki).
fn study_timing(c: &DeckConfigInner) -> Value {
    json!({
        "show_timer": c.show_timer,
        "stop_timer_on_answer": c.stop_timer_on_answer,
        "cap_answer_secs": c.cap_answer_time_to_secs,
        "question_secs": c.seconds_to_show_question,
        "answer_secs": c.seconds_to_show_answer,
        "wait_for_audio": c.wait_for_audio,
        "question_action": question_action_name(c.question_action),
        "answer_action": answer_action_name(c.answer_action),
    })
}

fn question_action_name(action: i32) -> &'static str {
    use anki_proto::deck_config::deck_config::config::QuestionAction;
    if action == QuestionAction::ShowReminder as i32 {
        "reminder"
    } else {
        "show_answer"
    }
}

fn answer_action_name(action: i32) -> &'static str {
    use anki_proto::deck_config::deck_config::config::AnswerAction;
    match AnswerAction::try_from(action).unwrap_or(AnswerAction::BuryCard) {
        AnswerAction::BuryCard => "bury",
        AnswerAction::AnswerAgain => "again",
        AnswerAction::AnswerGood => "good",
        AnswerAction::AnswerHard => "hard",
        AnswerAction::ShowReminder => "reminder",
    }
}

fn deck_option_values(c: &DeckConfigInner) -> Value {
    use anki_proto::deck_config::deck_config::config::LeechAction;
    json!({
        "new_per_day": c.new_per_day,
        "reviews_per_day": c.reviews_per_day,
        "learn_steps": steps_text(&c.learn_steps),
        "graduating_interval_good": c.graduating_interval_good,
        "graduating_interval_easy": c.graduating_interval_easy,
        "relearn_steps": steps_text(&c.relearn_steps),
        "minimum_lapse_interval": c.minimum_lapse_interval,
        "leech_threshold": c.leech_threshold,
        "leech_action": if c.leech_action == LeechAction::TagOnly as i32 { "tag" } else { "suspend" },
        "bury_new": c.bury_new,
        "bury_reviews": c.bury_reviews,
        "bury_interday_learning": c.bury_interday_learning,
        "maximum_review_interval": c.maximum_review_interval,
        "desired_retention": c.desired_retention,
        "starting_ease": c.initial_ease,
        "easy_bonus": c.easy_multiplier,
        "interval_modifier": c.interval_multiplier,
        "hard_interval": c.hard_multiplier,
        "new_interval": c.lapse_multiplier,
        "disable_autoplay": c.disable_autoplay,
        "skip_question_when_replaying_answer": c.skip_question_when_replaying_answer,
        "show_timer": c.show_timer,
        "stop_timer_on_answer": c.stop_timer_on_answer,
        "cap_answer_time_to_secs": c.cap_answer_time_to_secs,
        "seconds_to_show_question": c.seconds_to_show_question,
        "seconds_to_show_answer": c.seconds_to_show_answer,
        "wait_for_audio": c.wait_for_audio,
        "question_action": question_action_name(c.question_action),
        "answer_action": answer_action_name(c.answer_action),
    })
}

fn set_deck_option(c: &mut DeckConfigInner, key: &str, value: &Value) -> std::result::Result<(), ApiError> {
    use anki_proto::deck_config::deck_config::config::LeechAction;
    let bad = || ApiError::new("args", format!("invalid value for {key}: {value}"));
    let count = || value.as_u64().filter(|&n| n <= 9999).map(|n| n as u32).ok_or_else(bad);
    let days = || value.as_u64().filter(|&n| (1..=36500).contains(&n)).map(|n| n as u32).ok_or_else(bad);
    let ratio = || value.as_f64().filter(|n| n.is_finite() && *n >= 0.0).map(|n| n as f32).ok_or_else(bad);
    let flag = || value.as_bool().ok_or_else(bad);
    let steps = || value.as_str().and_then(parse_steps).ok_or_else(bad);
    match key {
        "new_per_day" => c.new_per_day = count()?,
        "reviews_per_day" => c.reviews_per_day = count()?,
        "learn_steps" => c.learn_steps = steps()?,
        "graduating_interval_good" => c.graduating_interval_good = days()?,
        "graduating_interval_easy" => c.graduating_interval_easy = days()?,
        "relearn_steps" => c.relearn_steps = steps()?,
        "minimum_lapse_interval" => c.minimum_lapse_interval = days()?,
        "leech_threshold" => c.leech_threshold = count()?,
        "leech_action" => {
            c.leech_action = match value.as_str() {
                Some("suspend") => LeechAction::Suspend as i32,
                Some("tag") => LeechAction::TagOnly as i32,
                _ => return Err(bad()),
            }
        }
        "bury_new" => c.bury_new = flag()?,
        "bury_reviews" => c.bury_reviews = flag()?,
        "bury_interday_learning" => c.bury_interday_learning = flag()?,
        "maximum_review_interval" => c.maximum_review_interval = days()?,
        "desired_retention" => {
            c.desired_retention = Some(ratio()?).filter(|r| (0.7..=0.99).contains(r)).ok_or_else(bad)?
        }
        "starting_ease" => c.initial_ease = Some(ratio()?).filter(|r| (1.31..=5.0).contains(r)).ok_or_else(bad)?,
        "easy_bonus" => c.easy_multiplier = Some(ratio()?).filter(|r| (1.0..=5.0).contains(r)).ok_or_else(bad)?,
        "interval_modifier" => {
            c.interval_multiplier = Some(ratio()?).filter(|r| (0.5..=2.0).contains(r)).ok_or_else(bad)?
        }
        "hard_interval" => c.hard_multiplier = Some(ratio()?).filter(|r| (0.5..=1.3).contains(r)).ok_or_else(bad)?,
        "new_interval" => c.lapse_multiplier = Some(ratio()?).filter(|r| *r <= 1.0).ok_or_else(bad)?,
        "disable_autoplay" => c.disable_autoplay = flag()?,
        "skip_question_when_replaying_answer" => c.skip_question_when_replaying_answer = flag()?,
        "show_timer" => c.show_timer = flag()?,
        "stop_timer_on_answer" => c.stop_timer_on_answer = flag()?,
        "cap_answer_time_to_secs" => {
            c.cap_answer_time_to_secs = value.as_u64().filter(|n| (1..=7200).contains(n)).ok_or_else(bad)? as u32
        }
        "seconds_to_show_question" => {
            c.seconds_to_show_question = Some(ratio()?).filter(|s| *s <= 9999.0).ok_or_else(bad)?
        }
        "seconds_to_show_answer" => c.seconds_to_show_answer = Some(ratio()?).filter(|s| *s <= 9999.0).ok_or_else(bad)?,
        "wait_for_audio" => c.wait_for_audio = flag()?,
        "question_action" => {
            use anki_proto::deck_config::deck_config::config::QuestionAction;
            c.question_action = match value.as_str() {
                Some("show_answer") => QuestionAction::ShowAnswer as i32,
                Some("reminder") => QuestionAction::ShowReminder as i32,
                _ => return Err(bad()),
            }
        }
        "answer_action" => {
            use anki_proto::deck_config::deck_config::config::AnswerAction;
            c.answer_action = match value.as_str() {
                Some("bury") => AnswerAction::BuryCard as i32,
                Some("again") => AnswerAction::AnswerAgain as i32,
                Some("good") => AnswerAction::AnswerGood as i32,
                Some("hard") => AnswerAction::AnswerHard as i32,
                Some("reminder") => AnswerAction::ShowReminder as i32,
                _ => return Err(bad()),
            }
        }
        _ => return Err(ApiError::new("args", format!("unknown deck option {key}"))),
    }
    Ok(())
}

/// Learning steps (minutes) as Anki writes them: "1m 10m 1d".
fn steps_text(steps: &[f32]) -> String {
    steps
        .iter()
        .map(|&minutes| {
            if minutes >= 1440.0 && (minutes % 1440.0).abs() < 0.001 {
                format!("{}d", minutes / 1440.0)
            } else if minutes >= 60.0 && (minutes % 60.0).abs() < 0.001 {
                format!("{}h", minutes / 60.0)
            } else if minutes < 1.0 {
                format!("{}s", (minutes * 60.0).round())
            } else {
                format!("{minutes}m")
            }
        })
        .collect::<Vec<_>>()
        .join(" ")
}

/// "1m 10m 1d" (s, m, h, d; minutes without a unit) to minutes; None if any
/// step is not a positive duration.
fn parse_steps(text: &str) -> Option<Vec<f32>> {
    text.split_whitespace()
        .map(|step| {
            let split = step.find(|c: char| c.is_ascii_alphabetic()).unwrap_or(step.len());
            let number: f32 = step[..split].parse().ok().filter(|n: &f32| n.is_finite() && *n > 0.0)?;
            let minutes = match &step[split..] {
                "" | "m" => 1.0,
                "s" => 1.0 / 60.0,
                "h" => 60.0,
                "d" => 1440.0,
                _ => return None,
            };
            Some(number * minutes)
        })
        .collect()
}

fn find_deck_node(node: &DeckTreeNode, deck_id: i64) -> Option<&DeckTreeNode> {
    if node.deck_id == deck_id {
        return Some(node);
    }
    node.children.iter().find_map(|child| find_deck_node(child, deck_id))
}

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
