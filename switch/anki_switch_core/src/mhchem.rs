//! `\ce{...}` and `\pu{...}` (MathJax's mhchem extension, which Anki's MathJax
//! loads) turned into plain LaTeX before MiTeX, which has no mhchem. Covers
//! what chemistry cards commonly use: formulas with counts and charges,
//! coefficients, states, reaction arrows (with conditions above), hydrate
//! dots and gas/precipitate symbols. Units in `\pu` are set upright.

use std::borrow::Cow;

/// `latex` with every `\ce{...}` and `\pu{...}` expanded.
pub fn expand(latex: &str) -> Cow<'_, str> {
    if !latex.contains("\\ce") && !latex.contains("\\pu") {
        return Cow::Borrowed(latex);
    }
    let mut out = String::with_capacity(latex.len() * 2);
    let mut rest = latex;
    while let Some(at) = find_command(rest) {
        let (name, after) = (&rest[at + 1..at + 3], &rest[at + 3..]);
        let Some((arg, tail)) = braced(after.trim_start()) else {
            break;
        };
        out.push_str(&rest[..at]);
        out.push('{');
        out.push_str(&if name == "ce" { formula(arg) } else { units(arg) });
        out.push('}');
        rest = tail;
    }
    out.push_str(rest);
    Cow::Owned(out)
}

/// The next `\ce` or `\pu` that is a whole command name (not `\cedilla`).
fn find_command(s: &str) -> Option<usize> {
    let mut from = 0;
    while let Some(i) = s[from..].find('\\').map(|i| i + from) {
        let name = &s[i + 1..];
        if (name.starts_with("ce") || name.starts_with("pu"))
            && !name[2..].starts_with(|c: char| c.is_ascii_alphabetic())
        {
            return Some(i);
        }
        from = i + 1;
    }
    None
}

/// `{...}` at the start of `s` (nested braces kept): its content and what follows.
fn braced(s: &str) -> Option<(&str, &str)> {
    if !s.starts_with('{') {
        return None;
    }
    let mut depth = 0;
    for (i, c) in s.char_indices() {
        match c {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    return Some((&s[1..i], &s[i + 1..]));
                }
            }
            _ => {}
        }
    }
    None
}

fn units(s: &str) -> String {
    let mut out = String::new();
    for (i, part) in s.split_whitespace().enumerate() {
        if i > 0 {
            out.push_str("\\ ");
        }
        if part.parse::<f64>().is_ok() || part.chars().all(|c| c.is_ascii_digit() || ".,-+e".contains(c)) {
            out.push_str(part);
        } else {
            out.push_str(&format!("\\mathrm{{{}}}", part.replace('.', "\\cdot ")));
        }
    }
    out
}

/// A charge after a formula: `^{2-}`, `^2-`, `^-`, or a bare `+`/`-` stuck to it.
fn charge(chars: &[char], i: &mut usize, out: &mut String) -> bool {
    if chars.get(*i) == Some(&'^') {
        *i += 1;
        if chars.get(*i) == Some(&'{') {
            let start = *i + 1;
            let end = (start..chars.len()).find(|&k| chars[k] == '}').unwrap_or(chars.len());
            out.push_str(&format!("^{{{}}}", chars[start..end].iter().collect::<String>()));
            *i = (end + 1).min(chars.len());
        } else {
            let start = *i;
            while *i < chars.len() && (chars[*i].is_ascii_digit() || chars[*i] == '+' || chars[*i] == '-') {
                *i += 1;
            }
            out.push_str(&format!("^{{{}}}", chars[start..*i].iter().collect::<String>()));
        }
        return true;
    }
    // "Na+", "OH-", "e-": a sign stuck to the formula, then a space or the end.
    if matches!(chars.get(*i), Some('+') | Some('-'))
        && chars.get(*i + 1).is_none_or(|c| c.is_whitespace() || *c == '}' || *c == ')')
    {
        out.push_str(&format!("^{{{}}}", chars[*i]));
        *i += 1;
        return true;
    }
    false
}

fn formula(s: &str) -> String {
    let chars: Vec<char> = s.trim().chars().collect();
    let mut out = String::new();
    let mut i = 0;
    // At the start of a term (coefficient position) rather than after a symbol.
    let mut term_start = true;
    while i < chars.len() {
        let c = chars[i];
        let rest: String = chars[i..].iter().collect();
        // Arrows, with an optional [condition] above.
        if let Some((arrow, len)) = [("<=>", "\\rightleftharpoons"), ("<->", "\\leftrightarrow"),
            ("->", "\\rightarrow"), ("<-", "\\leftarrow")]
            .iter()
            .find(|(a, _)| rest.starts_with(a))
            .map(|(a, l)| (*l, a.len()))
        {
            i += len;
            if !out.is_empty() && !out.ends_with(' ') {
                out.push(' ');
            }
            if chars.get(i) == Some(&'[') {
                let end = (i..chars.len()).find(|&k| chars[k] == ']').unwrap_or(chars.len());
                let above: String = chars[i + 1..end].iter().collect();
                out.push_str(&format!("\\overset{{\\text{{{above}}}}}{{{arrow}}} "));
                i = (end + 1).min(chars.len());
            } else {
                out.push_str(&format!("{arrow} "));
            }
            term_start = true;
            continue;
        }
        if c.is_whitespace() {
            // Gas and precipitate: " ^" and " v" standing alone.
            let next_is_alone = |k: usize| chars.get(k + 1).is_none_or(|n| n.is_whitespace());
            if chars.get(i + 1) == Some(&'^') && next_is_alone(i + 1) {
                out.push_str("\\uparrow");
                i += 2;
                continue;
            }
            if chars.get(i + 1) == Some(&'v') && next_is_alone(i + 1) {
                out.push_str("\\downarrow");
                i += 2;
                continue;
            }
            if !out.ends_with(' ') {
                out.push(' ');
            }
            term_start = true;
            i += 1;
            continue;
        }
        if c == '+' && term_start {
            out.push_str("+ ");
            i += 1;
            continue;
        }
        if c == '*' || (c == '.' && !term_start) {
            out.push_str("\\cdot ");
            term_start = true;
            i += 1;
            continue;
        }
        if c.is_ascii_digit() && term_start {
            // A coefficient (2, 1/2, 0.5).
            while i < chars.len() && (chars[i].is_ascii_digit() || chars[i] == '/' || chars[i] == '.') {
                out.push(chars[i]);
                i += 1;
            }
            term_start = false;
            continue;
        }
        if c.is_ascii_uppercase() {
            let mut symbol = c.to_string();
            i += 1;
            while i < chars.len() && chars[i].is_ascii_lowercase() {
                symbol.push(chars[i]);
                i += 1;
            }
            out.push_str(&format!("\\mathrm{{{symbol}}}"));
            term_start = false;
        } else if c == '(' || c == '[' {
            // A state, (aq) (s) (l) (g), upright; otherwise a group.
            let close = if c == '(' { ')' } else { ']' };
            let end = (i..chars.len()).find(|&k| chars[k] == close).unwrap_or(chars.len());
            let inner: String = chars[i + 1..end].iter().collect();
            if c == '(' && ["aq", "s", "l", "g"].contains(&inner.as_str()) {
                out.push_str(&format!("\\mathrm{{({inner})}}"));
                i = (end + 1).min(chars.len());
                term_start = false;
                continue;
            }
            out.push(c);
            i += 1;
            term_start = true;
            continue;
        } else if c == ')' || c == ']' {
            out.push(c);
            i += 1;
            term_start = false;
        } else if c.is_ascii_lowercase() {
            out.push(c);
            i += 1;
            term_start = false;
        } else {
            out.push(c);
            i += 1;
            continue;
        }
        // After a symbol or group: a count, then a charge.
        let start = i;
        while i < chars.len() && chars[i].is_ascii_digit() {
            i += 1;
        }
        if i > start {
            out.push_str(&format!("_{{{}}}", chars[start..i].iter().collect::<String>()));
        }
        charge(&chars, &mut i, &mut out);
    }
    out.trim().to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn formulas_counts_and_coefficients() {
        assert_eq!(
            expand(r"\ce{2H2 + O2 -> 2H2O}"),
            r"{2\mathrm{H}_{2} + \mathrm{O}_{2} \rightarrow 2\mathrm{H}_{2}\mathrm{O}}"
        );
    }

    #[test]
    fn charges_states_and_arrows() {
        assert_eq!(expand(r"\ce{SO4^2-}"), r"{\mathrm{S}\mathrm{O}_{4}^{2-}}");
        assert_eq!(expand(r"\ce{Fe^{3+}}"), r"{\mathrm{Fe}^{3+}}");
        assert_eq!(expand(r"\ce{Na+ + Cl- -> NaCl(s)}"),
            r"{\mathrm{Na}^{+} + \mathrm{Cl}^{-} \rightarrow \mathrm{Na}\mathrm{Cl}\mathrm{(s)}}");
        assert_eq!(expand(r"\ce{N2 + 3H2 <=>[Fe] 2NH3}"),
            r"{\mathrm{N}_{2} + 3\mathrm{H}_{2} \overset{\text{Fe}}{\rightleftharpoons} 2\mathrm{N}\mathrm{H}_{3}}");
        assert_eq!(expand(r"\ce{Ca(OH)2}"), r"{\mathrm{Ca}(\mathrm{O}\mathrm{H})_{2}}");
    }

    #[test]
    fn hydrates_gases_and_units() {
        assert_eq!(expand(r"\ce{CuSO4*5H2O}"), r"{\mathrm{Cu}\mathrm{S}\mathrm{O}_{4}\cdot 5\mathrm{H}_{2}\mathrm{O}}");
        assert_eq!(expand(r"\ce{CO2 ^}"), r"{\mathrm{C}\mathrm{O}_{2}\uparrow}");
        assert_eq!(expand(r"x = \pu{25 kJ/mol}"), r"x = {25\ \mathrm{kJ/mol}}");
        // Other commands are left alone.
        assert_eq!(expand(r"\cedilla{c}"), r"\cedilla{c}");
    }
}
