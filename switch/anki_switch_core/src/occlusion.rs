//! Image Occlusion cards without JavaScript.
//!
//! Anki renders each occlusion as an empty `<div class="cloze|cloze-inactive|
//! cloze-highlight" data-shape=... data-left=...>` and a script draws them on
//! a canvas over the image (ts/routes/image-occlusion/review.ts). Here the
//! same divs are read into shapes for the app, which draws them over the image
//! natively, and the image is marked with `#ankinx-io` so the card view knows
//! which one they belong to.

use std::sync::LazyLock;

use regex::Regex;
use serde_json::json;
use serde_json::Value;

/// URL fragment that marks the occluded image for the card view.
pub const IMAGE_MARKER: &str = "#ankinx-io";

static SHAPE_DIV: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"<div class="(cloze|cloze-inactive|cloze-highlight)"([^>]*\bdata-shape="[^"]*"[^>]*)></div>"#)
        .unwrap()
});
static DATA_ATTR: LazyLock<Regex> = LazyLock::new(|| Regex::new(r#"data-([A-Za-z-]+)="([^"]*)""#).unwrap());
/// The image inside Anki's occlusion container (the {{Image}} field).
static CONTAINER_IMG: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"(?s)(<div id="image-occlusion-container">.*?<img\b[^>]*?\bsrc=")([^"]*)(")"#).unwrap()
});
static CANVAS: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r#"<canvas id="image-occlusion-canvas"></canvas>"#).unwrap());
/// Answer side "Toggle masks" button: the app maps it to a controller button.
static TOGGLE_BUTTON: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r#"<div><button id="toggle">[^<]*</button></div>"#).unwrap());

pub struct Occlusions {
    pub html: String,
    pub shapes: Vec<Value>,
    /// The answer side's masks can be hidden: it has Anki's toggle button, or
    /// inactive shapes are drawn ("occlude inactive").
    pub can_toggle: bool,
}

/// Find occlusion shapes in a rendered card side. `None` for other cards.
pub fn extract(html: &str) -> Option<Occlusions> {
    if !html.contains("image-occlusion-container") || !SHAPE_DIV.is_match(html) {
        return None;
    }
    let mut shapes = Vec::new();
    let mut can_toggle = false;
    for div in SHAPE_DIV.captures_iter(html) {
        let state = match &div[1] {
            "cloze" => "active",
            "cloze-inactive" => "inactive",
            _ => "highlight",
        };
        if let Some(shape) = shape_json(state, &div[2]) {
            can_toggle |= shape["occlude_inactive"] == true;
            shapes.push(shape);
        }
    }
    let html = CONTAINER_IMG.replace(html, |c: &regex::Captures| {
        format!("{}{}{IMAGE_MARKER}{}", &c[1], &c[2], &c[3])
    });
    let html = CANVAS.replace(&html, "");
    // Anki's back template always has a "Toggle Masks" button (the app's X):
    // in "hide one, guess one" it hides the highlighted shape.
    can_toggle |= TOGGLE_BUTTON.is_match(&html);
    let html = TOGGLE_BUTTON.replace(&html, "").into_owned();
    Some(Occlusions { html, shapes, can_toggle })
}

/// One shape, as review.ts's extractShapeFromRenderedCloze/buildShape read it.
/// Coordinates stay normalized (0-1 of the image size); angle in degrees.
fn shape_json(state: &str, attrs: &str) -> Option<Value> {
    let mut data = std::collections::HashMap::new();
    for attr in DATA_ATTR.captures_iter(attrs) {
        data.insert(attr[1].to_ascii_lowercase(), decode_attribute(&attr[2]));
    }
    let kind = data.get("shape")?.clone();
    if !matches!(kind.as_str(), "rect" | "ellipse" | "polygon" | "text") {
        return None;
    }
    let num = |name: &str| data.get(name).and_then(|v| v.parse::<f64>().ok());
    // Stored as steps of 1/10000 of a turn (shapes/lib.ts, storedToAngle).
    let angle = num("angle").map(|steps| (steps % 10000.0) / 10000.0 * 360.0).unwrap_or(0.0);
    let points: Vec<[f64; 2]> = data
        .get("points")
        .map(|p| {
            p.split(' ')
                .filter_map(|pair| {
                    let (x, y) = pair.split_once(',')?;
                    Some([x.parse().ok()?, y.parse().ok()?])
                })
                .collect()
        })
        .unwrap_or_default();
    Some(json!({
        "state": state,
        "shape": kind,
        "occlude_inactive": data.get("occludeinactive").map(String::as_str) == Some("1"),
        "left": num("left").unwrap_or(0.0),
        "top": num("top").unwrap_or(0.0),
        "width": num("width").unwrap_or(0.0),
        "height": num("height").unwrap_or(0.0),
        "rx": num("rx").unwrap_or(0.0),
        "ry": num("ry").unwrap_or(0.0),
        "angle": angle,
        "points": points,
        "text": data.get("text").cloned().unwrap_or_default(),
        "scale": num("scale").unwrap_or(1.0),
        // Fraction of the image height; 0 means Anki's default (40px).
        "font_size": num("font-size").unwrap_or(0.0),
        "fill": data.get("fill").cloned().unwrap_or_default(),
    }))
}

fn decode_attribute(value: &str) -> String {
    value
        .replace("&quot;", "\"")
        .replace("&#x27;", "'")
        .replace("&#39;", "'")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&amp;", "&")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_rendered_occlusions() {
        let html = r#"<div style="display: none"><div class="cloze" data-ordinal="1" data-shape="rect" data-left=".1" data-top=".2" data-width=".3" data-height=".4" data-occludeInactive="1" ></div><div class="cloze-inactive" data-ordinal="2" data-shape="polygon" data-left=".5" data-top=".5" data-points=".5,.5 .6,.7 .4,.7" ></div><div class="cloze" data-ordinal="0" data-shape="text" data-left=".1" data-top=".1" data-text="a &quot;b&quot;" data-scale="2" data-font-size=".05" data-angle="2500" ></div></div>
<div id="err"></div>
<div id="image-occlusion-container">
    <img src="pic.png">
    <canvas id="image-occlusion-canvas"></canvas>
</div>"#;
        let out = extract(html).unwrap();
        assert_eq!(out.shapes.len(), 3);
        assert!(out.can_toggle);
        assert!(out.html.contains(r#"<img src="pic.png#ankinx-io">"#), "{}", out.html);
        assert!(!out.html.contains("<canvas"));
        let rect = &out.shapes[0];
        assert_eq!(rect["state"], "active");
        assert_eq!(rect["width"], 0.3);
        let poly = &out.shapes[1];
        assert_eq!(poly["state"], "inactive");
        assert_eq!(poly["points"][2], json!([0.4, 0.7]));
        let text = &out.shapes[2];
        assert_eq!(text["text"], "a \"b\"");
        assert_eq!(text["angle"], 90.0);
        assert_eq!(text["font_size"], 0.05);
        assert!(extract("<span class=cloze>[...]</span>").is_none());
    }

    #[test]
    fn hide_one_answers_can_toggle() {
        // "Hide one, guess one": no inactive masks, but the back template's
        // toggle button hides the highlighted one.
        let shape = r#"<div style="display: none"><div class="cloze-highlight" data-ordinal="1" data-shape="rect" data-left=".1" data-top=".2" data-width=".3" data-height=".4" ></div></div>
<div id="image-occlusion-container">
    <img src="pic.png">
    <canvas id="image-occlusion-canvas"></canvas>
</div>"#;
        assert!(!extract(shape).unwrap().can_toggle);
        let back = format!("{shape}\n<div><button id=\"toggle\">Toggle Masks</button></div>");
        let out = extract(&back).unwrap();
        assert!(out.can_toggle);
        assert!(!out.html.contains("toggle"), "{}", out.html);
    }
}
