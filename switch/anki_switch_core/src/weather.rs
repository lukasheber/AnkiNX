//! Live weather for Memory Forest (src/forest.rs): MET Norway's forecast
//! (free, no account, CC BY 4.0) for a city found with Photon (OpenStreetMap
//! data, free, no account). A port of the add-on's weather.py, at the commit
//! pinned in app/library/PINS; names follow its.
//!
//! The forecast is cached in a JSON file, refreshed from the network by
//! `refresh` (called by the app in the background, never holding the API's
//! lock) and read by `current`, which the forest is grown with. MET's
//! forecast has no sunrise or past rain, so those are worked out here.

use std::path::Path;
use std::sync::Mutex;
use std::time::Duration;

use chrono::DateTime;
use chrono::Timelike;
use chrono::Utc;
use reqwest::Client;
use serde_json::json;
use serde_json::Map;
use serde_json::Value;

// Places only, a few of them (Photon files some big cities as districts).
const GEOCODE_URL: &str = "https://photon.komoot.io/api/?limit=6&lang=en&osm_tag=place&q=";
const TOWNS: [&str; 5] = ["city", "town", "village", "hamlet", "municipality"];
const REGIONS: [&str; 5] = ["state", "province", "county", "region", "country"];
const FORECAST_URL: &str = "https://api.met.no/weatherapi/locationforecast/2.0/compact";
// MET and Photon both ask to be told who is calling, and how to reach them.
const USER_AGENT: &str =
    concat!("AnkiNX/", env!("CARGO_PKG_VERSION"), " (Memory Forest live weather; https://github.com/lukasheber/AnkiNX)");
const CACHE_VERSION: i64 = 2;
// How long a forecast is kept when MET's reply has no Expires (which rules when it has one).
const MAX_AGE_SECS: f64 = 15.0 * 60.0;
// After a failure: with no weather to show, try again soon, then less often.
const RETRY_STEPS_SECS: [f64; 4] = [60.0, 120.0, 300.0, 600.0];
const RETRY_WHILE_SHOWING_SECS: f64 = 600.0;
const RETRY_PAUSE: Duration = Duration::from_secs(2);
// Past this the cache is not weather any more: the preset's own is more honest.
const STALE_SECS: f64 = 6.0 * 3600.0;
// Beaufort 6, "strong breeze".
const WINDY_KMH: f64 = 39.0;
// Clear or cloudy counts as "after the rain" when more than this fell in the last hours.
const RECENT_RAIN_HOURS: i64 = 2;
const RECENT_RAIN_MM: f64 = 0.1;
// Hourly rain and sky kept, either side of now.
const KEEP_HOURS: i64 = 24;
const HTTP_TIMEOUT: Duration = Duration::from_secs(15);
const GEOCODE_TIMEOUT: Duration = Duration::from_secs(30);
const MAX_ERROR_CHARS: usize = 200;
// Degrees: the sun's top edge on the horizon, through the air.
const SUN_ALTITUDE: f64 = -0.833;

// The city Photon did not know: not asked again until the text changes (kept
// out of the file, so a restart asks once more).
static NOT_FOUND: Mutex<Option<String>> = Mutex::new(None);

fn utc(now: f64) -> DateTime<Utc> {
    DateTime::from_timestamp(now as i64, 0).unwrap_or_default()
}

fn hour_key(t: DateTime<Utc>) -> String {
    t.format("%Y-%m-%dT%H:00:00Z").to_string()
}

/// A MET symbol code ("lightrainshowers_day") as the forest's weather.
fn map_symbol(symbol: &str) -> &'static str {
    let s = symbol.split('_').next().unwrap_or("");
    if s.contains("thunder") {
        "storm"
    } else if s.contains("snow") {
        "snow"
    } else if s.contains("rain") || s.contains("sleet") || s.contains("drizzle") {
        "rain"
    } else if s == "fog" {
        "fog"
    } else if s == "clearsky" || s == "fair" {
        "clear"
    } else {
        "cloudy"
    }
}

/// ({UTC hour: mm of rain in it}, {UTC hour: its symbol, temperature and wind})
/// from a MET compact forecast.
fn parse_forecast(js: &Value) -> (Map<String, Value>, Map<String, Value>) {
    let (mut rain, mut hours) = (Map::new(), Map::new());
    for e in js["properties"]["timeseries"].as_array().into_iter().flatten() {
        let Some(time) = e["time"].as_str() else { continue };
        let data = &e["data"];
        let next = if data["next_1_hours"].is_object() { &data["next_1_hours"] } else { &data["next_6_hours"] };
        let details = &data["instant"]["details"];
        let wind = details["wind_speed"].as_f64().unwrap_or(0.0) * 3.6; // m/s → km/h
        hours.insert(
            time.into(),
            json!({ "symbol": next["summary"]["symbol_code"].as_str().unwrap_or("cloudy"),
                    "temp": details["air_temperature"],
                    "wind": (wind * 10.0).round() / 10.0 }),
        );
        if let Some(mm) = data["next_1_hours"]["details"]["precipitation_amount"].as_f64() {
            rain.insert(time.into(), json!(mm));
        }
    }
    (rain, hours)
}

/// The sky at `now`: the forecast's hour now, or its first when it starts later.
fn sky_at(hours: &Map<String, Value>, now: f64) -> Option<Value> {
    let stamp = hour_key(utc(now));
    let mut times: Vec<&String> = hours.keys().collect();
    times.sort();
    let key = times.iter().rev().find(|h| ***h <= stamp).or(times.first())?;
    let hour = &hours[*key];
    let symbol = hour["symbol"].as_str().unwrap_or("cloudy");
    let weather = map_symbol(symbol);
    let wind = hour["wind"].as_f64().unwrap_or(0.0);
    Some(json!({ "sky": weather, "symbol": symbol, "temp": hour["temp"], "wind": wind,
                 "windy": wind >= WINDY_KMH && weather != "fog" }))
}

/// Sunrise and sunset (UTC, RFC 3339) on the place's solar day around `now`,
/// from the sunrise equation (NOAA's, to about a minute); `polar` "day" or
/// "night" when the sun does not cross the horizon that day.
pub fn sun_times(lat: f64, lon: f64, now: f64) -> Value {
    let day = (utc(now) + chrono::Duration::seconds((lon / 15.0 * 3600.0) as i64)).date_naive();
    let n = (day - chrono::NaiveDate::from_ymd_opt(2000, 1, 1).unwrap()).num_days() as f64;
    let j_star = n - lon / 360.0;
    let m = (357.5291 + 0.98560028 * j_star).rem_euclid(360.0);
    let rm = m.to_radians();
    let c = 1.9148 * rm.sin() + 0.02 * (2.0 * rm).sin() + 0.0003 * (3.0 * rm).sin();
    let ecl = ((m + c + 180.0 + 102.9372).rem_euclid(360.0)).to_radians();
    let transit = 2451545.0 + j_star + 0.0053 * rm.sin() - 0.0069 * (2.0 * ecl).sin();
    let sin_d = ecl.sin() * 23.4397f64.to_radians().sin();
    let cos_d = sin_d.asin().cos();
    let phi = lat.to_radians();
    let cos_w = (SUN_ALTITUDE.to_radians().sin() - phi.sin() * sin_d) / (phi.cos() * cos_d);
    if cos_w < -1.0 {
        return json!({ "sunrise": null, "sunset": null, "polar": "day" });
    }
    if cos_w > 1.0 {
        return json!({ "sunrise": null, "sunset": null, "polar": "night" });
    }
    let half = cos_w.acos().to_degrees() / 360.0;
    let at = |jd: f64| utc((jd - 2440587.5) * 86400.0).with_second(0).unwrap_or_default().to_rfc3339();
    json!({ "sunrise": at(transit - half), "sunset": at(transit + half), "polar": null })
}

fn instant(v: &Value) -> Option<DateTime<Utc>> {
    DateTime::parse_from_rfc3339(v.as_str()?).ok().map(|t| t.with_timezone(&Utc))
}

fn is_day(sun: &Value, now: f64) -> bool {
    if let Some(polar) = sun["polar"].as_str() {
        return polar == "day";
    }
    let t = utc(now);
    matches!((instant(&sun["sunrise"]), instant(&sun["sunset"])), (Some(r), Some(s)) if r <= t && t < s)
}

/// More than a trace fell in the last whole hours before this one.
fn rained_recently(rain: &Map<String, Value>, now: f64) -> bool {
    let hour = utc(now).with_minute(0).and_then(|t| t.with_second(0)).unwrap_or_default();
    let mm: f64 = (1..=RECENT_RAIN_HOURS)
        .map(|k| rain.get(&hour_key(hour - chrono::Duration::hours(k))).and_then(Value::as_f64).unwrap_or(0.0))
        .sum();
    mm > RECENT_RAIN_MM
}

/// The weather as the forest uses it at `now`: the sky, the sun's times for
/// the place, and "after the rain" when it has just stopped in daylight.
fn resolve(sky: &Value, rain: &Map<String, Value>, place: &Value, now: f64) -> Value {
    let sun = sun_times(place["lat"].as_f64().unwrap_or(0.0), place["lon"].as_f64().unwrap_or(0.0), now);
    let day = is_day(&sun, now);
    let mut weather = sky["sky"].as_str().unwrap_or("cloudy").to_string();
    if (weather == "clear" || weather == "cloudy") && day && rained_recently(rain, now) {
        weather = "after_rain".into();
    }
    let mut out = sky.as_object().cloned().unwrap_or_default();
    out.extend(sun.as_object().cloned().unwrap_or_default());
    out.insert("weather".into(), json!(weather));
    out.insert("is_day".into(), json!(day));
    Value::Object(out)
}

/// New hours over old ones, kept within KEEP_HOURS of now.
fn merge_hours(old: &Value, new: &Map<String, Value>, now: f64) -> Map<String, Value> {
    let t = utc(now);
    let (lo, hi) = (hour_key(t - chrono::Duration::hours(KEEP_HOURS)), hour_key(t + chrono::Duration::hours(KEEP_HOURS)));
    let mut all = old.as_object().cloned().unwrap_or_default();
    all.extend(new.clone());
    all.into_iter().filter(|(h, _)| *h >= lo && *h <= hi).collect()
}

fn load(path: &Path) -> Value {
    std::fs::read(path)
        .ok()
        .and_then(|b| serde_json::from_slice::<Value>(&b).ok())
        .filter(|v| v["v"].as_i64() == Some(CACHE_VERSION))
        .unwrap_or_else(|| json!({}))
}

fn save(path: &Path, state: &Value) {
    // (whole or not at all: the forest may be reading it meanwhile)
    let tmp = path.with_extension("tmp");
    if std::fs::write(&tmp, serde_json::to_vec_pretty(state).unwrap()).is_ok() {
        let _ = std::fs::rename(&tmp, path);
    }
}

fn ours(state: &Value, city: &str) -> bool {
    state["city"].as_str() == Some(city.trim().to_lowercase().as_str())
}

/// The last known weather for `city` in the cache at `path`, if recent
/// enough to use: its weather, temperature, wind and the sun's times.
pub fn current(path: &Path, city: &str, now: f64) -> Option<Value> {
    let state = load(path);
    current_in(&state, city, now)
}

fn current_in(state: &Value, city: &str, now: f64) -> Option<Value> {
    if !ours(state, city) || !state["weather"].is_object() {
        return None;
    }
    if now - state["fetched_at"].as_f64().unwrap_or(0.0) > STALE_SECS {
        return None;
    }
    let empty = Map::new();
    let hours = state["hours"].as_object().unwrap_or(&empty);
    let sky = sky_at(hours, now).unwrap_or_else(|| state["weather"].clone());
    Some(resolve(&sky, state["rain"].as_object().unwrap_or(&empty), &state["place"], now))
}

/// The place the cache has for `city`: its name, latitude and longitude.
pub fn place(path: &Path, city: &str) -> Option<Value> {
    let state = load(path);
    ours(&state, city).then(|| state["place"].clone()).filter(Value::is_object)
}

fn needs_refresh(state: &Value, city: &str, now: f64) -> bool {
    if !ours(state, city) {
        return true;
    }
    if NOT_FOUND.lock().unwrap().as_deref() == Some(city.trim()) {
        return false;
    }
    let expires = state["expires"].as_f64().unwrap_or(0.0);
    if now < expires {
        return false; // MET asks never to come back before its Expires
    }
    if let Some(failed) = state["failed_at"].as_f64() {
        let fails = state["fails"].as_u64().unwrap_or(1).max(1) as usize;
        let wait = if current_in(state, city, now).is_some() {
            RETRY_WHILE_SHOWING_SECS
        } else {
            RETRY_STEPS_SECS[fails.min(RETRY_STEPS_SECS.len()) - 1]
        };
        return now - failed >= wait;
    }
    expires > 0.0 || now - state["fetched_at"].as_f64().unwrap_or(0.0) > MAX_AGE_SECS
}

struct Reply {
    status: u16,
    body: Option<Value>,
    expires: f64,
    modified: Option<String>,
}

async fn get(client: &Client, url: &str, query: &[(&str, String)], modified: Option<&str>, timeout: Duration) -> Result<Reply, String> {
    let fetch = || async {
        let mut req = client.get(url).query(query).header("User-Agent", USER_AGENT).timeout(timeout);
        if let Some(m) = modified {
            req = req.header("If-Modified-Since", m);
        }
        req.send().await
    };
    // a dropped connection or a slow server is often gone a moment later: one more try
    let response = match fetch().await {
        Ok(r) if r.status().is_server_error() => {
            std::thread::sleep(RETRY_PAUSE); // (this call's own thread: the app waits on nothing)
            fetch().await
        }
        Ok(r) => Ok(r),
        Err(e) if e.is_timeout() || e.is_connect() || e.is_request() => {
            std::thread::sleep(RETRY_PAUSE); // (this call's own thread: the app waits on nothing)
            fetch().await
        }
        Err(e) => Err(e),
    }
    .map_err(|e| e.to_string())?;
    let status = response.status().as_u16();
    let header = |name: &str| response.headers().get(name).and_then(|v| v.to_str().ok()).map(str::to_string);
    let expires = header("Expires")
        .and_then(|e| DateTime::parse_from_rfc2822(&e).ok())
        .map_or(0.0, |t| t.timestamp() as f64);
    let modified = header("Last-Modified");
    if status == 304 {
        return Ok(Reply { status, body: None, expires, modified });
    }
    if !(200..300).contains(&status) {
        return Err(format!("{url}: HTTP {status}"));
    }
    let bytes = response.bytes().await.map_err(|e| e.to_string())?;
    let body: Value = serde_json::from_slice(&bytes).map_err(|e| e.to_string())?;
    Ok(Reply { status, body: Some(body), expires, modified })
}

/// Photon's town (or region) by that name.
async fn geocode(client: &Client, city: &str) -> Result<Option<Value>, String> {
    let js = get(client, &format!("{GEOCODE_URL}{}", urlencode(city.trim())), &[], None, GEOCODE_TIMEOUT)
        .await?
        .body
        .unwrap_or_default();
    let osm = |f: &Value| f["properties"]["osm_value"].as_str().unwrap_or("").to_string();
    let feats: Vec<&Value> = js["features"]
        .as_array()
        .into_iter()
        .flatten()
        .filter(|f| TOWNS.contains(&osm(f).as_str()) || REGIONS.contains(&osm(f).as_str()))
        .collect();
    let Some(&first) = feats.first() else { return Ok(None) };
    let mut best = first;
    if REGIONS.contains(&osm(first).as_str()) {
        // a city named after its state ranks under it: the city is the one meant
        let key = |f: &Value| (f["properties"]["name"].clone(), f["properties"]["countrycode"].clone());
        if let Some(town) = feats[1..].iter().find(|f| TOWNS.contains(&osm(f).as_str()) && key(f) == key(first)) {
            best = town;
        }
    }
    let props = &best["properties"];
    let coords = &best["geometry"]["coordinates"];
    Ok(Some(json!({ "name": props["name"].as_str().unwrap_or(city), "lat": coords[1], "lon": coords[0],
                    "country": props["countrycode"] })))
}

fn urlencode(s: &str) -> String {
    s.bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => (b as char).to_string(),
            _ => format!("%{b:02X}"),
        })
        .collect()
}

/// Fetch the weather for `city` into the cache at `path` if it is due (MET's
/// Expires, or after a failure, a while); never fails. Returns the weather
/// now, or why there is none.
pub async fn refresh(path: &Path, city: &str, client: &Client) -> Value {
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0.0, |d| d.as_secs_f64());
    let prev = load(path);
    if city.trim().is_empty() || !needs_refresh(&prev, city, now) {
        return json!({ "weather": current_in(&prev, city, now), "error": prev["error"] });
    }
    let prev = if ours(&prev, city) { prev } else { json!({}) };
    let key = city.trim().to_lowercase();
    let mut place = prev["place"].clone();
    let result: Result<Value, String> = async {
        if !place.is_object() {
            match geocode(client, city).await? {
                Some(p) => place = p,
                None => {
                    *NOT_FOUND.lock().unwrap() = Some(city.trim().to_string());
                    return Err(format!("city not found: {city}"));
                }
            }
        }
        let modified = prev["modified"].as_str().filter(|_| prev["hours"].is_object());
        let query = [
            ("lat", format!("{:.4}", place["lat"].as_f64().unwrap_or(0.0))),
            ("lon", format!("{:.4}", place["lon"].as_f64().unwrap_or(0.0))),
        ];
        let reply = get(client, FORECAST_URL, &query, modified, HTTP_TIMEOUT).await?;
        let (rain, hours) = match (&reply.body, reply.status) {
            (Some(body), _) => parse_forecast(body),
            (None, _) => (
                prev["rain"].as_object().cloned().unwrap_or_default(),
                prev["hours"].as_object().cloned().unwrap_or_default(),
            ),
        };
        let rain = merge_hours(&prev["rain"], &rain, now);
        let hours = merge_hours(&prev["hours"], &hours, now);
        let sky = sky_at(&hours, now).ok_or("the forecast has no hours")?;
        Ok(json!({ "v": CACHE_VERSION, "city": key, "place": place, "fetched_at": now, "expires": reply.expires,
                   "modified": reply.modified.or_else(|| prev["modified"].as_str().map(str::to_string)),
                   "weather": sky, "rain": rain, "hours": hours }))
    }
    .await;
    let state = match result {
        Ok(state) => state,
        Err(error) => {
            // the place is kept, though the forecast failed: Photon is the slow one to ask again
            let mut s = if prev.is_object() && ours(&prev, city) { prev.clone() } else { json!({ "v": CACHE_VERSION, "city": key }) };
            if place.is_object() {
                s["place"] = place;
            }
            s["failed_at"] = json!(now);
            s["fails"] = json!(prev["fails"].as_u64().unwrap_or(0) + 1);
            s["error"] = json!(error.chars().take(MAX_ERROR_CHARS).collect::<String>());
            s
        }
    };
    save(path, &state);
    json!({ "weather": current_in(&state, city, now), "error": state["error"] })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn symbols() {
        assert_eq!(map_symbol("lightrainshowers_day"), "rain");
        assert_eq!(map_symbol("heavysnowandthunder"), "storm");
        assert_eq!(map_symbol("partlycloudy_night"), "cloudy");
        assert_eq!(map_symbol("fair_day"), "clear");
    }

    #[test]
    fn sun_over_oslo() {
        // 2026-06-21, noon UTC: a long day, sunrise before 3 and sunset after 20 (UTC)
        let sun = sun_times(59.91, 10.75, 1_782_043_200.0);
        let rise = instant(&sun["sunrise"]).unwrap();
        let set = instant(&sun["sunset"]).unwrap();
        assert!(rise.hour() <= 2 && set.hour() >= 20, "{sun}");
        // midsummer at 78°N: the sun never sets
        assert_eq!(sun_times(78.2, 15.6, 1_782_043_200.0)["polar"], "day");
    }
}
