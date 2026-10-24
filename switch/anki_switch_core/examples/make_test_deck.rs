//! The AnkiNX test deck: Anki's card types and the template features it
//! supports, one or a few cards each, every card saying what to check.
//! Built with Anki's own code (rslib) and exported as an .apkg with media,
//! card states and deck options.
//! Usage: make_test_deck <media dir> <out.apkg>   (see tools/make_test_deck.sh)

use std::path::PathBuf;
use std::sync::Arc;

use anki::collection::Collection;
use anki::collection::CollectionBuilder;
use anki::notetype::CardTemplate;
use anki::notetype::NoteField;
use anki::prelude::*;
use anki::scheduler::answering::CardAnswer;
use anki::scheduler::answering::Rating;
use anki::search::SortMode;
use anki_proto::import_export::ExportAnkiPackageOptions;
use anki_proto::scheduler::bury_or_suspend_cards_request::Mode as BuryOrSuspendMode;

const ROOT: &str = "AnkiNX Test";
const TAG: &str = "ankinx-test";

/// The grey "Check:" line at the end of a card (plain text: escaped, so a
/// mention of <script> is not a script).
fn check(text: &str) -> String {
    let text = text.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    format!(
        "<div style=\"margin-top:20px;padding-top:8px;border-top:1px dashed #999;font-size:15px;color:#888;\
         text-align:left\">Check: {text}</div>"
    )
}

struct Deck {
    col: Collection,
    media: PathBuf,
}

impl Deck {
    fn deck(&mut self, name: &str) -> DeckId {
        self.col.get_or_create_normal_deck(&format!("{ROOT}::{name}")).unwrap().id
    }

    fn stock(&mut self, name: &str) -> Arc<Notetype> {
        self.col.get_notetype_by_name(name).unwrap().unwrap()
    }

    /// A note type copied from `base` with its own fields, templates and CSS
    /// (empty: the base's).
    fn notetype(
        &mut self,
        base: &str,
        name: &str,
        fields: &[&str],
        templates: &[(&str, &str, &str)],
        css: &str,
    ) -> Arc<Notetype> {
        let mut nt = (*self.stock(base)).clone();
        nt.id = NotetypeId(0);
        nt.name = name.into();
        if !fields.is_empty() {
            nt.fields = fields.iter().map(|f| NoteField::new(*f)).collect();
        }
        nt.templates = templates.iter().map(|(n, q, a)| CardTemplate::new(*n, *q, *a)).collect();
        if !css.is_empty() {
            nt.config.css = css.into();
        }
        self.col.add_notetype(&mut nt, false).unwrap();
        self.stock(name)
    }

    fn add(&mut self, deck: &str, nt: &Notetype, fields: &[&str]) -> (NoteId, Vec<CardId>) {
        let did = self.deck(deck);
        let mut note = nt.new_note();
        for (i, f) in fields.iter().enumerate() {
            note.set_field(i, *f).unwrap();
        }
        let sub = deck.split("::").next().unwrap_or(deck).split_once(' ').map_or(deck, |(_, n)| n);
        note.tags = vec![format!("{TAG}::{}", sub.to_lowercase().replace(' ', "-"))];
        self.col.add_note(&mut note, did).unwrap();
        let cards = self
            .col
            .search_cards(format!("nid:{}", note.id).as_str(), SortMode::NoOrder)
            .unwrap();
        (note.id, cards)
    }

    fn image_occlusion(&mut self, deck: &str, occlusions: &str, header: &str, back_extra: &str) {
        let did = self.deck(deck);
        self.col.set_current_deck(did).unwrap();
        self.col
            .add_image_occlusion_note(anki_proto::image_occlusion::AddImageOcclusionNoteRequest {
                image_path: self.media.join("ankinx_test_map.png").to_string_lossy().into(),
                occlusions: occlusions.into(),
                header: header.into(),
                back_extra: back_extra.into(),
                tags: vec![format!("{TAG}::image-occlusion")],
                notetype_id: 0,
            })
            .unwrap();
    }

    fn answer(&mut self, card_id: CardId, rating: Rating) {
        let states = self.col.get_scheduling_states(card_id).unwrap();
        let new_state = match rating {
            Rating::Again => states.again,
            Rating::Hard => states.hard,
            Rating::Good => states.good,
            Rating::Easy => states.easy,
        };
        self.col
            .answer_card(&mut CardAnswer {
                card_id,
                current_state: states.current,
                new_state,
                rating,
                answered_at: TimestampMillis::now(),
                milliseconds_taken: 5_000,
                custom_data: None,
                from_queue: false,
            })
            .unwrap();
    }
}

fn main() {
    let media_dir = PathBuf::from(std::env::args().nth(1).expect("usage: make_test_deck <media dir> <out.apkg>"));
    let out = PathBuf::from(std::env::args().nth(2).expect("usage: make_test_deck <media dir> <out.apkg>"));
    let dir = std::env::temp_dir().join(format!("ankinx-test-deck-{}", std::process::id()));
    std::fs::create_dir_all(dir.join("collection.media")).unwrap();
    // Media the notes name (the occlusion image is added by its notes).
    for entry in std::fs::read_dir(&media_dir).unwrap() {
        let path = entry.unwrap().path();
        let name = path.file_name().unwrap().to_string_lossy().to_string();
        if name != "ankinx_test_map.png" {
            std::fs::copy(&path, dir.join("collection.media").join(&name)).unwrap();
        }
    }
    let col = CollectionBuilder::new(dir.join("collection.anki2"))
        .set_media_paths(dir.join("collection.media"), dir.join("collection.mdb"))
        .build()
        .unwrap();
    let mut d = Deck { col, media: media_dir };

    basic_types(&mut d);
    cloze(&mut d);
    image_occlusion(&mut d);
    templates(&mut d);
    formatting(&mut d);
    languages(&mut d);
    media(&mut d);
    maths(&mut d);
    css(&mut d);
    javascript(&mut d);
    states(&mut d);
    deck_options(&mut d);

    let count = d
        .col
        .export_apkg(
            &out,
            ExportAnkiPackageOptions { with_scheduling: true, with_deck_configs: true, with_media: true, legacy: false },
            format!("\"deck:{ROOT}\"").as_str(),
            None,
        )
        .unwrap();
    d.col.close(None).unwrap();
    std::fs::remove_dir_all(&dir).ok();
    println!("{} notes in {}", count, out.display());
    if !verify(&out, std::env::args().nth(3).map(PathBuf::from)) {
        std::process::exit(1);
    }
}

/// Import the package into a fresh collection, as Anki would, and render
/// every card. `keep`: leave that collection there (for the desktop preview).
fn verify(apkg: &PathBuf, keep: Option<PathBuf>) -> bool {
    let dir = keep.clone().unwrap_or_else(|| std::env::temp_dir().join(format!("ankinx-test-verify-{}", std::process::id())));
    std::fs::remove_dir_all(&dir).ok();
    std::fs::create_dir_all(dir.join("collection.media")).unwrap();
    let mut col = CollectionBuilder::new(dir.join("collection.anki2"))
        .set_media_paths(dir.join("collection.media"), dir.join("collection.mdb"))
        .build()
        .unwrap();
    col.import_apkg(
        apkg,
        anki_proto::import_export::ImportAnkiPackageOptions {
            with_scheduling: true,
            with_deck_configs: true,
            ..Default::default()
        },
    )
    .unwrap();
    let cards = col.search_cards(format!("\"deck:{ROOT}\"").as_str(), SortMode::NoOrder).unwrap();
    let mut problems = 0;
    for &cid in &cards {
        match col.render_existing_card(cid, false, false) {
            Ok(out) => {
                let text = |nodes: &[anki::template::RenderedNode]| {
                    nodes
                        .iter()
                        .map(|n| match n {
                            anki::template::RenderedNode::Text { text } => text.clone(),
                            _ => String::new(),
                        })
                        .collect::<String>()
                };
                if out.is_empty {
                    problems += 1;
                    let q = text(&out.qnodes);
                    println!("problem: card {cid} has an empty front: {}", &q[..q.len().min(120)]);
                }
            }
            Err(e) => {
                problems += 1;
                println!("problem: card {cid}: {e:?}");
            }
        }
    }
    let media = std::fs::read_dir(dir.join("collection.media")).unwrap().count();
    col.close(None).unwrap();
    if keep.is_none() {
        std::fs::remove_dir_all(&dir).ok();
    }
    println!("verified: {} cards render after import, {} media files, {} problems", cards.len(), media, problems);
    problems == 0
}

fn basic_types(d: &mut Deck) {
    const DECK: &str = "01 Basic note types";
    let basic = d.stock("Basic");
    d.add(DECK, &basic, &[
        "<b>Basic</b>: the front of a one-card note.",
        &format!("The back.{}", check("one card; the answer side repeats the front above a line.")),
    ]);
    let reversed = d.stock("Basic (and reversed card)");
    d.add(DECK, &reversed, &[
        "Hund (German)",
        &format!("dog{}", check("two cards from this note: \"Hund\" → \"dog\" and \"dog\" → \"Hund\".")),
    ]);
    let optional = d.stock("Basic (optional reversed card)");
    d.add(DECK, &optional, &[
        "Optional reverse, <i>filled</i>",
        &format!("Two cards{}", check("this note has \"Add Reverse\" filled, so it also has a reversed card.")),
        "y",
    ]);
    d.add(DECK, &optional, &[
        "Optional reverse, <i>empty</i>",
        &format!("One card{}", check("\"Add Reverse\" is empty: no reversed card for this note.")),
        "",
    ]);
    let typing = d.stock("Basic (type in the answer)");
    d.add(DECK, &typing, &[
        "Type the capital of Italy.",
        &format!("Rome{}", check("an answer box (A or a tap opens the keyboard); the answer side compares what you typed with \"Rome\" (green/red).")),
    ]);
    d.add(DECK, &typing, &[
        "Type the French word for coffee (with the accent).",
        &format!("café{}", check("type \"café\" for an exact match; \"cafe\" shows the é as missed.")),
    ]);
    d.add(DECK, &typing, &[
        "Type 猫 in hiragana.",
        &format!("ねこ{}", check("Japanese input on the Switch keyboard; the comparison handles non-Latin text.")),
    ]);
}

fn cloze(d: &mut Deck) {
    const DECK: &str = "02 Cloze";
    let cloze = d.stock("Cloze");
    d.add(DECK, &cloze, &[
        "{{c1::Canberra}} is the capital of {{c2::Australia}}; its Parliament House opened in {{c3::1988}}.",
        &check("three cards (c1, c2, c3); on each, one gap is [...] and the others show their text."),
    ]);
    d.add(DECK, &cloze, &[
        "The chemical symbol for gold is {{c1::Au::two letters}}.",
        &check("the gap shows the hint: [two letters]."),
    ]);
    d.add(DECK, &cloze, &[
        "{{c1::Red}} and {{c1::blue}} make purple.",
        &check("one card with both gaps hidden (same number twice)."),
    ]);
    d.add(DECK, &cloze, &[
        "{{c1::Canberra was {{c2::purpose-built}} as a compromise}} between Sydney and Melbourne.",
        &check("nested clozes, two cards: c1 hides the whole clause; c2 hides only \"purpose-built\"."),
    ]);
    d.add(DECK, &cloze, &[
        "<ul><li>{{c1::<b>Mercury</b>}}</li><li>Venus</li><li>Earth</li></ul>\
         <table border=\"1\" style=\"margin:auto\"><tr><th>Planet</th><th>Colour</th></tr>\
         <tr><td>Mars</td><td>{{c2::red}}</td></tr></table>",
        &check("gaps inside a list and a table; the bold Mercury is revealed bold."),
    ]);
    d.add(DECK, &cloze, &[
        r"Mass–energy: \(E = {{c1::mc^2}}\)",
        &check("a gap inside maths: the formula shows [...] where mc² goes, and the answer typesets it."),
    ]);
    d.add(DECK, &cloze, &[
        "Hidden picture: {{c1::<img src=\"ankinx_test_image.png\">}}",
        &check("the question shows [...], the answer the green PNG."),
    ]);
    let typed = d.notetype(
        "Cloze",
        "AnkiNX Test: cloze (type in)",
        &[],
        &[("Cloze", "{{cloze:Text}}<br><br>{{type:cloze:Text}}", "{{cloze:Text}}<br><br>{{type:cloze:Text}}<br>{{Back Extra}}")],
        "",
    );
    d.add(DECK, &typed, &[
        "The capital of Japan is {{c1::Tokyo}}.",
        &check("{{type:cloze:...}}: type the hidden word; the answer compares it with \"Tokyo\"."),
    ]);
}

fn image_occlusion(d: &mut Deck) {
    const DECK: &str = "03 Image Occlusion";
    let shapes = |oi: &str| {
        format!(
            "{{{{c1::image-occlusion:rect:left=.075:top=.12:width=.3:height=.28{oi}}}}}\
             {{{{c2::image-occlusion:ellipse:left=.55:top=.1:rx=.15:ry=.18{oi}}}}}\
             {{{{c3::image-occlusion:polygon:left=.35:top=.54:points=.35,.92 .5,.54 .65,.92{oi}}}}}\
             {{{{c4::image-occlusion:rect:left=.75:top=.66:width=.2:height=.24{oi}}}}}"
        )
    };
    d.image_occlusion(
        DECK,
        &shapes(":oi=1"),
        "Hide all, guess one: name the colour",
        &check("four cards; on each, all four regions are masked and the one asked is highlighted (rectangle, ellipse, triangle, rectangle). X toggles the masks on the answer."),
    );
    d.image_occlusion(
        DECK,
        &shapes(""),
        "Hide one, guess one: name the colour",
        &check("four cards; only the asked region is masked, the others are visible."),
    );
    d.image_occlusion(
        DECK,
        "{{c1::image-occlusion:rect:left=.075:top=.12:width=.3:height=.28:oi=1}}\
         {{c1::image-occlusion:ellipse:left=.55:top=.1:rx=.15:ry=.18:oi=1}}\
         {{c2::image-occlusion:rect:left=.33:top=.6:width=.34:height=.2:angle=15:fill=#7e57c2:oi=1}}\
         {{c3::image-occlusion:text:left=.1:top=.82:text=Text label:scale=1:fs=.06}}",
        "Grouped, rotated and coloured masks, and a text label",
        &check("c1 masks red and blue together (grouped); c2 is a purple mask turned 15°; \"Text label\" is drawn as text."),
    );
}

fn templates(d: &mut Deck) {
    const DECK: &str = "04 Template features";
    let answer_check = check(
        "special fields show this card's tags (ankinx-test::template-features), deck, subdeck, note type and \
         card name; Flag shows the card's flag (empty unless flagged).",
    );
    let nt = d.notetype(
        "Basic",
        "AnkiNX Test: templates",
        &["Front", "Back", "Extra", "Optional", "Reading"],
        &[
            (
                "Card 1 (conditionals)",
                "<p style=\"color:#888\">Card 1: conditional sections</p>{{Front}}\
                 {{#Extra}}<p>Extra is filled, so this section (#Extra) shows: {{Extra}}</p>{{/Extra}}\
                 {{^Optional}}<p><i>Optional is empty, so this section (^Optional) shows.</i></p>{{/Optional}}",
                &format!(
                    "{{{{FrontSide}}}}<hr id=answer>{{{{Back}}}}<p style=\"text-align:left\">Tags: <b>{{{{Tags}}}}</b><br>\
                     Deck: <b>{{{{Deck}}}}</b><br>Subdeck: <b>{{{{Subdeck}}}}</b><br>Note type: <b>{{{{Type}}}}</b><br>\
                     Card: <b>{{{{Card}}}}</b><br>Flag: <b>{{{{CardFlag}}}}</b></p>{answer_check}"
                ),
            ),
            (
                "Card 2 (only if Optional)",
                "{{#Optional}}<p style=\"color:#888\">Card 2: exists only because Optional is filled</p>{{Optional}}{{/Optional}}",
                &format!(
                    "{{{{FrontSide}}}}<hr id=answer>{{{{Back}}}}{}",
                    check("only one note of this type has Optional filled, so there is only one Card 2 in the deck.")
                ),
            ),
            (
                "Card 3 (filters)",
                "<p style=\"color:#888\">Card 3: field filters</p>{{Front}}<p>{{hint:Extra}}</p>",
                &format!(
                    "{{{{FrontSide}}}}<hr id=answer><div style=\"text-align:left\">\
                     text: {{{{text:Back}}}}<br>furigana: {{{{furigana:Reading}}}}<br>kana: {{{{kana:Reading}}}}<br>\
                     kanji: {{{{kanji:Reading}}}}<br>tts: {{{{tts en_US:Front}}}}</div>{}",
                    check(
                        "the question has a \"Show Extra\" hint link (tap or X). On the answer: text: is the Back \
                         field without its bold/italic; furigana: shows readings above the kanji; kana: only \
                         the readings; kanji: only the kanji; tts: nothing (no speech on the console; a speaker \
                         button in Anki).",
                    )
                ),
            ),
        ],
        "",
    );
    d.add(DECK, &nt, &[
        "First note (Optional filled)",
        "<b>bold</b> and <i>italic</i> back",
        "an extra detail",
        "The optional field's text",
        "日本語[にほんご]を 勉強[べんきょう]する",
    ]);
    d.add(DECK, &nt, &[
        "Second note (Optional empty)",
        "<b>bold</b> and <i>italic</i> back",
        "another extra detail",
        "",
        "漢字[かんじ]",
    ]);
    let nc = d.notetype(
        "Basic (type in the answer)",
        "AnkiNX Test: type (ignore accents)",
        &[],
        &[("Card 1", "{{Front}}<br><br>{{type:nc:Back}}", "{{Front}}<hr id=answer>{{type:nc:Back}}<br>{{Back}}")],
        "",
    );
    d.add(DECK, &nc, &[
        "{{type:nc:...}} ignores accents: type \"cafe\" without the accent.",
        &format!("café{}", check("\"cafe\" is accepted as correct (nc = no combining characters).")),
    ]);
}

fn formatting(d: &mut Deck) {
    const DECK: &str = "05 Formatting";
    let basic = d.stock("Basic");
    d.add(DECK, &basic, &[
        "<b>Bold</b>, <i>italic</i>, <u>underline</u>, <s>strike</s>, H<sub>2</sub>O, x<sup>2</sup>, \
         <code>code</code>, <kbd>Ctrl</kbd>, <mark>highlight</mark>, <small>small</small>, \
         <span style=\"font-size:32px\">big</span>, <span style=\"color:#d32f2f\">red text</span>, \
         <span style=\"background-color:#fff59d\">yellow background</span>",
        &check("every style looks as named."),
    ]);
    d.add(DECK, &basic, &[
        "Lists and tables",
        &format!(
            "<ol style=\"text-align:left\"><li>First<ul><li>nested bullet</li><li>another</li></ul></li><li>Second</li></ol>\
             <table border=\"1\" cellpadding=\"6\" style=\"margin:auto;border-collapse:collapse\">\
             <tr><th>Header</th><th colspan=\"2\">Two columns</th></tr>\
             <tr><td rowspan=\"2\">Two rows</td><td>a</td><td style=\"text-align:right\">1</td></tr>\
             <tr><td>b</td><td style=\"text-align:right\">22</td></tr></table>{}",
            check("numbered list with a nested bullet list; table with borders, a header spanning two columns and a cell spanning two rows."),
        ),
    ]);
    d.add(DECK, &basic, &[
        "Blocks, headings and entities",
        &format!(
            "<h1>Heading 1</h1><h2>Heading 2</h2><h3>Heading 3</h3>\
             <blockquote style=\"border-left:4px solid #999;margin:8px;padding-left:10px;text-align:left\">A quotation</blockquote>\
             <pre style=\"text-align:left;background:#eee;padding:8px\">def f(x):\n    return x * 2</pre>\
             <hr><p>&amp; &lt;tag&gt; &copy; &mdash; &euro; a&nbsp;&nbsp;&nbsp;b</p>{}",
            check("three heading sizes, an indented quotation, a code block keeping its indentation, a line, and & <tag> © — € with wide spaces between a and b."),
        ),
    ]);
    let long: String = (1..=30)
        .map(|i| format!("<p>Paragraph {i} of 30. Scroll with the right stick or by dragging.</p>"))
        .collect();
    d.add(DECK, &basic, &[
        "A long card",
        &format!("{long}<p id=\"bottom\"><b>The end.</b></p>{}", check("the card scrolls; the answer side starts at the line under the question.")),
    ]);
    d.add(DECK, &basic, &[
        "Emoji and symbols: 😀 🎌 🐱 ✓ ✗ ★ ♪ → ⇒ ∞ ≠ ± ° € £ ¥",
        &check("every symbol is drawn (no empty boxes); colour emoji may show in one colour."),
    ]);
}

fn languages(d: &mut Deck) {
    const DECK: &str = "06 Languages";
    let basic = d.stock("Basic");
    let furigana = d.notetype(
        "Basic",
        "AnkiNX Test: furigana",
        &[],
        &[("Card 1", "<div style=\"font-size:36px\">{{furigana:Front}}</div>", "{{FrontSide}}<hr id=answer>{{Back}}")],
        "",
    );
    d.add(DECK, &furigana, &[
        "日本語[にほんご]を 勉強[べんきょう]する。",
        &format!("to study Japanese{}", check("readings (furigana) sit above their kanji.")),
    ]);
    d.add(DECK, &basic, &[
        "<span style=\"font-size:32px\"><ruby>汉<rt>hàn</rt></ruby><ruby>字<rt>zì</rt></ruby> · 你好 · 안녕하세요</span>",
        &check("Chinese with pinyin above, and Korean."),
    ]);
    d.add(DECK, &basic, &[
        "<div dir=\"rtl\" style=\"font-size:28px\">السلام عليكم</div><div dir=\"rtl\" style=\"font-size:28px\">\
         زبان‌های فارسی ۱۲۳</div><div dir=\"rtl\" style=\"font-size:28px\">שָׁלוֹם עוֹלָם</div>",
        &check("Arabic and Persian letters joined, all three lines read right to left; Persian digits ۱۲۳; Hebrew with vowel points."),
    ]);
    d.add(DECK, &basic, &[
        "Привет · Γειά σου · สวัสดี · नमस्ते · Tiếng Việt · e\u{301} (e + combining accent)",
        &check("Russian, Greek, Thai, Hindi, Vietnamese, and é built from two characters. Known gap: the Hindi conjunct स्त shows as स with a small stroke under it, then त (no shaping)."),
    ]);
}

fn media(d: &mut Deck) {
    const DECK: &str = "07 Media";
    let basic = d.stock("Basic");
    d.add(DECK, &basic, &[
        "PNG and JPEG<br><img src=\"ankinx_test_image.png\"> <img src=\"ankinx_test_photo.jpg\">",
        &check("a green PNG and a striped JPEG, each labelled with its format."),
    ]);
    d.add(DECK, &basic, &[
        "Animated GIF<br><img src=\"ankinx_test_anim.gif\">",
        &check("the GIF animates: the label counts frames 1/8 to 8/8 and the red dot moves right, over and over."),
    ]);
    d.add(DECK, &basic, &[
        "BMP and WebP<br><img src=\"ankinx_test_bitmap.bmp\"> <img src=\"ankinx_test_badge.webp\">",
        &check("a purple BMP and a yellow WebP."),
    ]);
    d.add(DECK, &basic, &[
        "SVG (with &lt;use&gt; and a gradient)<br><img src=\"ankinx_test_vector.svg\">",
        &check("a blue-to-purple rounded card with three dots and the word SVG."),
    ]);
    d.add(DECK, &basic, &[
        "A large image<br><img src=\"ankinx_test_large.png\">",
        &check("the 2400 × 1500 picture is as wide as the card (no sideways scrolling) and its label is complete; scroll down for the rest. Zooming (stick press) enlarges text but not a picture already as wide as the card, as in a browser."),
    ]);
    d.add(DECK, &basic, &[
        "Sized with width=\"80\": <img src=\"ankinx_test_image.png\" width=\"80\"> inline with text.",
        &check("a small image sitting in the line of text."),
    ]);
    d.add(DECK, &basic, &[
        "MP3 on the question [sound:ankinx_test_a4.mp3]",
        &check("a tone (A4) plays when the question appears; the play button or Y replays it."),
    ]);
    d.add(DECK, &basic, &[
        "OGG on the question [sound:ankinx_test_c5.ogg]",
        &format!("FLAC on the answer [sound:ankinx_test_g5.flac]{}", check("C5 on the question; on the answer G5 plays (and Y replays the question's first unless the deck option says otherwise).")),
    ]);
    d.add(DECK, &basic, &[
        "Opus [sound:ankinx_test_e5.opus] then WAV [sound:ankinx_test_c6.wav]",
        &check("two tones one after the other: E5, then C6."),
    ]);
    d.add(DECK, &basic, &[
        "A chord (MP3, 2 s) [sound:ankinx_test_chord.mp3]",
        &check("a C-major chord."),
    ]);
    d.add(DECK, &basic, &[
        "Silent question; the sound is on the answer.",
        &format!("[sound:ankinx_test_a4.mp3]{}", check("nothing plays until the answer shows.")),
    ]);
    d.add(DECK, &basic, &[
        "Missing files: <img src=\"ankinx_test_missing.png\"> [sound:ankinx_test_missing.mp3]",
        &check("no crash: a missing-media notice for the sound, and no picture."),
    ]);
}

fn maths(d: &mut Deck) {
    const DECK: &str = "08 Maths";
    let basic = d.stock("Basic");
    d.add(DECK, &basic, &[
        r"Solve \(ax^2 + bx + c = 0\).",
        &format!(r"\[x = \frac{{-b \pm \sqrt{{b^2-4ac}}}}{{2a}}\]{}", check("inline maths in the question; a centred display formula on the answer.")),
    ]);
    d.add(DECK, &basic, &[
        "Matrices and aligned equations",
        &format!(
            r"\[\begin{{pmatrix}} 1 & 2 \\ 3 & 4 \end{{pmatrix}} \begin{{pmatrix}} x \\ y \end{{pmatrix}} = \begin{{pmatrix}} 5 \\ 6 \end{{pmatrix}}\]\[\begin{{aligned}} 2x + y &= 7 \\ x - y &= -1 \end{{aligned}}\]{}",
            check("a matrix equation in round brackets, and two equations aligned on =."),
        ),
    ]);
    d.add(DECK, &basic, &[
        r"\(\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6}\), \(\int_0^1 x\,dx\), \(\lim_{x \to 0} \frac{\sin x}{x} = 1\), \(\sqrt[3]{27} = 3\), \(\alpha\beta\gamma\Omega\), \(\text{speed} = \frac{d}{t}\)",
        &check("sum, integral, limit, cube root, Greek letters, and upright text inside maths."),
    ]);
    d.add(DECK, &basic, &[
        r"Chemistry (mhchem): \(\ce{2H2 + O2 -> 2H2O}\), \(\ce{N2 + 3H2 <=>[Fe] 2NH3}\), \(\ce{SO4^2- + Ba^2+ -> BaSO4 v}\), \(\ce{CuSO4*5H2O}\), \(\pu{25 kJ/mol}\)",
        &check("formulas with subscripts and charges (2-, 2+), a plain arrow, a two-way arrow with Fe above it, a down arrow for the precipitate, a hydrate dot, then 25 kJ/mol upright."),
    ]);
    d.add(DECK, &basic, &[
        "LaTeX image: [latex]$\\displaystyle\\sum_{k=0}^{n} k = \\frac{n(n+1)}{2}$[/latex]",
        &check("an image of the formula, which Anki desktop makes only when LaTeX is installed on the computer. Without LaTeX no image exists, and the LaTeX source shows instead (on the desktop too)."),
    ]);
}

fn css(d: &mut Deck) {
    const DECK: &str = "09 Styling (CSS)";
    let demo = "<div class=\"font\">Card font from the media folder: 0O 1lI</div>\
                <div class=\"flex\"><div>flex 1</div><div>flex 2</div><div>flex 3</div></div>\
                <div class=\"grid\"><div>1</div><div>2</div><div>3</div><div>4</div><div>5</div><div>6</div></div>\
                <p class=\"gradient\">Gradient background, rounded corners</p>\
                <p class=\"shadow\">Box shadow</p>\
                <p><span class=\"rotate\">Rotated −8°</span></p>\
                <p class=\"vars\">CSS variable: purple bold</p>\
                <p><span class=\"badge\">Badge<span>3</span></span></p>\
                <p class=\"day-only\">Day mode: this line shows in light mode</p>\
                <p class=\"night-only\">Night mode: this line shows in dark mode</p>";
    let nt = d.notetype(
        "Basic",
        "AnkiNX Test: CSS",
        &[],
        &[
            (
                "Card 1",
                &format!("<div class=\"side\"><h3>{{{{Front}}}} (Card 1: green bar)</h3>{demo}</div>"),
                &format!("{{{{FrontSide}}}}<hr id=answer>{{{{Back}}}}"),
            ),
            (
                "Card 2",
                "<div class=\"side\"><h3>{{Front}} (Card 2: pink bar)</h3></div>",
                "{{FrontSide}}<hr id=answer>{{Back}}",
            ),
        ],
        r#"@font-face { font-family: "AnkiNX Test Font"; src: url("_ankinx_test_font.ttf"); }
.card { font-family: arial; font-size: 20px; text-align: center; color: #222; background-color: #fafafa; }
.card.nightMode { color: #eee; background-color: #222; }
.side { padding-left: 12px; }
.card1 .side { border-left: 8px solid #4caf50; }
.card2 .side { border-left: 8px solid #e91e63; }
.font { font-family: "AnkiNX Test Font", monospace; font-size: 24px; }
.flex { display: flex; justify-content: space-between; margin: 8px 0; }
.flex div { flex: 1; margin: 0 4px; padding: 8px; background: #bbdefb; color: #000; border-radius: 8px; }
.grid { display: grid; grid-template-columns: 1fr 1fr 1fr; grid-gap: 6px; margin: 8px 0; }
.grid div { background: #c8e6c9; color: #000; padding: 6px; }
.gradient { background: linear-gradient(90deg, #ff9800, #e91e63); color: #fff; padding: 10px; border-radius: 12px; }
.shadow { box-shadow: 0 4px 12px rgba(0, 0, 0, .45); padding: 10px; margin: 14px; }
.rotate { display: inline-block; transform: rotate(-8deg); }
.vars { --accent: #673ab7; color: var(--accent); font-weight: bold; }
.night-only { display: none; }
.nightMode .night-only { display: block; }
.nightMode .day-only { display: none; }
.badge { position: relative; padding-right: 14px; }
.badge span { position: absolute; top: -10px; right: -12px; background: #e53935; color: #fff; border-radius: 10px; padding: 1px 6px; font-size: 12px; }
"#,
    );
    d.add(DECK, &nt, &[
        "CSS",
        &check(
            "Card 1 has a green bar and Card 2 a pink one (.card1/.card2). Each bar is as tall as its side's \
             content, so Card 2's is a short one beside its heading. Card 1: the mono card font (0 and O \
             differ), three flex boxes in a row, a 3 × 2 grid, the gradient, a soft shadow under the box, a \
             word tilted up to the right, purple bold text, a red badge \"3\" at the top right of \"Badge\", \
             and the day or night line matching the theme. Known gap: the grid shows as one column.",
        ),
    ]);
}

fn javascript(d: &mut Deck) {
    const DECK: &str = "10 JavaScript";
    let basic = d.stock("Basic");
    d.add(DECK, &basic, &[
        "onclick: <button onclick=\"document.getElementById('clue').style.display='block'\">Show clue</button>\
         <div id=\"clue\" style=\"display:none\">It starts with T.</div>",
        &format!("Tokyo{}", check("tapping the button (or D-pad to it, then A) shows the clue without flipping the card.")),
    ]);
    d.add(DECK, &basic, &[
        "Random number: <b id=\"roll\"></b><script>window.ankinxRoll = 1 + Math.floor(Math.random() * 100); \
         document.getElementById('roll').textContent = window.ankinxRoll;</script>",
        &format!(
            "Same number on the answer: <b id=\"roll2\"></b><script>document.getElementById('roll2').textContent = \
             window.ankinxRoll;</script>{}",
            check("the answer repeats the question's number (globals are kept from question to answer)."),
        ),
    ]);
    d.add(DECK, &basic, &[
        "Countdown: <b id=\"count\">5</b><script>(function () { var n = 5; var t = setInterval(function () { n--; \
         document.getElementById('count').textContent = n > 0 ? n : 'Time is up'; if (n <= 0) clearInterval(t); }, \
         1000); })();</script>",
        &check("the number counts down once a second, then \"Time is up\"."),
    ]);
    d.add(DECK, &basic, &[
        "jQuery: <span class=\"jq\">one</span> <span class=\"jq\">two</span> <span id=\"jqv\"></span>\
         <script>$(function () { $('.jq').css('color', '#1e88e5').css('font-weight', 'bold'); \
         $('#jqv').text('(jQuery ' + $.fn.jquery + ')'); });</script>",
        &check("\"one\" and \"two\" are blue and bold as soon as the card shows (the script runs on load), followed by the jQuery version (3.7.1 in Anki)."),
    ]);
    d.add(DECK, &basic, &[
        "Tap the words to cross them out: <span class=\"tick\">apple</span> · <span class=\"tick\">banana</span> · \
         <span class=\"tick\">cherry</span><script>document.querySelectorAll('.tick').forEach(function (el) { \
         el.addEventListener('click', function () { el.style.textDecoration = \
         el.style.textDecoration ? '' : 'line-through'; }); });</script>",
        &check("each tap toggles the line through that word (addEventListener)."),
    ]);
    d.add(DECK, &basic, &[
        "<label><input type=\"checkbox\" id=\"cb\"> Tick me</label> <span id=\"cbs\">unticked</span>\
         <script>document.getElementById('cb').addEventListener('change', function (e) { \
         document.getElementById('cbs').textContent = e.target.checked ? 'ticked' : 'unticked'; });</script>",
        &check("tapping the box or its label toggles it and the word next to it."),
    ]);
    d.add(DECK, &basic, &[
        "Seen this session: <b id=\"seen\"></b><script>var k = 'ankinx-test-seen'; \
         var n = Number(localStorage.getItem(k) || 0) + 1; localStorage.setItem(k, n); \
         document.getElementById('seen').textContent = n + ' time(s)';</script>",
        &check("localStorage: the count goes up each time this card's question is shown in the same session."),
    ]);
    d.add(DECK, &basic, &[
        "Layout from scripts: <span id=\"box\" style=\"padding:4px;background:#ffe082\">measure me</span> \
         <span id=\"size\"></span><script>window.onload = function () { var r = \
         document.getElementById('box').getBoundingClientRect(); document.getElementById('size').textContent = \
         '(' + Math.round(r.width) + ' × ' + Math.round(r.height) + ' px)'; };</script>",
        &check("the size of the yellow box in pixels, not 0 × 0 (scripts can measure the card)."),
    ]);
    d.add(DECK, &basic, &[
        "Canvas: <canvas id=\"cv\" width=\"120\" height=\"60\"></canvas> <span id=\"cvs\"></span><script>var c = \
         document.getElementById('cv'); var g = c.getContext && c.getContext('2d'); if (g) { g.fillStyle = \
         '#43a047'; g.fillRect(0, 0, 120, 60); document.getElementById('cvs').textContent = '(drawn)'; } else \
         { document.getElementById('cvs').textContent = '(no canvas here)'; }</script>",
        &check("a green rectangle in Anki; on the console \"(no canvas here)\" (canvas is a known gap)."),
    ]);
    d.add(DECK, &basic, &[
        "Show the answer from a script: <button onclick=\"pycmd('ans')\">Reveal</button>",
        &format!(
            "<button onclick=\"pycmd('ease3')\">Answer Good</button>{}",
            check("Reveal flips the card (pycmd('ans')); on the answer, \"Answer Good\" grades it Good (pycmd('ease3'))."),
        ),
    ]);
    let files = d.notetype(
        "Basic",
        "AnkiNX Test: JavaScript files",
        &[],
        &[(
            "Card 1",
            "{{Front}}<p id=\"lib\"></p><p id=\"data\"></p>\
             <script src=\"_ankinx_test_lib.js\"></script>\
             <script>document.getElementById('lib').textContent = window.ankinxTestLib ? \
             ankinxTestLib.greet('script file') : 'script file not loaded'; \
             fetch(\"_ankinx_test_data.json\").then(function (r) { return r.json(); }).then(function (j) { \
             document.getElementById('data').textContent = 'JSON from the media folder: capital = ' + j.capital; \
             }).catch(function (e) { document.getElementById('data').textContent = 'fetch failed: ' + e; });</script>",
            "{{FrontSide}}<hr id=answer>{{Back}}",
        )],
        "",
    );
    d.add(DECK, &files, &[
        "Files from the media folder",
        &check("\"Hello, script file!\" (<script src>) and \"capital = Canberra\" (fetch of a JSON file)."),
    ]);
    let mcq = d.notetype(
        "Basic",
        "AnkiNX Test: multiple choice",
        &["Question", "option_1 (A)", "option_2 (B)", "option_3 (C)", "option_4 (D)", "option_5 (E)",
            "option_6 (F)", "option_7 (G)", "option_8 (H)", "Ans", "Explanation"],
        &[(
            "Card 1",
            include_str!("../../../app/tests/fixtures/mcq_front.html"),
            include_str!("../../../app/tests/fixtures/mcq_back.html"),
        )],
        include_str!("../../../app/tests/fixtures/mcq.css"),
    );
    d.add(DECK, &mcq, &[
        "Which of these is the capital of Australia?", "Sydney", "Canberra", "Melbourne", "Perth", "", "", "",
        "", "B",
        &format!(
            "Canberra was purpose-built as a compromise between Sydney and Melbourne.{}",
            check("a shared multiple-choice template (jQuery, anki-persistence): tapping an option ticks it and flips to the answer, which marks the right one; the gear opens its settings."),
        ),
    ]);
}

fn states(d: &mut Deck) {
    const DECK: &str = "11 Card states";
    let basic = d.stock("Basic");
    d.add(DECK, &basic, &["A new card", &check("while this card shows, the blue (new) number in the header is the underlined one.")]);
    let (_, cards) = d.add(DECK, &basic, &[
        "A learning card (answered Again when the deck was made)",
        &check("while this card shows, the red (learning) number is underlined; Again on it is due in a minute."),
    ]);
    d.answer(cards[0], Rating::Again);
    let (_, cards) = d.add(DECK, &basic, &[
        "A review card due today",
        &check("while this card shows, the green (due) number is underlined; its buttons show intervals in days."),
    ]);
    d.col.set_due_date(&cards, "0", None).unwrap();
    let (_, cards) = d.add(DECK, &basic, &[
        "A review card due in 30 days",
        &check("should NOT appear today (it is there only if the deck was imported without its learning progress); find it in the card browser (L on the deck overview)."),
    ]);
    d.col.set_due_date(&cards, "30", None).unwrap();
    let (_, cards) = d.add(DECK, &basic, &[
        "A suspended card",
        &check("should NOT appear; listed as suspended in the card browser."),
    ]);
    d.col.bury_or_suspend_cards(&cards, BuryOrSuspendMode::Suspend).unwrap();
    let (_, cards) = d.add(DECK, &basic, &[
        "A buried card",
        &check("should NOT appear until tomorrow (buried cards come back the next day)."),
    ]);
    d.col.bury_or_suspend_cards(&cards, BuryOrSuspendMode::BuryUser).unwrap();
    for (flag, colour) in ["Red", "Orange", "Green", "Blue", "Pink", "Turquoise", "Purple"].iter().enumerate() {
        let (_, cards) = d.add(DECK, &basic, &[
            &format!("Flag {}: {colour}", flag + 1),
            &check(&format!("the {colour} flag in the header and a {colour} bar along the card's top.")),
        ]);
        d.col.set_card_flag(&cards, flag as u32 + 1).unwrap();
    }
    let (nid, _) = d.add(DECK, &basic, &["A marked note", &check("★ in the header (the note has the \"marked\" tag).")]);
    d.col.add_tags_to_notes(&[nid], "marked").unwrap();
    let (nid, _) = d.add(DECK, &basic, &["A leech", &check("the note carries the \"leech\" tag. Anki makes a card a leech after 8 lapses (forgetting it as a review card, a deck option), not when a new or learning card is missed.")]);
    d.col.add_tags_to_notes(&[nid], "leech").unwrap();
}

/// The deck's description, and a preset that shows every new card today with
/// short learning steps (so the test is quick to walk through).
fn deck_options(d: &mut Deck) {
    let root = d.col.get_or_create_normal_deck(ROOT).unwrap().id;
    let mut deck = (*d.col.get_deck(root).unwrap().unwrap()).clone();
    if let anki::decks::DeckKind::Normal(normal) = &mut deck.kind {
        normal.description = "<b>AnkiNX test deck</b>: Anki's card types and template features, one subdeck \
             each. Every card ends with a grey <i>Check:</i> line saying what you should see. Study the whole \
             deck, or one subdeck at a time.<br>Made by tools/make_test_deck.sh."
            .into();
    }
    d.col.update_deck(&mut deck).unwrap();
    // Open with the subdecks showing.
    d.col.set_deck_collapsed(root, false, anki::decks::tree::DeckCollapseScope::Reviewer).unwrap();

    let info = d.col.get_deck_configs_for_update(root).unwrap();
    let mut config = d.col.get_deck_config(DeckConfigId(1), true).unwrap().unwrap();
    config.id = DeckConfigId(0);
    config.name = ROOT.into();
    config.inner.new_per_day = 9999;
    config.inner.reviews_per_day = 9999;
    config.inner.learn_steps = vec![1.0, 10.0];
    d.col
        .update_deck_configs(anki::deckconfig::UpdateDeckConfigsRequest {
            target_deck_id: root,
            configs: vec![config],
            removed_config_ids: vec![],
            mode: anki_proto::deck_config::UpdateDeckConfigsMode::ApplyToChildren,
            card_state_customizer: info.card_state_customizer.clone(),
            limits: info.current_deck.clone().unwrap_or_default().limits.unwrap_or_default(),
            new_cards_ignore_review_limit: info.new_cards_ignore_review_limit,
            apply_all_parent_limits: info.apply_all_parent_limits,
            fsrs: info.fsrs,
            fsrs_reschedule: false,
            fsrs_health_check: info.fsrs_health_check,
        })
        .unwrap();
}
