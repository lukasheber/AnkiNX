//! Memory Forest's page data for a collection (src/forest.rs), with the
//! moment it was worked out for, as JSON on stdout: for comparing with the
//! add-on's own Python (app/forest/reference.py) and for timing it.
//! Usage: cargo run --example forest_dump -- <collection.anki2> [nature]
//! (with nothing remembered, as on the add-on's first look)

use anki::collection::CollectionBuilder;
use anki_switch_core::forest;

fn main() {
    let mut args = std::env::args().skip(1);
    let path = args.next().expect("usage: forest_dump <collection.anki2> [nature]");
    let opts = forest::Options { nature: args.next().unwrap_or_else(|| "peaceful".into()), settled: true, week_start: 1 };
    let mut memory = serde_json::json!({});
    let mut col = CollectionBuilder::new(path).build().unwrap();
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
    let out = serde_json::json!({ "at": { "cutoff": at.cutoff, "today": at.today, "now": at.now }, "page": page });
    println!("{out}");
}
