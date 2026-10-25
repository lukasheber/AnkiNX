//! Fetch Memory Forest's live weather for a city (src/weather.rs), as the
//! app's `forest_weather` call does, and print it.
//! Usage: cargo run --example weather_probe -- <city> [cache.json]

use anki_switch_core::weather;

fn main() {
    let mut args = std::env::args().skip(1);
    let city = args.next().expect("usage: weather_probe <city> [cache.json]");
    let path = std::path::PathBuf::from(args.next().unwrap_or_else(|| "/tmp/forest-weather.json".into()));
    let runtime = tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap();
    let client = reqwest::Client::new();
    let out = runtime.block_on(weather::refresh(&path, &city, &client));
    println!("{}", serde_json::to_string_pretty(&out).unwrap());
    println!("place: {:?}", weather::place(&path, &city));
}
