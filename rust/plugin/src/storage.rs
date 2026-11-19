//! Files, presets and the text form of a wavetable (base64 inside host sessions and presets). std only.

use seedling_dsp::params::{NUM_PARAMS, PARAMS};
use seedling_dsp::wavetable::{deserialize_wavetable, serialize_wavetable, Wavetable};
use std::path::{Path, PathBuf};

const B64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

pub fn b64_encode(data: &[u8]) -> String {
    let mut out = String::with_capacity(data.len().div_ceil(3) * 4);
    for chunk in data.chunks(3) {
        let n = (chunk[0] as u32) << 16 | (*chunk.get(1).unwrap_or(&0) as u32) << 8 | *chunk.get(2).unwrap_or(&0) as u32;
        out.push(B64[(n >> 18) as usize & 63] as char);
        out.push(B64[(n >> 12) as usize & 63] as char);
        out.push(if chunk.len() > 1 { B64[(n >> 6) as usize & 63] as char } else { '=' });
        out.push(if chunk.len() > 2 { B64[n as usize & 63] as char } else { '=' });
    }
    out
}

pub fn b64_decode(text: &str) -> Option<Vec<u8>> {
    let mut out = Vec::with_capacity(text.len() / 4 * 3);
    let (mut acc, mut bits) = (0u32, 0u32);
    for c in text.bytes() {
        let v = match c {
            b'A'..=b'Z' => c - b'A',
            b'a'..=b'z' => c - b'a' + 26,
            b'0'..=b'9' => c - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            b'=' => break,
            b' ' | b'\n' | b'\r' | b'\t' => continue,
            _ => return None,
        };
        acc = acc << 6 | v as u32;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push((acc >> bits) as u8);
            acc &= (1 << bits) - 1;
        }
    }
    Some(out)
}

pub fn table_to_text(t: &Wavetable) -> String {
    b64_encode(&serialize_wavetable(t))
}

pub fn table_from_text(text: &str) -> Option<Wavetable> {
    if text.is_empty() {
        return None;
    }
    deserialize_wavetable(&b64_decode(text)?)
}

fn documents_dir() -> PathBuf {
    let home = std::env::var_os("USERPROFILE").or_else(|| std::env::var_os("HOME")).map(PathBuf::from).unwrap_or_default();
    home.join("Documents")
}

fn ensure(dir: PathBuf) -> PathBuf {
    let _ = std::fs::create_dir_all(&dir);
    dir
}

pub fn seedling_root() -> PathBuf { ensure(documents_dir().join("Seedling")) }
pub fn wavetable_dir() -> PathBuf { ensure(documents_dir().join("Seedling").join("Wavetables")) }
pub fn preset_dir() -> PathBuf { ensure(documents_dir().join("Seedling").join("Presets")) }

/// Files with the given extension (case-insensitive), sorted by name.
pub fn list_files(dir: &Path, ext: &str) -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = std::fs::read_dir(dir)
        .map(|rd| rd.filter_map(|e| e.ok().map(|e| e.path())).collect())
        .unwrap_or_default();
    v.retain(|p| p.extension().and_then(|e| e.to_str()).is_some_and(|e| e.eq_ignore_ascii_case(ext)));
    v.sort_by_key(|p| p.file_name().map(|n| n.to_string_lossy().to_lowercase()));
    v
}

pub fn open_folder(dir: &Path) {
    let cmd = if cfg!(target_os = "windows") { "explorer" } else if cfg!(target_os = "macos") { "open" } else { "xdg-open" };
    let _ = std::process::Command::new(cmd).arg(dir).spawn();
}

/// A saved patch: the 52 synth values, the seed it came from and optionally a custom wavetable.
#[derive(Clone, Debug, PartialEq)]
pub struct Preset {
    pub values: Vec<f32>,
    pub seed: u32,
    pub wavetable: String,
    pub wavetable_info: String,
}

pub fn preset_to_text(p: &Preset) -> String {
    let mut s = String::from("seedling-preset 1\n");
    s += &format!("seed={}\n", p.seed);
    for (def, v) in PARAMS.iter().zip(&p.values) {
        s += &format!("{}={:?}\n", def.id, v);
    }
    if !p.wavetable.is_empty() {
        s += &format!("wavetable_info={}\nwavetable={}\n", p.wavetable_info.replace('\n', " "), p.wavetable);
    }
    s
}

pub fn preset_from_text(text: &str) -> Option<Preset> {
    let mut lines = text.lines();
    if !lines.next()?.starts_with("seedling-preset") {
        return None;
    }
    let mut p = Preset { values: PARAMS.iter().map(|d| d.def).collect(), seed: 0, wavetable: String::new(), wavetable_info: String::new() };
    for line in lines {
        let Some((k, v)) = line.split_once('=') else { continue };
        match k {
            "seed" => p.seed = v.trim().parse().unwrap_or(0),
            "wavetable" => p.wavetable = v.trim().to_string(),
            "wavetable_info" => p.wavetable_info = v.to_string(),
            id => {
                if let (Some(i), Ok(x)) = (PARAMS.iter().position(|d| d.id == id), v.trim().parse::<f32>()) {
                    if x.is_finite() {
                        p.values[i] = x.clamp(PARAMS[i].lo, PARAMS[i].hi);
                    }
                }
            }
        }
    }
    debug_assert_eq!(p.values.len(), NUM_PARAMS);
    Some(p)
}

pub fn safe_file_stem(name: &str) -> String {
    let s: String = name.chars().map(|c| if c.is_alphanumeric() || c == ' ' || c == '-' || c == '_' { c } else { '_' }).collect();
    let s = s.trim().to_string();
    if s.is_empty() { "Untitled".into() } else { s }
}

#[cfg(test)]
mod tests {
    use super::*;
    use seedling_dsp::wavetable::make_builtin_wavetable;

    #[test]
    fn base64_round_trip() {
        for n in 0..40usize {
            let data: Vec<u8> = (0..n).map(|i| (i * 37 + 11) as u8).collect();
            assert_eq!(b64_decode(&b64_encode(&data)).unwrap(), data);
        }
        assert_eq!(b64_encode(b"Man"), "TWFu");
        assert_eq!(b64_encode(b"Ma"), "TWE=");
        assert!(b64_decode("@@@").is_none());
    }

    #[test]
    fn wavetable_text_round_trip() {
        let t = make_builtin_wavetable();
        let back = table_from_text(&table_to_text(&t)).expect("decodes");
        assert_eq!(back.frames, t.frames);
        assert!(table_from_text("").is_none());
        assert!(table_from_text("not base64!!").is_none());
    }

    #[test]
    fn preset_round_trip_and_junk() {
        let p = Preset { values: PARAMS.iter().map(|d| d.def + 0.01 * (d.hi - d.lo)).collect(), seed: 1234567, wavetable: "QUJD".into(), wavetable_info: "A3 +6 ct".into() };
        let back = preset_from_text(&preset_to_text(&p)).unwrap();
        assert_eq!(back, p);
        assert!(preset_from_text("hello").is_none());
        let junk = preset_from_text("seedling-preset 1\nenv_time=abc\nzzz=1\nvol_atk=999\n").unwrap();
        assert_eq!(junk.values[4], PARAMS[4].hi);
    }
}
