//! Create a small sample collection for UI testing (desktop preview).
//! Usage: cargo run --example make_sample -- <data dir>
//! Writes <dir>/collection.anki2; media files are added by the caller.

use anki::collection::CollectionBuilder;

fn main() {
    let dir = std::path::PathBuf::from(std::env::args().nth(1).expect("usage: make_sample <dir>"));
    std::fs::create_dir_all(dir.join("collection.media")).unwrap();
    let mut col = CollectionBuilder::new(dir.join("collection.anki2"))
        .set_media_paths(dir.join("collection.media"), dir.join("collection.mdb"))
        .build()
        .unwrap();

    let basic = col.get_notetype_by_name("Basic").unwrap().unwrap();
    let typing = col.get_notetype_by_name("Basic (type in the answer)").unwrap().unwrap();
    let cloze = col.get_notetype_by_name("Cloze").unwrap().unwrap();
    // A note type using {{hint:...}} (normally revealed by a JavaScript onclick).
    let mut hinted = (*basic).clone();
    hinted.id = anki::prelude::NotetypeId(0);
    hinted.name = "Basic (with hint)".into();
    hinted.templates = vec![anki::notetype::CardTemplate::new(
        "Card 1",
        "{{Front}}<br><br>{{hint:Back}}",
        "{{FrontSide}}<hr id=answer>{{Back}}",
    )];
    col.add_notetype(&mut hinted, false).unwrap();
    // A user's multiple-choice note type: jQuery, anki-persistence, inline
    // SVG and a data: URI checkbox tick (app/tests/fixtures).
    let mut mcq = (*basic).clone();
    mcq.id = anki::prelude::NotetypeId(0);
    mcq.name = "MCQ".into();
    mcq.fields = ["Question", "option_1 (A)", "option_2 (B)", "option_3 (C)", "option_4 (D)", "option_5 (E)",
        "option_6 (F)", "option_7 (G)", "option_8 (H)", "Ans", "Explanation"]
        .into_iter()
        .map(anki::notetype::NoteField::new)
        .collect();
    mcq.templates = vec![anki::notetype::CardTemplate::new(
        "Card 1",
        include_str!("../../../app/tests/fixtures/mcq_front.html"),
        include_str!("../../../app/tests/fixtures/mcq_back.html"),
    )];
    mcq.config.css = include_str!("../../../app/tests/fixtures/mcq.css").into();
    col.add_notetype(&mut mcq, false).unwrap();
    // Furigana: Anki's {{furigana:...}} filter (rslib writes <ruby><rb>).
    let mut furigana = (*basic).clone();
    furigana.id = anki::prelude::NotetypeId(0);
    furigana.name = "Japanese (furigana)".into();
    furigana.templates = vec![anki::notetype::CardTemplate::new(
        "Card 1",
        "<div style=\"font-size: 40px\">{{furigana:Front}}</div>",
        "{{FrontSide}}<hr id=answer>{{Back}}",
    )];
    col.add_notetype(&mut furigana, false).unwrap();
    // A shared deck's back template whose <div is never closed with ">": the
    // field's replay link turns into attributes of the div (Anki plays it
    // when the div is clicked, and so must we).
    let mut broken = (*typing).clone();
    broken.id = anki::prelude::NotetypeId(0);
    broken.name = "Typing (unclosed div)".into();
    broken.templates = vec![anki::notetype::CardTemplate::new(
        "Card 1",
        "{{Front}}\n<br>\n{{type:Back}}",
        "{{FrontSide}}\n<hr id=answer>\n<hr id=answer>\n\n<div \nfont-size: 4px;\ncolor: white;\n{{Back}}\n</div>",
    )];
    col.add_notetype(&mut broken, false).unwrap();
    // ANKINX_SAMPLE_ONLY=<deck>: only that deck, for screenshots of one card type.
    let only = std::env::var("ANKINX_SAMPLE_ONLY").ok();
    let wanted = |deck: &str| only.as_deref().map_or(true, |o| deck.starts_with(o));
    let mut add = |deck: &str, notetype: &anki::prelude::Notetype, fields: &[&str]| {
        if !wanted(deck) {
            return;
        }
        let deck = col.get_or_create_normal_deck(deck).unwrap();
        let mut note = notetype.new_note();
        for (i, f) in fields.iter().enumerate() {
            note.set_field(i, *f).unwrap();
        }
        col.add_note(&mut note, deck.id).unwrap();
    };

    add(
        "Japanese::Vocabulary",
        &basic,
        &["<b>猫</b> [sound:neko.mp3]", "ねこ — <i>cat</i>"],
    );
    add("Japanese::Vocabulary", &basic, &["犬", "いぬ — dog"]);
    add("Japanese::Kanji", &basic, &["<span style=\"font-size:60px\">学</span>", "learn, study"]);
    add(
        "Japanese::Furigana",
        &furigana,
        &[
            "日本語[にほんご]を 勉強[べんきょう]する。<ruby>漢字<rp>(</rp><rt>かんじ</rt><rp>)</rp></ruby>も。",
            "to study Japanese; kanji too",
        ],
    );
    add(
        "Geography",
        &basic,
        &[
            "What is the capital of <i>France</i>?",
            "<b>Paris</b><br><img src=\"paris.png\">",
        ],
    );
    add(
        "Geography",
        &basic,
        &[
            "Name three of Europe's longest rivers",
            "<ul><li>Volga</li><li>Danube</li><li>Ural</li></ul>",
        ],
    );
    add(
        "Biology",
        &cloze,
        &["The {{c1::mitochondrion}} is the powerhouse of the cell.", ""],
    );
    // Media formats beyond PNG/JPEG (files written by app/preview.sh).
    add(
        "Media",
        &basic,
        &[
            "Which equation is this?<br><img src=\"formula.svg\">",
            "Mass–energy equivalence<br><img src=\"badge.webp\">",
        ],
    );
    add("Media", &hinted, &["Largest ocean on Earth?", "Pacific"]);
    // Maths with MathJax delimiters, inline and display.
    add(
        "Maths",
        &basic,
        &[
            r"Solve \(ax^2 + bx + c = 0\) for \(x\).",
            r"\[x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}\]and \(\int_0^\infty e^{-x^2}\,dx = \frac{\sqrt{\pi}}{2}\)",
        ],
    );
    // Template JavaScript: an onclick button, and a script filling a value
    // that must survive from question to answer (one page per card in Anki).
    add(
        "Scripts",
        &basic,
        &[
            "Capital of Japan? <button onclick=\"document.getElementById('h').style.display='block'\">Show clue</button>\
             <div id=\"h\" style=\"display:none\">It starts with T</div>\
             <script>window.pick = 1 + Math.floor(Math.random() * 6);</script>",
            "Tokyo. You rolled <b id=\"r\"></b>.<script>document.getElementById('r').textContent = window.pick;</script>",
        ],
    );
    add(
        "Scripts::Multiple choice",
        &mcq,
        &["Which of these is the capital of Australia?", "Sydney", "Canberra", "Melbourne", "Perth", "", "",
            "", "", "B", "Canberra was purpose-built as a compromise between Sydney and Melbourne."],
    );
    // Card fonts from the media folder via @font-face (app/preview.sh copies
    // the font files); the Japanese falls back to the system font.
    add(
        "Fonts",
        &basic,
        &[
            "<style>@font-face { font-family: 'Card Mono'; src: url('_cardmono.woff2') format('woff2'), \
             url(\"_cardmono.ttf\"); } @font-face { font-family: 'Card Mono'; font-weight: bold; \
             src: url(_cardmono-bold.ttf); }</style>\
             <div style=\"font-family: 'Card Mono', monospace\">A monospaced card font, <b>its bold face</b>, \
             and 日本語 from the system font.</div>",
            "Loaded from the media folder",
        ],
    );
    // Right-to-left text: Persian (with ZWNJ, digits and an English word) with
    // and without dir="rtl", and Arabic.
    add(
        "RTL",
        &basic,
        &[
            "<div style=\"text-align: left\">کتاب English ۱۲۳</div><hr>\
             <div dir=\"rtl\" style=\"text-align: left\">زبان فارسی یکی از زبان‌های هندواروپایی است و در ایران، \
             افغانستان و تاجیکستان به آن سخن می‌گویند (۱۱۰ میلیون نفر).</div><hr>\
             <div dir=\"rtl\">السلام عليكم</div>",
            "Persian / Arabic",
        ],
    );
    // {{type:Back}}: an answer box on the question, a comparison on the answer.
    add("Typing", &typing, &["あ (romaji)", "a"]);
    add("Typing::Unclosed div", &broken, &["え", "e[sound:4_e.mp3]"]);

    // Image Occlusion: three masks (rectangle, ellipse, polygon) over a
    // generated picture; "oi=1" hides the other masks on the question side.
    if wanted("Occlusion") {
        let image = dir.join("io-sample.bmp");
        std::fs::write(&image, sample_bmp(400, 250)).unwrap();
        let deck = col.get_or_create_normal_deck("Occlusion").unwrap();
        col.set_current_deck(deck.id).unwrap();
        col.add_image_occlusion_note(anki_proto::image_occlusion::AddImageOcclusionNoteRequest {
            image_path: image.to_string_lossy().into(),
            occlusions: "{{c1::image-occlusion:rect:left=.08:top=.12:width=.3:height=.25:oi=1}}\
                         {{c2::image-occlusion:ellipse:left=.55:top=.1:rx=.15:ry=.18:oi=1}}\
                         {{c3::image-occlusion:polygon:left=.35:top=.55:points=.35,.95 .5,.55 .65,.95:oi=1}}"
                .into(),
            header: "Name the shapes".into(),
            back_extra: "Rectangle, circle, triangle".into(),
            tags: vec![],
            notetype_id: 0,
        })
        .unwrap();
        std::fs::remove_file(&image).unwrap();
    }

    // A deck description (HTML), for the deck overview.
    if wanted("Geography") {
        let mut deck = col.get_or_create_normal_deck("Geography").unwrap();
        if let anki::decks::DeckKind::Normal(normal) = &mut deck.kind {
            normal.description = "<b>Capitals</b> and rivers of Europe.<br>From a school atlas.".into();
        }
        col.update_deck(&mut deck).unwrap();
    }

    // ANKINX_SAMPLE_TIMING=1: the default preset shows the answer timer and
    // auto-advances (2 s question, 3 s answer, then Good), for previews.
    if std::env::var("ANKINX_SAMPLE_TIMING").is_ok() {
        let default_deck = anki::prelude::DeckId(1);
        let info = col.get_deck_configs_for_update(default_deck).unwrap();
        let mut config = col.get_deck_config(anki::prelude::DeckConfigId(1), true).unwrap().unwrap();
        config.inner.show_timer = true;
        config.inner.seconds_to_show_question = 2.0;
        config.inner.seconds_to_show_answer = 3.0;
        config.inner.answer_action = anki_proto::deck_config::deck_config::config::AnswerAction::AnswerGood as i32;
        col.update_deck_configs(anki::deckconfig::UpdateDeckConfigsRequest {
            target_deck_id: default_deck,
            configs: vec![config],
            removed_config_ids: vec![],
            mode: anki_proto::deck_config::UpdateDeckConfigsMode::Normal,
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

    // Show the nested decks expanded.
    for name in ["Japanese"].into_iter().filter(|n| wanted(n)) {
        let deck = col.get_or_create_normal_deck(name).unwrap();
        col.set_deck_collapsed(deck.id, false, anki::decks::tree::DeckCollapseScope::Reviewer)
            .unwrap();
    }
    col.close(None).unwrap();
}

/// A 24-bit BMP: sky over grass, with a red block, a blue disc and a green
/// triangle where the masks go.
fn sample_bmp(width: u32, height: u32) -> Vec<u8> {
    let row = (width * 3).div_ceil(4) * 4;
    let size = 54 + row * height;
    let mut out = Vec::with_capacity(size as usize);
    out.extend_from_slice(b"BM");
    for v in [size, 0, 54, 40] {
        out.extend_from_slice(&v.to_le_bytes());
    }
    out.extend_from_slice(&(width as i32).to_le_bytes());
    out.extend_from_slice(&(height as i32).to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes());
    out.extend_from_slice(&24u16.to_le_bytes());
    for v in [0u32, row * height, 2835, 2835, 0, 0] {
        out.extend_from_slice(&v.to_le_bytes());
    }
    for y in (0..height).rev() {
        let fy = y as f32 / height as f32;
        let mut line = Vec::with_capacity(row as usize);
        for x in 0..width {
            let fx = x as f32 / width as f32;
            let (r, g, b) = if (0.08..0.38).contains(&fx) && (0.12..0.37).contains(&fy) {
                (200, 40, 40)
            } else if ((fx - 0.7) / 0.15).powi(2) + ((fy - 0.28) / 0.18).powi(2) < 1.0 {
                (40, 70, 200)
            } else if fy > 0.55 && (fx - 0.5).abs() < (fy - 0.55) / 0.4 * 0.15 {
                (40, 160, 60)
            } else if fy > 0.8 {
                (120, 180, 90)
            } else {
                (210, 230, 250)
            };
            line.extend_from_slice(&[b, g, r]);
        }
        line.resize(row as usize, 0);
        out.extend_from_slice(&line);
    }
    out
}
