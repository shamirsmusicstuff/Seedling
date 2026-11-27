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

/// Built-in skins, in the order of the picker. The first is the default.
pub const BUILTIN: [(&str, [&str; 5]); 8] = [
    ("Indigo", ["05040F", "3A2EE6", "150D7A", "CFC2FF", "EEEAFF"]),
    ("Dusk", ["151E3F", "030027", "F2F3D9", "DC9E82", "C16E70"]),
    ("Brass", ["363636", "242F40", "CCA43B", "E5E5E5", "FFFFFF"]),
    ("Oxblood", ["0A0908", "49111C", "F2F4F3", "A9927D", "5E503F"]),
    ("Seedling", ["0C120C", "C20114", "6D7275", "C7D6D5", "ECEBF3"]),
    ("Skin 1", ["202030", "39304A", "635C51", "7D7461", "B0A990"]),
    ("Skin 2", ["14110F", "34312D", "7E7F83", "D9C5B2", "F3F3F4"]),
    ("Skin 3", ["3B1F2B", "DB162F", "DBDFAC", "5F758E", "383961"]),
];

/// Skins whose roles are chosen by hand rather than by luminance: (ink, accent, slate, mist, paper).
fn explicit_roles(name: &str) -> Option<[&'static str; 5]> {
    match name {
        "Indigo" => Some(["05040F", "3A2EE6", "150D7A", "CFC2FF", "EEEAFF"]),
        // coolors.co/151e3f-030027-f2f3d9-dc9e82-c16e70
        "Dusk" => Some(["030027", "C16E70", "151E3F", "DC9E82", "F2F3D9"]),
        // coolors.co/363636-242f40-cca43b-e5e5e5-ffffff
        "Brass" => Some(["242F40", "CCA43B", "363636", "E5E5E5", "FFFFFF"]),
        // coolors.co/0a0908-49111c-f2f4f3-a9927d-5e503f
        "Oxblood" => Some(["0A0908", "49111C", "5E503F", "A9927D", "F2F4F3"]),
        _ => None,
    }
}

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

/// The text of a built-in skin's file (with its roles when they were chosen by hand).
fn builtin_json(name: &str, hex: &[&str; 5]) -> String {
    match explicit_roles(name) {
        Some(r) => format!(
            "{{\n  \"name\": \"{name}\",\n  \"colors\": [\"{}\", \"{}\", \"{}\", \"{}\", \"{}\"],\n  \"roles\": {{ \"ink\": \"{}\", \"accent\": \"{}\", \"slate\": \"{}\", \"mist\": \"{}\", \"paper\": \"{}\" }}\n}}\n",
            hex[0], hex[1], hex[2], hex[3], hex[4], r[0], r[1], r[2], r[3], r[4]
        ),
        None => to_json(name, hex),
    }
}

pub fn builtins() -> Vec<Skin> {
    BUILTIN.iter().filter_map(|(n, h)| parse(&builtin_json(n, h), n).ok()).collect()
}

const README: &str = "SEEDLING SKINS\n==============\n\nA skin is a small text file in this folder with the extension .seedskin. It is just five colours - the same as a coolors.co palette.\n\nCopy any skin, edit the name and the five hex colours, save, then pick \"Reload skins\" in the plugin's skin menu.\n\n  {\n    \"name\": \"My Skin\",\n    \"colors\": [\"202030\", \"39304A\", \"635C51\", \"7D7461\", \"B0A990\"]\n  }\n\nColours can be in any order. Seedling works out the roles:\n  darkest -> background, lightest -> text and highlights,\n  most vivid of the middle three -> neon accent, the other two -> lines/tracks and secondary text.\n\nTo choose the roles yourself add:  \"roles\": { \"ink\": \"..\", \"accent\": \"..\", \"slate\": \"..\", \"mist\": \"..\", \"paper\": \"..\" }\n\nA plain text file works too: a first line \"name: My Skin\" and a pasted coolors.co link:\n  https://coolors.co/0c120c-c20114-6d7275-c7d6d5-ecebf3\n";

pub fn skins_dir() -> PathBuf {
    let d = storage::seedling_root().join("Skins");
    let _ = std::fs::create_dir_all(&d);
    d
}

/// Reads every *.seedskin (writing a copy of each built-in skin and a README when missing). Built-ins come first, in their
/// own order (a file of the same name overrides the built-in), then any other skins. Returns the skins and a list of problems.
pub fn load_all() -> (Vec<Skin>, Vec<String>) {
    let dir = skins_dir();
    for (n, h) in BUILTIN.iter() {
        let f = dir.join(format!("{n}.seedskin"));
        if !f.exists() {
            let _ = std::fs::write(f, builtin_json(n, h));
        }
    }
    let readme = dir.join("README.txt");
    if !readme.exists() { let _ = std::fs::write(readme, README); }
    let (mut files, mut problems) = (Vec::new(), Vec::new());
    for f in storage::list_files(&dir, "seedskin") {
        let stem = f.file_stem().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
        match std::fs::read_to_string(&f).map_err(|e| e.to_string()).and_then(|t| parse(&t, &stem)) {
            Ok(s) => files.push(s),
            Err(e) => problems.push(format!("{}: {e}", f.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default())),
        }
    }
    (arrange(files, builtins()), problems)
}

/// Built-ins first (replaced by a same-named skin from a file), then the rest of the files.
fn arrange(mut files: Vec<Skin>, builtins: Vec<Skin>) -> Vec<Skin> {
    let mut out = Vec::new();
    for b in builtins {
        match files.iter().position(|s| s.name == b.name) {
            Some(i) => out.push(files.remove(i)),
            None => out.push(b),
        }
    }
    out.extend(files);
    out
}


// ---- the editor's colours, derived from a skin ----------------------------------------------------------------------

/// The colours the editor draws with. Everything is worked out from the five skin roles so any skin stays readable:
/// paper text always sits on something at least 4.5:1 darker.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Theme {
    /// window background and the darkest ridge at the front
    pub ink: Rgb,
    /// text and outlines
    pub paper: Rgb,
    /// knob fills, curves and the lit edge of every ridge
    pub mist: Rgb,
    /// the ridge at the back (the brighter of accent and slate, darkened until paper text reads on it)
    pub back: Rgb,
    /// the ridges in between
    pub mid: Rgb,
    /// the soft glow at the upper right
    pub glow: Rgb,
    /// hovered buttons and pills
    pub hover: Rgb,
    /// popup window, widgets, hovered widgets and pressed widgets
    pub window: Rgb,
    pub widget: Rgb,
    pub widget_hover: Rgb,
    pub widget_active: Rgb,
}

/// `c` mixed towards `ink` until `paper` reads on it with at least `min` contrast.
fn legible(c: Rgb, paper: Rgb, ink: Rgb, min: f32) -> Rgb {
    let mut t = 0.0;
    loop {
        let m = mix(c, ink, t);
        if contrast(paper, m) >= min || t >= 1.0 { return m; }
        t += 0.04;
    }
}

impl Theme {
    pub fn from_skin(s: &Skin) -> Theme {
        let (hi, lo) = if luminance(s.accent) >= luminance(s.slate) { (s.accent, s.slate) } else { (s.slate, s.accent) };
        let back = legible(hi, s.paper, s.ink, 4.5);
        let mid = legible(lo, s.paper, s.ink, 4.5);
        let (back, mid) = if luminance(mid) > luminance(back) { (mid, back) } else { (back, mid) };
        Theme {
            ink: s.ink,
            paper: s.paper,
            mist: s.mist,
            back,
            mid,
            glow: mix(s.accent, s.paper, 0.3),
            hover: back,
            window: mix(s.ink, back, 0.2),
            widget: mix(s.ink, back, 0.4),
            widget_hover: back,
            widget_active: legible(mix(back, s.paper, 0.2), s.paper, s.ink, 4.5),
        }
    }
}

// ---- Seed colours: a palette grown from the patch seed (OKLCH, readability guaranteed) ----------------------------

pub fn original() -> Skin {
    let (n, h) = BUILTIN.iter().find(|(n, _)| *n == "Seedling").expect("builtin");
    parse(&builtin_json(n, h), n).expect("builtin")
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
        assert_eq!(s.len(), BUILTIN.len());
        assert_eq!(s[0].name, "Indigo");
        let o = original();
        assert_eq!(o.name, "Seedling");
        assert_eq!(o.ink, [0x0C, 0x12, 0x0C]);
        assert_eq!(o.accent, [0xC2, 0x01, 0x14]);   // the vivid red
        assert_eq!(o.paper, [0xEC, 0xEB, 0xF3]);
        for k in &s { assert!(contrast(k.paper, k.ink) > 4.5, "{} readable", k.name); }
    }

    #[test]
    fn the_three_coolors_palettes_use_exactly_their_five_colours() {
        let hex = |c: Rgb| format!("{:02X}{:02X}{:02X}", c[0], c[1], c[2]);
        for (name, palette) in [("Dusk", "151E3F-030027-F2F3D9-DC9E82-C16E70"), ("Brass", "363636-242F40-CCA43B-E5E5E5-FFFFFF"), ("Oxblood", "0A0908-49111C-F2F4F3-A9927D-5E503F")] {
            let s = builtins().into_iter().find(|s| s.name == name).expect(name);
            let mut want: Vec<String> = palette.split('-').map(String::from).collect();
            let mut got: Vec<String> = [s.ink, s.accent, s.slate, s.mist, s.paper].iter().map(|c| hex(*c)).collect();
            want.sort();
            got.sort();
            assert_eq!(got, want, "{name}");
        }
    }

    #[test]
    fn indigo_theme_is_the_original_look() {
        let t = Theme::from_skin(&builtins()[0]);
        assert_eq!((t.ink, t.back, t.mid, t.mist, t.paper), ([5, 4, 15], [0x3A, 0x2E, 0xE6], [0x15, 0x0D, 0x7A], [0xCF, 0xC2, 0xFF], [0xEE, 0xEA, 0xFF]));
    }

    #[test]
    fn every_theme_is_readable_and_ridges_get_darker_towards_the_front() {
        let mut all = builtins();
        all.extend((1..400u32).step_by(13).map(from_seed));
        for s in &all {
            let t = Theme::from_skin(s);
            assert!(contrast(t.paper, t.back) >= 4.5, "{}: paper on back ridge", s.name);
            assert!(contrast(t.paper, t.mid) >= 4.5, "{}: paper on middle ridge", s.name);
            assert!(contrast(t.paper, t.widget) >= 4.5, "{}: paper on widgets", s.name);
            assert!(contrast(t.paper, t.window) >= 4.5, "{}: paper on popups", s.name);
            assert!(luminance(t.back) >= luminance(t.mid), "{}: back ridge is the brightest", s.name);
        }
    }

    #[test]
    fn built_ins_come_first_and_files_override_them() {
        let mut mine = builtins()[2].clone();
        mine.ink = [1, 2, 3];
        let other = Skin { name: "Mine".into(), ..mine.clone() };
        let out = arrange(vec![other.clone(), mine.clone()], builtins());
        assert_eq!(out.len(), BUILTIN.len() + 1);
        assert_eq!(out[2].ink, [1, 2, 3]);
        assert_eq!(out.last().unwrap().name, "Mine");
        assert_eq!(arrange(vec![], builtins()), builtins());
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
