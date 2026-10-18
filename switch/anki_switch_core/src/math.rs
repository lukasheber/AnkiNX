//! Card maths without MathJax.
//!
//! Anki cards write maths as `\(...\)` (inline) and `\[...\]` (display), and
//! Anki's reviewer typesets them with MathJax (JavaScript). Here each formula
//! is converted to Typst with MiTeX, laid out by Typst, and written as an SVG
//! into a cache folder; the card HTML gets an `<img>` in its place, sized in
//! `em` so it follows the surrounding text, and lowered by its depth (negative
//! bottom margin) so it sits on the text baseline. The app draws SVGs already
//! (lunasvg).

use std::collections::hash_map::DefaultHasher;
use std::hash::Hash;
use std::hash::Hasher;
use std::path::PathBuf;

use typst::diag::FileError;
use typst::diag::FileResult;
use typst::foundations::Bytes;
use typst::foundations::Datetime;
use typst::foundations::Duration;
use typst::layout::Abs;
use typst::layout::Frame;
use typst::layout::FrameItem;
use typst::syntax::FileId;
use typst::syntax::RootedPath;
use typst::syntax::Source;
use typst::syntax::VirtualPath;
use typst::syntax::VirtualRoot;
use typst::text::Font;
use typst::text::FontBook;
use typst::utils::LazyHash;
use typst::Library;
use typst::LibraryExt;
use typst::World;
use typst_layout::PagedDocument;

/// Bump when the output changes, to invalidate cached SVGs.
const RENDER_VERSION: u32 = 3;
/// Typst text size for rendering. Large, so the SVG's intrinsic size is big
/// enough for a sharp raster; the HTML scales it back with `em` sizes.
const FONT_PT: f64 = 40.0;
/// Space around the formula so glyph ink isn't clipped. (Lines are sized by
/// glyph bounds, not Typst's default cap-height-to-baseline, for the same
/// reason: tall inline formulas were cropped.)
const MARGIN_PT: f64 = 2.0;

static FONTS: [&[u8]; 2] = [
    include_bytes!("../assets/fonts/NewCMMath-Regular.otf"),
    include_bytes!("../assets/fonts/NewCM10-Regular.otf"),
];
static MITEX_FILES: [(&str, &str); 3] = [
    ("mitex/mod.typ", include_str!("../assets/mitex/mod.typ")),
    ("mitex/prelude.typ", include_str!("../assets/mitex/prelude.typ")),
    ("mitex/latex/standard.typ", include_str!("../assets/mitex/latex/standard.typ")),
];

fn file_id(path: &str) -> FileId {
    FileId::new(RootedPath::new(VirtualRoot::Project, VirtualPath::new(path).expect("valid path")))
}

/// The Typst "world": the fonts, MiTeX's definitions and one main file.
struct MathWorld {
    library: LazyHash<Library>,
    book: LazyHash<FontBook>,
    fonts: Vec<Font>,
    main: Source,
}

impl World for MathWorld {
    fn library(&self) -> &LazyHash<Library> {
        &self.library
    }
    fn book(&self) -> &LazyHash<FontBook> {
        &self.book
    }
    fn main(&self) -> FileId {
        self.main.id()
    }
    fn source(&self, id: FileId) -> FileResult<Source> {
        if id == self.main.id() {
            return Ok(self.main.clone());
        }
        MITEX_FILES
            .iter()
            .find(|(path, _)| id == file_id(path))
            .map(|(_, text)| Source::new(id, text.to_string()))
            .ok_or(FileError::AccessDenied)
    }
    fn file(&self, _id: FileId) -> FileResult<Bytes> {
        Err(FileError::AccessDenied)
    }
    fn font(&self, index: usize) -> Option<Font> {
        self.fonts.get(index).cloned()
    }
    fn today(&self, _offset: Option<Duration>) -> Option<Datetime> {
        None
    }
}

/// A rendered formula, sizes in `em` of the surrounding text.
struct Rendered {
    file: PathBuf,
    width: f64,
    height: f64,
    /// How far the image extends below the text baseline.
    depth: f64,
}

pub struct MathRenderer {
    world: MathWorld,
    cache: PathBuf,
}

impl MathRenderer {
    pub fn new(cache: PathBuf) -> Self {
        let fonts: Vec<Font> = FONTS.iter().flat_map(|data| Font::iter(Bytes::new(*data))).collect();
        let book = FontBook::from_fonts(&fonts);
        MathRenderer {
            world: MathWorld {
                library: LazyHash::new(Library::default()),
                book: LazyHash::new(book),
                fonts,
                main: Source::detached(""),
            },
            cache,
        }
    }

    /// Replace `\(...\)` and `\[...\]` in a card side with images. Formulas
    /// that fail to convert are left as they are, as MathJax shows its input.
    /// `color` is the text colour, e.g. "#000000".
    pub fn render_html(&mut self, html: &str, color: &str) -> String {
        if !html.contains("\\(") && !html.contains("\\[") {
            return html.to_string();
        }
        let mut out = String::with_capacity(html.len());
        let mut rest = html;
        while let Some((start, open)) = next_open(rest) {
            let close = if open == "\\(" { "\\)" } else { "\\]" };
            let body_start = start + open.len();
            let Some(len) = rest[body_start..].find(close) else {
                break;
            };
            let latex = latex_text(&rest[body_start..body_start + len]);
            let display = open == "\\[";
            out.push_str(&rest[..start]);
            match self.render(&latex, display, color) {
                Some(r) => out.push_str(&image_html(&r, display, &latex)),
                None => out.push_str(&rest[start..body_start + len + close.len()]),
            }
            rest = &rest[body_start + len + close.len()..];
        }
        out.push_str(rest);
        out
    }

    fn render(&mut self, latex: &str, display: bool, color: &str) -> Option<Rendered> {
        let mut hasher = DefaultHasher::new();
        (RENDER_VERSION, latex, display, color).hash(&mut hasher);
        let key = format!("{:016x}", hasher.finish());
        let file = self.cache.join(format!("{key}.svg"));
        let meta = self.cache.join(format!("{key}.size"));
        // Cached: "width height depth" in em.
        if let Ok(text) = std::fs::read_to_string(&meta) {
            let n: Vec<f64> = text.split_whitespace().filter_map(|v| v.parse().ok()).collect();
            if n.len() == 3 && file.exists() {
                return Some(Rendered { file, width: n[0], height: n[1], depth: n[2] });
            }
        }

        let code = mitex::convert_math(latex, None).ok()?;
        self.world.main = Source::new(
            file_id("main.typ"),
            format!(
                "#import \"mitex/mod.typ\": mitex-scope\n\
                 #set page(width: auto, height: auto, margin: {MARGIN_PT}pt, fill: none)\n\
                 #set text(size: {FONT_PT}pt, fill: rgb(\"{color}\"), top-edge: \"bounds\", bottom-edge: \"bounds\")\n\
                 #math.equation(block: {display}, eval(\"$\" + {code:?} + \"$\", scope: mitex-scope))\n"
            ),
        );
        let document = typst::compile::<PagedDocument>(&self.world).output.ok()?;
        // Keep Typst's memoization cache from growing across many cards.
        comemo::evict(10);
        let page = document.pages().first()?;
        let size = page.frame.size();
        let baseline = equation_baseline(&page.frame).unwrap_or(size.y - Abs::pt(MARGIN_PT));
        let svg = typst_svg::svg(page, &Default::default());

        let em = |abs: Abs| abs.to_pt() / FONT_PT;
        let rendered = Rendered {
            file,
            width: em(size.x),
            height: em(size.y),
            depth: em(size.y - baseline),
        };
        std::fs::create_dir_all(&self.cache).ok()?;
        std::fs::write(&rendered.file, svg).ok()?;
        std::fs::write(
            &meta,
            format!("{} {} {}", rendered.width, rendered.height, rendered.depth),
        )
        .ok()?;
        Some(rendered)
    }
}

/// The baseline of the equation on the page: the largest group that has its
/// own baseline (the equation's frame), in page coordinates.
fn equation_baseline(frame: &Frame) -> Option<Abs> {
    fn walk(frame: &Frame, y: Abs, best: &mut Option<(f64, Abs)>) {
        for (pos, item) in frame.items() {
            if let FrameItem::Group(group) = item {
                let f = &group.frame;
                if f.has_baseline() {
                    let area = f.size().x.to_pt() * f.size().y.to_pt();
                    if best.map_or(true, |(a, _)| area > a) {
                        *best = Some((area, y + pos.y + f.baseline()));
                    }
                }
                walk(f, y + pos.y, best);
            }
        }
    }
    let mut best = None;
    walk(frame, Abs::zero(), &mut best);
    best.map(|(_, baseline)| baseline)
}

/// The next `\(` or `\[`: (position, delimiter).
fn next_open(html: &str) -> Option<(usize, &'static str)> {
    let inline = html.find("\\(").map(|i| (i, "\\("));
    let display = html.find("\\[").map(|i| (i, "\\["));
    match (inline, display) {
        (Some(a), Some(b)) => Some(if a.0 < b.0 { a } else { b }),
        (a, b) => a.or(b),
    }
}

/// The TeX source as MathJax reads it from the card's HTML: tags dropped
/// (line breaks become spaces) and entities decoded.
fn latex_text(html: &str) -> String {
    let mut text = String::with_capacity(html.len());
    let mut in_tag = false;
    for c in html.chars() {
        match c {
            '<' => in_tag = true,
            '>' if in_tag => {
                in_tag = false;
                text.push(' ');
            }
            _ if !in_tag => text.push(c),
            _ => {}
        }
    }
    text.replace("&nbsp;", " ")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&#39;", "'")
        .replace("&amp;", "&")
}

fn image_html(r: &Rendered, display: bool, latex: &str) -> String {
    let alt = latex.replace('&', "&amp;").replace('"', "&quot;").replace('<', "&lt;");
    let img = format!(
        // The image's bottom margin edge sits on the baseline: a negative
        // bottom margin of the formula's depth lowers it into place.
        // (position: relative would be clipped to the unshifted box.)
        "<img class=\"ankinx-math\" src=\"{}\" alt=\"{alt}\" \
         style=\"width: {:.3}em; height: {:.3}em; margin-bottom: -{:.3}em\">",
        r.file.display(),
        r.width,
        r.height,
        r.depth
    );
    if display {
        format!("<div class=\"ankinx-math-display\" style=\"text-align: center; margin: 0.5em 0\">{img}</div>")
    } else {
        img
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn renders_inline_and_display_maths() {
        let dir = tempfile::tempdir().unwrap();
        let mut renderer = MathRenderer::new(dir.path().to_path_buf());
        let html = r"Solve \(x^2 &lt; 4\) and \[\frac{a}{b}\] then \(x";
        let out = renderer.render_html(html, "#000000");
        assert_eq!(out.matches("class=\"ankinx-math\"").count(), 2, "{out}");
        assert!(out.contains("ankinx-math-display"), "{out}");
        assert!(out.starts_with("Solve <img"), "{out}");
        // An unclosed delimiter is left as text, as MathJax does.
        assert!(out.ends_with(r" then \(x"), "{out}");
        let svgs = std::fs::read_dir(dir.path())
            .unwrap()
            .filter(|e| e.as_ref().unwrap().path().extension().unwrap() == "svg")
            .count();
        assert_eq!(svgs, 2);
        // Second render comes from the cache, identical.
        assert_eq!(renderer.render_html(html, "#000000"), out);
        // Inline maths sits on the baseline: some depth, less than its height.
        let style = out.split("margin-bottom: -").nth(1).unwrap();
        let depth: f64 = style.split("em").next().unwrap().parse().unwrap();
        assert!(depth > 0.0 && depth < 1.0, "{depth}");
    }

    /// Inline formulas taller than a text line (an integral with limits) must
    /// not be cut to the line's height: the line is sized by glyph bounds.
    #[test]
    fn tall_inline_maths_is_not_cropped() {
        let dir = tempfile::tempdir().unwrap();
        let mut renderer = MathRenderer::new(dir.path().to_path_buf());
        let tall = renderer.render(r"\int_0^\infty e^{-x^2}dx", false, "#000000").unwrap();
        let flat = renderer.render(r"a + b", false, "#000000").unwrap();
        assert!(tall.height > flat.height * 1.4, "tall {} vs flat {}", tall.height, flat.height);
        assert!(tall.depth > 0.2, "limits reach below the baseline: {}", tall.depth);
    }

    #[test]
    fn latex_text_strips_html() {
        assert_eq!(latex_text("a &lt; b<br>c&amp;d"), "a < b c&d");
    }
}
