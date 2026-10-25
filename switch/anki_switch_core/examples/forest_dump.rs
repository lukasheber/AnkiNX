//! Memory Forest's page data for a collection (src/forest.rs), with the
//! moment it was worked out for, as JSON on stdout: for comparing with the
//! add-on's own Python (app/forest/reference.py) and for timing it.
//! Usage: cargo run --example forest_dump -- <collection.anki2> [nature]
//!            [--exclude <deck name>] [--start YYYY-MM-DD] [--keep] [--no-suspended]
//! (with nothing remembered, as on the add-on's first look)

use anki::collection::CollectionBuilder;
use anki_switch_core::forest;

fn main() {
    let mut args = std::env::args().skip(1);
    let path = args.next().expect("usage: forest_dump <collection.anki2> [nature] [--exclude deck] [--start date] [--keep]");
    let mut opts = forest::Options::default();
    let mut exclude = Vec::new();
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--exclude" => exclude.push(args.next().expect("a deck name")),
            "--start" => opts.ignore_before = args.next(),
            "--keep" => opts.keep_earlier_trees = true,
            "--no-suspended" => opts.keep_suspended = false,
            nature => opts.nature = nature.into(),
        }
    }
    let mut col = CollectionBuilder::new(path).build().unwrap();
    for name in &exclude {
        let id = col.get_deck_id(name).unwrap().unwrap_or_else(|| panic!("no deck {name}"));
        opts.excluded.push(id.0);
    }
    let mut memory = serde_json::json!({});
    let at = forest::now(&mut col).unwrap();
    let look = forest::Look {
        environment: "natural".into(),
        landscape: "lake".into(),
        landmark: "none".into(),
        weather: "clear".into(),
        time: "golden_hour".into(),
        real_sky: false,
        live: None,
        city: None,
    };
    let started = std::time::Instant::now();
    let page = forest::page(&mut col, at, &look, &opts, &mut memory).unwrap();
    eprintln!("forest in {} ms", started.elapsed().as_millis());
    let out = serde_json::json!({ "at": { "cutoff": at.cutoff, "today": at.today, "now": at.now },
                                  "excluded": opts.excluded, "page": page });
    println!("{out}");
}
