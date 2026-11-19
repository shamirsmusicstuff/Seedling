//! Skins: a skin is five colours (like a coolors.co palette). Files live in `Documents/Seedling/Skins/*.seedskin`
//! (same format as the JUCE version), plus the virtual "Seed colours" skin whose palette is grown from the patch seed.
//! std only.

use crate::storage;
use std::path::PathBuf;

pub type Rgb = [u8; 3];

#[derive(Clone, Debug, PartialEq)]
pub struct Skin {
    pub name: String,
    /// background
    pub ink: Rgb,
    /// neon / active colour
    pub accent: Rgb,
    /// lines and tracks
    pub slate: Rgb,
    /// secondary text and curves
    pub mist: Rgb,
    /// main text and highlights
    pub paper: Rgb,
}

pub const SEED_COLOURS: &str = "Seed colours";

pub const BUILTIN: [(&str, [&str; 5]); 4] = [
    ("Seedling", ["0C120C", "C20114", "6D7275", "C7D6D5", "ECEBF3"]),
    ("Skin 1", ["202030", "39304A", "635C51", "7D7461", "B0A990"]),
    ("Skin 2", ["14110F", "34312D", "7E7F83", "D9C5B2", "F3F3F4"]),
    ("Skin 3", ["3B1F2B", "DB162F", "DBDFAC", "5F758E", "383961"]),
];

// ---- colour maths -----------------------------------------------------------------------------------------------

fn to_linear(c: f32) -> f32 { if c <= 0.04045 { c / 12.92 } else { ((c + 0.055) / 1.055).powf(2.4) } }
fn from_linear(c: f32) -> f32 { if c <= 0.003_130_8 { 12.92 * c } else { 1.055 * c.powf(1.0 / 2.4) - 0.055 } }

pub fn luminance(c: Rgb) -> f32 {
    0.2126 * to_linear(c[0] as f32 / 255.0) + 0.7152 * to_linear(c[1] as f32 / 255.0) + 0.0722 * to_linear(c[2] as f32 / 255.0)
}

pub fn contrast(a: Rgb, b: Rgb) -> f32 {
    let (mut la, mut lb) = (luminance(a), luminance(b));
    if la < lb { std::mem::swap(&mut la, &mut lb); }
    (la + 0.05) / (lb + 0.05)
}

/// HSV saturation 0..1
fn saturation(c: Rgb) -> f32 {
    let (mx, mn) = (c.iter().copied().max().unwrap() as f32, c.iter().copied().min().unwrap() as f32);
    if mx <= 0.0 { 0.0 } else { (mx - mn) / mx }
}

pub fn mix(a: Rgb, b: Rgb, t: f32) -> Rgb {
    let m = |x: u8, y: u8| (x as f32 + (y as f32 - x as f32) * t).round().clamp(0.0, 255.0) as u8;
    [m(a[0], b[0]), m(a[1], b[1]), m(a[2], b[2])]
}

pub fn mix_skin(a: &Skin, b: &Skin, t: f32) -> Skin {
    Skin { name: b.name.clone(), ink: mix(a.ink, b.ink, t), accent: mix(a.accent, b.accent, t), slate: mix(a.slate, b.slate, t), mist: mix(a.mist, b.mist, t), paper: mix(a.paper, b.paper, t) }
}

/// Five colours in any order -> roles (darkest = ink, lightest = paper, most vivid of the middle three = accent).
pub fn assign_roles(name: &str, mut c: [Rgb; 5]) -> Skin {
    c.sort_by(|a, b| luminance(*a).partial_cmp(&luminance(*b)).unwrap_or(std::cmp::Ordering::Equal));
    let mut best = 1;
    let mut best_score = -1.0f32;
    for i in 1..=3 {
        let score = saturation(c[i]) + ((contrast(c[i], c[0]) - 1.0) / 5.0).min(1.0);
        if score > best_score { best_score = score; best = i; }
    }
    let rest: Vec<Rgb> = (1..=3).filter(|&i| i != best).map(|i| c[i]).collect();
    Skin { name: name.to_string(), ink: c[0], accent: c[best], slate: rest[0], mist: rest[1], paper: c[4] }
}

fn hex_colour(tok: &str) -> Option<Rgb> {
    let t = tok.trim().trim_start_matches('#');
    if t.len() != 6 || !t.chars().all(|c| c.is_ascii_hexdigit()) { return None; }
    let v = u32::from_str_radix(t, 16).ok()?;
    Some([(v >> 16) as u8, (v >> 8) as u8, v as u8])
}

/// The string value of `"key": "value"` in a JSON-ish text.
fn json_string(text: &str, key: &str) -> Option<String> {
    let k = format!("\"{key}\"");
    let at = text.find(&k)? + k.len();
    let rest = &text[at..];
    let rest = rest[rest.find(':')? + 1..].trim_start();
    let rest = rest.strip_prefix('"')?;
    Some(rest[..rest.find('"')?].to_string())
}

/// Parses the text of a skin file: JSON ({"name", "colors", optional "roles"}) or plain text with "name: X" and five hex colours.
pub fn parse(text: &str, fallback_name: &str) -> Result<Skin, String> {
    let t = text.trim();
    let mut name = fallback_name.to_string();
    let mut cols: Vec<Rgb> = Vec::new();
    let mut roles: Option<Skin> = None;

    if t.starts_with('{') {
        if !t.ends_with('}') { return Err("not valid JSON".into()); }
        if let Some(n) = json_string(t, "name") { if !n.is_empty() { name = n; } }
        if let Some(at) = t.find("\"colors\"") {
            let rest = &t[at..];
            if let (Some(a), Some(b)) = (rest.find('['), rest.find(']')) {
                for part in rest[a + 1..b].split(',') {
                    if let Some(c) = hex_colour(part.trim().trim_matches('"')) { cols.push(c); }
                }
            }
        }
        if let Some(at) = t.find("\"roles\"") {
            let r = &t[at..];
            let get = |k: &str| json_string(r, k).and_then(|v| hex_colour(&v));
            match (get("ink"), get("accent"), get("slate"), get("mist"), get("paper")) {
                (Some(ink), Some(accent), Some(slate), Some(mist), Some(paper)) => {
                    roles = Some(Skin { name: String::new(), ink, accent, slate, mist, paper });
                }
                _ => return Err("\"roles\" needs ink, accent, slate, mist and paper as 6-digit hex colours".into()),
            }
        }
    } else {
        for line in t.lines() {
            let l = line.trim();
            if l.len() >= 5 && l[..5].eq_ignore_ascii_case("name:") { name = l[5..].trim().to_string(); continue; }
            if l.starts_with("//") { continue; }
            for tok in l.split(|c: char| " ,;-/:.\t\"[]()".contains(c)) {
                if let Some(c) = hex_colour(tok) { cols.push(c); }
            }
        }
    }
    let name = if name.trim().is_empty() { "Unnamed".to_string() } else { name.trim().to_string() };
    if let Some(mut s) = roles { s.name = name; return Ok(s); }
    if cols.len() < 5 { return Err(format!("needs 5 hex colours (found {})", cols.len())); }
    Ok(assign_roles(&name, [cols[0], cols[1], cols[2], cols[3], cols[4]]))
}

pub fn to_json(name: &str, hex: &[&str; 5]) -> String {
    format!("{{\n  \"name\": \"{name}\",\n  \"colors\": [\"{}\", \"{}\", \"{}\", \"{}\", \"{}\"]\n}}\n", hex[0], hex[1], hex[2], hex[3], hex[4])
}

pub fn builtins() -> Vec<Skin> {
    BUILTIN.iter().filter_map(|(n, h)| parse(&to_json(n, h), n).ok()).collect()
}

const README: &str = "SEEDLING SKINS\n==============\n\nA skin is a small text file in this folder with the extension .seedskin. It is just five colours - the same as a coolors.co palette.\n\nCopy any skin, edit the name and the five hex colours, save, then pick \"Reload skins\" in the plugin's skin menu.\n\n  {\n    \"name\": \"My Skin\",\n    \"colors\": [\"202030\", \"39304A\", \"635C51\", \"7D7461\", \"B0A990\"]\n  }\n\nColours can be in any order. Seedling works out the roles:\n  darkest -> background, lightest -> text and highlights,\n  most vivid of the middle three -> neon accent, the other two -> lines/tracks and secondary text.\n\nTo choose the roles yourself add:  \"roles\": { \"ink\": \"..\", \"accent\": \"..\", \"slate\": \"..\", \"mist\": \"..\", \"paper\": \"..\" }\n\nA plain text file works too: a first line \"name: My Skin\" and a pasted coolors.co link:\n  https://coolors.co/0c120c-c20114-6d7275-c7d6d5-ecebf3\n";

pub fn skins_dir() -> PathBuf {
    let d = storage::seedling_root().join("Skins");
    let _ = std::fs::create_dir_all(&d);
    d
}

/// Reads every *.seedskin (writing the starter skins and a README on first run). Returns the skins and a list of problems.
pub fn load_all() -> (Vec<Skin>, Vec<String>) {
    let dir = skins_dir();
    if storage::list_files(&dir, "seedskin").is_empty() {
        for (n, h) in BUILTIN.iter() {
            let _ = std::fs::write(dir.join(format!("{n}.seedskin")), to_json(n, h));
        }
    }
    let readme = dir.join("README.txt");
    if !readme.exists() { let _ = std::fs::write(readme, README); }
    let (mut out, mut problems) = (Vec::new(), Vec::new());
    for f in storage::list_files(&dir, "seedskin") {
        let stem = f.file_stem().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
        match std::fs::read_to_string(&f).map_err(|e| e.to_string()).and_then(|t| parse(&t, &stem)) {
            Ok(s) => out.push(s),
            Err(e) => problems.push(format!("{}: {e}", f.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default())),
        }
    }
    if out.is_empty() { out = builtins(); }
    (out, problems)
}

// ---- Seed colours: a palette grown from the patch seed (OKLCH, readability guaranteed) ----------------------------

pub fn original() -> Skin {
    parse(&to_json(BUILTIN[0].0, &BUILTIN[0].1), "Seedling").expect("builtin")
}

fn oklch_to_linear(l: f32, c: f32, hue_deg: f32) -> [f32; 3] {
    let h = hue_deg * std::f32::consts::PI / 180.0;
    let (a, b) = (c * h.cos(), c * h.sin());
    let l_ = l + 0.396_337_78 * a + 0.215_803_76 * b;
    let m_ = l - 0.105_561_346 * a - 0.063_854_17 * b;
    let s_ = l - 0.089_484_18 * a - 1.291_485_5 * b;
    let (l3, m3, s3) = (l_ * l_ * l_, m_ * m_ * m_, s_ * s_ * s_);
    [
        4.076_741_7 * l3 - 3.307_711_6 * m3 + 0.230_969_93 * s3,
        -1.268_438 * l3 + 2.609_757_4 * m3 - 0.341_319_4 * s3,
        -0.004_196_086 * l3 - 0.703_418_6 * m3 + 1.707_614_7 * s3,
    ]
}

fn in_gamut(c: [f32; 3]) -> bool { c.iter().all(|v| *v >= -0.0005 && *v <= 1.0005) }

fn to8(lin: f32) -> u8 { (from_linear(lin.clamp(0.0, 1.0)).clamp(0.0, 1.0) * 255.0).round() as u8 }

fn oklch(l: f32, c: f32, hue: f32) -> Rgb {
    let mut lin = oklch_to_linear(l, c, hue);
    if !in_gamut(lin) {
        let (mut lo, mut hi) = (0.0f32, c);
        for _ in 0..16 {
            let mid = 0.5 * (lo + hi);
            if in_gamut(oklch_to_linear(l, mid, hue)) { lo = mid } else { hi = mid }
        }
        lin = oklch_to_linear(l, lo, hue);
    }
    [to8(lin[0]), to8(lin[1]), to8(lin[2])]
}

struct Stream(u64);
impl Stream {
    fn new(seed: u32) -> Self { Stream((seed as u64).wrapping_mul(0xD1B5_4A32_D192_ED03) ^ 0x5EED_C010_1200) }
    fn next(&mut self) -> f32 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^= z >> 31;
        (z >> 40) as f32 * (1.0 / 16_777_216.0)
    }
    fn range(&mut self, lo: f32, hi: f32) -> f32 { lo + (hi - lo) * self.next() }
    fn pick(&mut self, n: usize) -> usize { ((self.next() * n as f32) as usize).min(n - 1) }
}

struct Spec { l: f32, c: f32, h: f32 }
impl Spec { fn rgb(&self) -> Rgb { oklch(self.l, self.c, self.h) } }

/// The palette of a seed. Seed 0 (no seed yet) is the original Seedling palette. The same seed always gives the same colours.
pub fn from_seed(seed: u32) -> Skin {
    if seed == 0 {
        let mut s = original();
        s.name = SEED_COLOURS.into();
        return s;
    }
    let mut r = Stream::new(seed);
    let h0 = r.range(0.0, 360.0);
    const SCHEMES: [[f32; 3]; 6] = [[35.0, -35.0, 70.0], [180.0, 20.0, 160.0], [150.0, 210.0, 30.0], [120.0, 240.0, 60.0], [90.0, 180.0, 270.0], [15.0, 30.0, -20.0]];
    let sc = SCHEMES[r.pick(6)];
    let flip = if r.next() < 0.5 { -1.0 } else { 1.0 };
    let ink = Spec { l: r.range(0.15, 0.23), c: r.range(0.030, 0.070), h: h0 };
    let mut accent = Spec { l: r.range(0.50, 0.60), c: r.range(0.120, 0.190), h: h0 + flip * sc[0] };
    let mut slate = Spec { l: r.range(0.40, 0.50), c: r.range(0.050, 0.120), h: h0 + flip * sc[1] };
    let mut mist = Spec { l: r.range(0.80, 0.88), c: r.range(0.030, 0.080), h: h0 + flip * sc[2] };
    let paper = Spec { l: r.range(0.95, 0.97), c: r.range(0.010, 0.030), h: h0 };
    let (ink_rgb, paper_rgb) = (ink.rgb(), paper.rgb());
    while contrast(accent.rgb(), ink_rgb) < 2.0 && accent.l < 0.75 { accent.l += 0.01; }
    while contrast(paper_rgb, accent.rgb()) < 4.5 && accent.l > 0.25 { accent.l -= 0.01; }
    while contrast(paper_rgb, slate.rgb()) < 5.5 && slate.l > 0.20 { slate.l -= 0.01; }
    while contrast(mist.rgb(), ink_rgb) < 4.5 && mist.l < 0.95 { mist.l += 0.01; }
    Skin { name: SEED_COLOURS.into(), ink: ink_rgb, accent: accent.rgb(), slate: slate.rgb(), mist: mist.rgb(), paper: paper_rgb }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_roles() {
        let s = builtins();
        assert_eq!(s.len(), 4);
        assert_eq!(s[0].name, "Seedling");
        assert_eq!(s[0].ink, [0x0C, 0x12, 0x0C]);
        assert_eq!(s[0].accent, [0xC2, 0x01, 0x14]);   // the vivid red
        assert_eq!(s[0].paper, [0xEC, 0xEB, 0xF3]);
        for k in &s { assert!(contrast(k.paper, k.ink) > 4.5, "{} readable", k.name); }
    }

    #[test]
    fn parses_json_text_and_roles() {
        let j = parse("{ \"name\": \"X\", \"colors\": [\"000000\",\"ff0000\",\"333333\",\"888888\",\"ffffff\"] }", "f").unwrap();
        assert_eq!(j.name, "X");
        assert_eq!(j.accent, [255, 0, 0]);
        let t = parse("name: Pasted\nhttps://coolors.co/0c120c-c20114-6d7275-c7d6d5-ecebf3", "f").unwrap();
        assert_eq!(t.name, "Pasted");
        assert_eq!(t.ink, [0x0c, 0x12, 0x0c]);
        let r = parse("{\"colors\":[],\"roles\":{\"ink\":\"000000\",\"accent\":\"ff0000\",\"slate\":\"111111\",\"mist\":\"aaaaaa\",\"paper\":\"ffffff\"}}", "named").unwrap();
        assert_eq!((r.name.as_str(), r.accent), ("named", [255, 0, 0]));
        assert!(parse("{ broken", "f").is_err());
        assert!(parse("name: Few\n112233 445566", "f").is_err());
        assert!(parse("{\"roles\":{\"ink\":\"000000\"}}", "f").is_err());
    }

    #[test]
    fn seed_palette_is_deterministic_and_readable() {
        assert_eq!(from_seed(0).ink, original().ink);
        assert_eq!(from_seed(123456), from_seed(123456));
        assert_ne!(from_seed(1).accent, from_seed(2).accent);
        for seed in (1..30_000u32).step_by(7).chain(1_000_000..1_020_000) {
            let p = from_seed(seed);
            assert!(contrast(p.paper, p.ink) >= 10.0, "seed {seed}: paper on ink");
            assert!(contrast(p.paper, p.accent) >= 4.5, "seed {seed}: paper on accent");
            assert!(contrast(p.paper, p.slate) >= 5.5, "seed {seed}: paper on slate");
            assert!(contrast(p.mist, p.ink) >= 4.5, "seed {seed}: mist on ink");
        }
    }
}
