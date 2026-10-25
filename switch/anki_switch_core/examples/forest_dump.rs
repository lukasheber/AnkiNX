//! Memory Forest's page data for a collection (src/forest.rs), with the
//! moment it was worked out for, as JSON on stdout: for comparing with the
//! add-on's own Python (app/forest/reference.py) and for timing it.
//! Usage: cargo run --example forest_dump -- <collection.anki2>

use anki::collection::CollectionBuilder;
use anki_switch_core::forest;

fn main() {
    let path = std::env::args().nth(1).expect("usage: forest_dump <collection.anki2>");
    let mut col = CollectionBuilder::new(path).build().unwrap();
    let at = forest::now(&mut col).unwrap();
    let look = forest::Look {
        environment: "natural".into(),
        landscape: "lake".into(),
        landmark: "none".into(),
        weather: "clear".into(),
        time: "golden_hour".into(),
    };
    let started = std::time::Instant::now();
    let page = forest::page(&mut col, at, &look).unwrap();
    eprintln!("forest in {} ms", started.elapsed().as_millis());
    let out = serde_json::json!({ "at": { "cutoff": at.cutoff, "today": at.today, "now": at.now }, "page": page });
    println!("{out}");
}
