//! Update check against the project's GitHub releases, and the download of
//! a newer `.nro` in the background (the app swaps it in when it exits).

use std::io::Write;
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Duration;

use reqwest::Client;
use serde_json::json;
use serde_json::Value;

const LATEST_RELEASE: &str = "https://api.github.com/repos/lukasheber/AnkiNX/releases/latest";
const USER_AGENT: &str = concat!("AnkiNX/", env!("CARGO_PKG_VERSION"));
/// The release asset that is the app.
const ASSET: &str = "AnkiNX.nro";

/// Progress of a download, shared with the task writing it.
#[derive(Default)]
pub struct Download {
    pub received: u64,
    pub total: u64,
    pub done: bool,
    pub error: Option<String>,
}

pub type Progress = Arc<Mutex<Download>>;

/// "v0.10.1" > "0.9.3": numeric, per component.
pub fn is_newer(latest: &str, current: &str) -> bool {
    let parse = |v: &str| -> Vec<u64> {
        v.trim_start_matches('v')
            .split(['.', '-', '+'])
            .take(3)
            .map(|p| p.parse().unwrap_or(0))
            .collect()
    };
    parse(latest) > parse(current)
}

/// The latest release: version, whether it is newer than this build, the
/// `.nro` download, its size and the release notes.
pub async fn check(client: &Client) -> Result<Value, String> {
    let response = client
        .get(LATEST_RELEASE)
        .header("User-Agent", USER_AGENT)
        .header("Accept", "application/vnd.github+json")
        .timeout(Duration::from_secs(20))
        .send()
        .await
        .map_err(|e| e.to_string())?;
    if !response.status().is_success() {
        return Err(format!("GitHub: {}", response.status()));
    }
    let body = response.bytes().await.map_err(|e| e.to_string())?;
    let release: Value = serde_json::from_slice(&body).map_err(|e| e.to_string())?;
    let tag = release["tag_name"].as_str().unwrap_or_default();
    let asset = release["assets"]
        .as_array()
        .and_then(|assets| assets.iter().find(|a| a["name"] == ASSET));
    Ok(json!({
        "latest": tag.trim_start_matches('v'),
        "newer": is_newer(tag, env!("CARGO_PKG_VERSION")),
        "url": asset.and_then(|a| a["browser_download_url"].as_str()),
        "size": asset.and_then(|a| a["size"].as_u64()),
        "notes": release["body"].as_str().unwrap_or_default(),
        "page": release["html_url"].as_str().unwrap_or_default(),
    }))
}

/// Download `url` to `path`, through `path` + ".part" (so a cut-off download
/// is never taken for the app), updating `progress` as it goes.
pub async fn download(client: Client, url: String, path: PathBuf, progress: Progress) {
    let result = fetch(&client, &url, &path, &progress).await;
    let mut p = progress.lock().unwrap();
    p.done = true;
    p.error = result.err();
}

async fn fetch(client: &Client, url: &str, path: &PathBuf, progress: &Progress) -> Result<(), String> {
    let mut response = client
        .get(url)
        .header("User-Agent", USER_AGENT)
        .send()
        .await
        .and_then(|r| r.error_for_status())
        .map_err(|e| e.to_string())?;
    progress.lock().unwrap().total = response.content_length().unwrap_or(0);
    let mut part = path.clone().into_os_string();
    part.push(".part");
    let part = PathBuf::from(part);
    let io = |e: std::io::Error| format!("{}: {e}", part.display());
    {
        let mut file = std::fs::File::create(&part).map_err(io)?;
        let mut first = true;
        while let Some(chunk) = response.chunk().await.map_err(|e| e.to_string())? {
            // An NRO has "NRO0" at offset 0x10: refuse anything else (an
            // error page, a wrong asset) before writing much.
            if first && (chunk.len() < 0x14 || &chunk[0x10..0x14] != b"NRO0") {
                drop(file);
                let _ = std::fs::remove_file(&part);
                return Err("the download is not an NRO".into());
            }
            first = false;
            file.write_all(&chunk).map_err(io)?;
            progress.lock().unwrap().received += chunk.len() as u64;
        }
        file.sync_all().map_err(io)?;
    }
    let _ = std::fs::remove_file(path); // Horizon's rename won't replace
    std::fs::rename(&part, path).map_err(io)
}

#[cfg(test)]
mod test {
    use super::is_newer;

    #[test]
    fn versions() {
        assert!(is_newer("v0.4.0", "0.3.0"));
        assert!(is_newer("v0.10.0", "0.9.9"));
        assert!(is_newer("0.3.1", "0.3.0"));
        assert!(!is_newer("v0.3.0", "0.3.0"));
        assert!(!is_newer("v0.2.9", "0.3.0"));
    }
}
