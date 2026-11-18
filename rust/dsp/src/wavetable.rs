//! Wavetables for the "Wavetable" sound source, plus the importer that turns a sound file into one.
//! Dependency-free port of `Source/Wavetable.h`.
//!
//! * A table is N frames of [`WT_SIZE`] samples, one cycle each (like Serum). The A Form knob scans through the frames.
//! * Every frame is stored band-limited at [`WT_LEVELS`] levels (512 ... 2 harmonics); the voice picks the level for
//!   the note it plays ([`Wavetable::level_for`]), so high notes never alias.
//! * [`import_wavetable`] accepts a wavetable file (length a multiple of 2048/1024/512/256: used as it is) or any
//!   sampled sound: its pitch is detected (YIN), then it is cut into pitch-synchronous cycles, every cycle resampled
//!   to exactly one period, so the table plays IN TUNE on every key (Correct Tune, applied to a sample).
//!
//! # What the engine needs
//! Per sample, per voice: `let level = Wavetable::level_for(dt);` (once per note/block; `dt` = phase increment per
//! sample, i.e. frequency / sample rate) and `let y = table.read(pos, phase, level);` where `pos` in 0..=1 scans the
//! frames (linear crossfade between neighbouring frames) and `phase` in 0..1 is the oscillator phase (linear
//! interpolation inside a frame). A [`Wavetable`] is immutable after construction, so wrap it in an `Arc` and share it.

use std::f64::consts::PI;

pub const WT_SIZE: usize = 2048;
pub const WT_LEVELS: usize = 9;
pub const WT_MAX_FRAMES: usize = 64;

/// Highest harmonic kept at band-limit `level` (0 = 512 harmonics ... 8 = 2 harmonics).
pub fn wt_cap(level: usize) -> usize {
    512 >> level
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
struct Cx {
    re: f64,
    im: f64,
}

impl Cx {
    fn mul(self, o: Cx) -> Cx {
        Cx { re: self.re * o.re - self.im * o.im, im: self.re * o.im + self.im * o.re }
    }
    fn add(self, o: Cx) -> Cx {
        Cx { re: self.re + o.re, im: self.im + o.im }
    }
    fn sub(self, o: Cx) -> Cx {
        Cx { re: self.re - o.re, im: self.im - o.im }
    }
    fn norm(self) -> f64 {
        self.re * self.re + self.im * self.im
    }
}

/// One wavetable: `frames` cycles of 2048 samples plus their band-limited copies.
#[derive(Clone, Debug)]
pub struct Wavetable {
    pub frames: usize,
    pub name: String,
    /// frames * WT_SIZE, normalised, DC-free
    pub base: Vec<f32>,
    /// per level: frames * (WT_SIZE + 1), band-limited, `[WT_SIZE]` repeats `[0]`
    pub mip: [Vec<f32>; WT_LEVELS],

    // what the importer found (for the display and for presets)
    pub user_made: bool,
    pub pitched: bool,
    pub table_file: bool,
    pub root_hz: f32,
    pub cents_off: f32,
    /// MIDI note of the detected pitch, -1 = none
    pub root_note: i32,
}

impl Default for Wavetable {
    fn default() -> Self {
        Wavetable {
            frames: 0,
            name: "Built-in".to_string(),
            base: Vec::new(),
            mip: Default::default(),
            user_made: false,
            pitched: false,
            table_file: false,
            root_hz: 0.0,
            cents_off: 0.0,
            root_note: -1,
        }
    }
}

/// In-place radix-2 FFT. `a.len()` must be a power of two.
fn fft_cx(a: &mut [Cx], inverse: bool) {
    let n = a.len();
    let mut j = 0usize;
    for i in 1..n {
        let mut bit = n >> 1;
        while j & bit != 0 {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if i < j {
            a.swap(i, j);
        }
    }
    let mut len = 2;
    while len <= n {
        let ang = 2.0 * PI / len as f64 * if inverse { 1.0 } else { -1.0 };
        let wl = Cx { re: ang.cos(), im: ang.sin() };
        let mut i = 0;
        while i < n {
            let mut w = Cx { re: 1.0, im: 0.0 };
            for k in 0..len / 2 {
                let u = a[i + k];
                let v = a[i + k + len / 2].mul(w);
                a[i + k] = u.add(v);
                a[i + k + len / 2] = u.sub(v);
                w = w.mul(wl);
            }
            i += len;
        }
        len <<= 1;
    }
    if inverse {
        let s = n as f64;
        for x in a.iter_mut() {
            x.re /= s;
            x.im /= s;
        }
    }
}

impl Wavetable {
    /// Real FFT of a real signal (length must be a power of two): returns (re, im) per bin.
    pub fn fft(input: &[f32]) -> Vec<(f64, f64)> {
        let mut a: Vec<Cx> = input.iter().map(|&v| Cx { re: v as f64, im: 0.0 }).collect();
        fft_cx(&mut a, false);
        a.into_iter().map(|c| (c.re, c.im)).collect()
    }

    /// Magnitude squared per bin of the FFT of `input` (handy for tests and analysis).
    pub fn power_spectrum(input: &[f32]) -> Vec<f64> {
        let mut a: Vec<Cx> = input.iter().map(|&v| Cx { re: v as f64, im: 0.0 }).collect();
        fft_cx(&mut a, false);
        a.iter().map(|c| c.norm()).collect()
    }

    /// Build the band-limited levels from `base`.
    pub fn build(&mut self) {
        let frames = self.frames;
        for m in self.mip.iter_mut() {
            *m = vec![0.0; frames * (WT_SIZE + 1)];
        }
        let mut spec = vec![Cx::default(); WT_SIZE];
        let mut tmp = vec![Cx::default(); WT_SIZE];
        for f in 0..frames {
            for i in 0..WT_SIZE {
                spec[i] = Cx { re: self.base[f * WT_SIZE + i] as f64, im: 0.0 };
            }
            fft_cx(&mut spec, false);
            for l in 0..WT_LEVELS {
                let cap = wt_cap(l);
                for k in 0..WT_SIZE {
                    let h = if k <= WT_SIZE / 2 { k } else { WT_SIZE - k };
                    tmp[k] = if h == 0 || h > cap { Cx::default() } else { spec[k] };
                }
                fft_cx(&mut tmp, true);
                let out = &mut self.mip[l][f * (WT_SIZE + 1)..(f + 1) * (WT_SIZE + 1)];
                for i in 0..WT_SIZE {
                    out[i] = tmp[i].re as f32;
                }
                out[WT_SIZE] = out[0];
            }
        }
    }

    /// Band-limit level for a note whose phase increment per sample is `dt` (= frequency / sample rate).
    pub fn level_for(dt: f32) -> usize {
        let hmax = 0.5 / dt.max(1e-5);
        let mut l = 0;
        while l < WT_LEVELS - 1 && wt_cap(l) as f32 > hmax {
            l += 1;
        }
        l
    }

    /// Read one sample. `pos` 0..1 scans the frames, `ph` 0..1 is the phase, `level` from [`Wavetable::level_for`].
    pub fn read(&self, pos: f32, ph: f32, level: usize) -> f32 {
        if self.frames == 0 {
            return 0.0;
        }
        let level = level.min(WT_LEVELS - 1);
        let fp = pos.clamp(0.0, 1.0) * (self.frames - 1) as f32;
        let f0 = (fp as usize).min(self.frames - 1);
        let f1 = (f0 + 1).min(self.frames - 1);
        let ff = fp - f0 as f32;
        let m = &self.mip[level];
        let a = &m[f0 * (WT_SIZE + 1)..(f0 + 1) * (WT_SIZE + 1)];
        let b = &m[f1 * (WT_SIZE + 1)..(f1 + 1) * (WT_SIZE + 1)];
        let x = ph * WT_SIZE as f32;
        let i = (x as i32).clamp(0, WT_SIZE as i32 - 1) as usize;
        let fr = (x - i as f32).clamp(0.0, 1.0);
        let va = a[i] + (a[i + 1] - a[i]) * fr;
        if f1 == f0 || ff <= 0.0 {
            return va;
        }
        let vb = b[i] + (b[i + 1] - b[i]) * fr;
        va + (vb - va) * ff
    }

    /// Normalise every frame (DC removed, optionally equal loudness) and the table (peak 0.95). Call before `build()`.
    pub fn normalise(&mut self, equal_loudness: bool) {
        let frames = self.frames;
        let mut rms = vec![0.0f32; frames];
        for f in 0..frames {
            let x = &mut self.base[f * WT_SIZE..(f + 1) * WT_SIZE];
            let mut m = 0.0f64;
            for v in x.iter() {
                m += *v as f64;
            }
            m /= WT_SIZE as f64;
            let mut e = 0.0f64;
            for v in x.iter_mut() {
                *v -= m as f32;
                e += *v as f64 * *v as f64;
            }
            rms[f] = (e / WT_SIZE as f64).sqrt() as f32;
        }
        if equal_loudness && frames > 0 {
            let mut r = rms.clone();
            r.sort_by(|a, b| a.total_cmp(b));
            let target = r[r.len() / 2];
            for f in 0..frames {
                if rms[f] > 1e-6 && target > 1e-6 {
                    let g = (target / rms[f]).min(8.0);
                    for v in &mut self.base[f * WT_SIZE..(f + 1) * WT_SIZE] {
                        *v *= g;
                    }
                }
            }
        }
        let pk = self.base.iter().fold(0.0f32, |p, v| p.max(v.abs()));
        if pk > 1e-9 {
            for v in self.base.iter_mut() {
                *v *= 0.95 / pk;
            }
        }
    }
}

// ============================================================================================================
// The eight built-in frames (sine, soft, organ, hollow, bright, vowel, glass, metal): the table the Wavetable
// source uses until you load your own.
pub fn make_builtin_wavetable() -> Wavetable {
    let mut t = Wavetable { frames: 8, name: "Built-in".to_string(), ..Default::default() };
    t.base = vec![0.0; 8 * WT_SIZE];
    let sin_t: Vec<f32> = (0..WT_SIZE).map(|i| (2.0 * PI * i as f64 / WT_SIZE as f64).sin() as f32).collect();
    const DH: [usize; 6] = [1, 2, 3, 4, 6, 8];
    const DA: [f32; 6] = [1.0, 0.8, 0.5, 0.4, 0.3, 0.2];
    let tau = 6.283_185_5_f32;
    for f in 0..8 {
        let x = &mut t.base[f * WT_SIZE..(f + 1) * WT_SIZE];
        for h in 1..=255usize {
            let hf = h as f32;
            let mut a = 0.0f32;
            let mut ph = 0.0f32;
            match f {
                0 => a = if h == 1 { 1.0 } else { 0.0 },
                1 => a = 1.0 / (hf * hf),
                2 => {
                    for k in 0..6 {
                        if DH[k] == h {
                            a = DA[k];
                        }
                    }
                }
                3 => a = if h & 1 == 1 { 1.0 / hf } else { 0.0 },
                4 => a = 1.0 / hf,
                5 => {
                    let (c1, c2, c3) = (3.0f32, 5.5f32, 12.0f32);
                    a = (1.0 / hf)
                        * (0.25
                            + 1.8 * (-0.5 * (hf - c1) * (hf - c1) / 1.2).exp()
                            + 1.2 * (-0.5 * (hf - c2) * (hf - c2) / 1.6).exp()
                            + 0.7 * (-0.5 * (hf - c3) * (hf - c3) / 5.0).exp());
                }
                6 => {
                    a = if h % 3 != 0 && h <= 40 { 1.0 / hf.powf(0.7) } else { 0.0 };
                    ph = 0.37 * (h * h) as f32;
                }
                _ => a = if h % 3 == 0 && h <= 36 { 1.0 / hf.powf(0.45) } else { 0.0 },
            }
            if a == 0.0 {
                continue;
            }
            let off = (ph / tau * WT_SIZE as f32).round() as i64;
            for i in 0..WT_SIZE {
                let idx = ((h as i64 * i as i64 + off) % WT_SIZE as i64 + WT_SIZE as i64) % WT_SIZE as i64;
                x[i] += a * sin_t[idx as usize];
            }
        }
    }
    t.normalise(false);
    t.build();
    t
}

// ============================================================================================================
// Pitch detection (YIN)

/// Returns `(fundamental_hz, confidence)`; 0 Hz = none found. Confidence is 0..1 (1 - the normalised difference at
/// the chosen lag). `min_hz`/`max_hz` default to 35 / 2000 in the importer.
pub fn detect_pitch_yin(x: &[f32], sr: f64, min_hz: f32, max_hz: f32) -> (f32, f32) {
    let n = x.len() as i64;
    let tau_max = (n / 2 - 1).min((sr / min_hz as f64) as i64);
    let tau_min = 2.max((sr / max_hz as f64) as i64);
    if tau_max <= tau_min + 2 {
        return (0.0, 0.0);
    }
    let tau_max = tau_max as usize;
    let tau_min = tau_min as usize;
    let mut d = vec![0.0f32; tau_max + 1];
    let mut cm = vec![1.0f32; tau_max + 1];
    let w = x.len() - tau_max;
    for tau in 1..=tau_max {
        let mut s = 0.0f64;
        for i in 0..w {
            let diff = x[i] as f64 - x[i + tau] as f64;
            s += diff * diff;
        }
        d[tau] = s as f32;
    }
    let mut run = 0.0f64;
    for tau in 1..=tau_max {
        run += d[tau] as f64;
        cm[tau] = if run > 0.0 { (d[tau] as f64 * tau as f64 / run) as f32 } else { 1.0 };
    }
    let mut best: i64 = -1;
    let mut tau = tau_min;
    while tau < tau_max {
        if cm[tau] < 0.15 {
            while tau + 1 < tau_max && cm[tau + 1] < cm[tau] {
                tau += 1;
            }
            best = tau as i64;
            break;
        }
        tau += 1;
    }
    if best < 0 {
        // no dip under the threshold: take the global minimum, low confidence
        let mut m = 1e9f32;
        for tau in tau_min..tau_max {
            if cm[tau] < m {
                m = cm[tau];
                best = tau as i64;
            }
        }
        if best < 0 || m > 0.4 {
            return (0.0, 0.0);
        }
    }
    let best = best as usize;
    // parabolic refinement
    let mut tau_f = best as f32;
    if best > 1 && best < tau_max {
        let a = cm[best - 1];
        let b = cm[best];
        let c = cm[best + 1];
        let den = a - 2.0 * b + c;
        if den.abs() > 1e-9 {
            tau_f += 0.5 * (a - c) / den;
        }
    }
    let confidence = (1.0 - cm[best]).max(0.0);
    ((sr / tau_f as f64) as f32, confidence)
}

/// Cubic Hermite interpolation at fractional position `pos` (clamped at both ends).
pub fn hermite_at(x: &[f32], pos: f64) -> f32 {
    let n = x.len() as i64;
    if n == 0 {
        return 0.0;
    }
    let fl = pos.floor();
    let i = fl as i64;
    let f = (pos - fl) as f32;
    let at = |k: i64| x[k.clamp(0, n - 1) as usize];
    let (xm1, x0, x1, x2) = (at(i - 1), at(i), at(i + 1), at(i + 2));
    let c1 = 0.5 * (x1 - xm1);
    let c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    let c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    ((c3 * f + c2) * f + c1) * f + x0
}

#[derive(Clone, Debug, Default)]
pub struct ImportResult {
    pub table: Option<Wavetable>,
    /// one line for the user
    pub message: String,
}

fn fail(msg: &str) -> ImportResult {
    ImportResult { table: None, message: msg.to_string() }
}

/// `mode`: 0 = automatic, 1 = wavetable file (frames of 2048, 1024, 512 or 256), 2 = sampled sound (detect pitch, tune).
/// `frame_hint` is the frame length announced by the file (Serum's `clm ` chunk), 0 = none.
pub fn import_wavetable(input: &[f32], sr: f64, name: &str, mode: i32, frame_hint: i32) -> ImportResult {
    if input.len() < 64 || sr < 8000.0 {
        return fail("That file is too short to use.");
    }
    let mut t = Wavetable { name: name.to_string(), user_made: true, ..Default::default() };
    let len_in = input.len() as i64;

    // ---- a wavetable file: frames used as they are
    let mut frame_len: i64 = 0;
    let hint = frame_hint as i64;
    if mode != 2 && (64..=8192).contains(&hint) && len_in % hint == 0 && len_in / hint >= 1 {
        frame_len = hint; // the file says so
    }
    if mode != 2 && frame_len == 0 {
        for s in [2048i64, 1024, 512, 256] {
            if len_in % s == 0 && len_in / s >= 1 && len_in / s <= 512 && (mode == 1 || len_in / s >= 2 || s == 2048) {
                frame_len = s;
                break;
            }
        }
    }
    if mode == 1 && frame_len == 0 {
        frame_len = 2048;
    }

    if frame_len > 0 && len_in >= frame_len {
        let fl = frame_len as usize;
        let total = input.len() / fl;
        let frames = WT_MAX_FRAMES.min(total);
        t.frames = frames;
        t.base = vec![0.0; frames * WT_SIZE];
        for f in 0..frames {
            let src = if frames == total {
                f
            } else {
                (f as f64 * (total as f64 - 1.0) / (frames.max(2) as f64 - 1.0)).round() as usize
            };
            let one = &input[src * fl..src * fl + fl];
            for i in 0..WT_SIZE {
                t.base[f * WT_SIZE + i] =
                    if fl == WT_SIZE { one[i] } else { hermite_at(one, i as f64 * fl as f64 / WT_SIZE as f64) };
            }
        }
        t.table_file = true;
        t.normalise(false);
        t.build();
        let message = format!("{} frames, used as they are (tuned: one cycle per frame)", frames);
        return ImportResult { table: Some(t), message };
    }

    // ---- a sampled sound: find the pitch, cut into cycles
    let take = input.len().min((sr * 12.0) as usize);
    let mut x: Vec<f32> = input[..take].to_vec();
    let mut mean = 0.0f64;
    for v in &x {
        mean += *v as f64;
    }
    mean /= x.len() as f64;
    let mut peak = 0.0f32;
    for v in x.iter_mut() {
        *v -= mean as f32;
        peak = peak.max(v.abs());
    }
    if !(peak >= 1e-5) {
        return fail("That file is silent.");
    }
    for v in x.iter_mut() {
        *v /= peak;
    }
    let xl = x.len() as i64;

    // trim silence at both ends (threshold 3 %), keep the loud part
    let mut a: i64 = 0;
    let mut b: i64 = xl - 1;
    while a < b && x[a as usize].abs() < 0.03 {
        a += 1;
    }
    while b > a && x[b as usize].abs() < 0.03 {
        b -= 1;
    }
    let len = b - a + 1;
    if len < (sr * 0.02) as i64 {
        return fail("That sound is too short to find a pitch.");
    }

    // pitch: YIN on up to 9 windows spread over the sound; the median of the confident ones
    let win = (4096.0f64).max(sr / 35.0 * 2.2) as i64;
    let mut f0s: Vec<f32> = Vec::new();
    let n_win: i64 = if len > win * 2 { 9 } else { 1 };
    for k in 0..n_win {
        let start = a + if n_win == 1 { 0 } else { ((len - win) as f64 * k as f64 / (n_win - 1) as f64) as i64 };
        let n = win.min(b - start + 1);
        if n < 512 {
            continue;
        }
        let (f, conf) = detect_pitch_yin(&x[start as usize..(start + n) as usize], sr, 35.0, 2000.0);
        if f > 0.0 && conf > 0.8 {
            f0s.push(f);
        }
    }
    f0s.sort_by(|p, q| p.total_cmp(q));
    let pitched = f0s.len() >= if n_win == 1 { 1 } else { 2 };
    let mut period = 0.0f64;
    if pitched {
        let f0 = f0s[f0s.len() / 2];
        period = sr / f0 as f64;
        t.pitched = true;
        t.root_hz = f0;
        let midi = 69.0 + 12.0 * (f0 as f64 / 440.0).log2();
        t.root_note = midi.round() as i32;
        t.cents_off = ((midi - t.root_note as f64) * 100.0) as f32;
    }

    let n_frames_raw: i64 = if pitched { (len as f64 / (period * 3.0)) as i64 } else { len / WT_SIZE as i64 };
    let n_frames = n_frames_raw.min(WT_MAX_FRAMES as i64).max(1) as usize;
    t.frames = n_frames;
    t.base = vec![0.0; n_frames * WT_SIZE];

    // refine the period around p0 (+-4 %) by the normalised autocorrelation of 3 periods
    let local_period = |centre: i64, p0: f64| -> f64 {
        let span = (p0 * 3.0) as i64;
        let s0 = 0.max((xl - span - (p0 * 1.1) as i64 - 4).min(centre - span / 2));
        let mut best = p0;
        let mut best_score = -1e30f64;
        for step in -16i64..=16 {
            let p = p0 * (1.0 + 0.04 * step as f64 / 16.0);
            let (mut num, mut e1, mut e2) = (0.0f64, 0.0f64, 0.0f64);
            let mut i = 0;
            while i < span {
                let idx = s0 + i;
                if idx >= xl {
                    break;
                }
                let u = x[idx as usize] as f64;
                let v = hermite_at(&x, (idx as f64) + p) as f64;
                num += u * v;
                e1 += u * u;
                e2 += v * v;
                i += 2;
            }
            let score = num / (e1 * e2 + 1e-12).sqrt();
            if score > best_score {
                best_score = score;
                best = p;
            }
        }
        best
    };

    for f in 0..n_frames {
        let out = &mut t.base[f * WT_SIZE..(f + 1) * WT_SIZE];
        if pitched {
            let usable = len as f64 - period * 4.0;
            let frac = if n_frames == 1 { 0.5 } else { f as f64 / (n_frames - 1) as f64 };
            let centre = a + if usable > 0.0 { (usable * frac) as i64 } else { 0 } + (period * 1.5) as i64;
            let p = local_period(centre, period);
            // start at the next rising zero crossing so neighbouring frames line up in phase
            let mut s = a.max(centre);
            let limit = (xl - (p * 1.2) as i64 - 4).min(s + p as i64);
            while s < limit && s >= 0 && s + 1 < xl && !(x[s as usize] <= 0.0 && x[s as usize + 1] > 0.0) {
                s += 1;
            }
            for i in 0..WT_SIZE {
                out[i] = hermite_at(&x, s as f64 + i as f64 * p / WT_SIZE as f64);
            }
        } else {
            let want = a + if n_frames == 1 { 0 } else { ((len - WT_SIZE as i64) as f64 * f as f64 / (n_frames - 1) as f64) as i64 };
            let s = (b - WT_SIZE as i64).min(want);
            for i in 0..WT_SIZE {
                let fade = 1.0f32.min((i as f32).min((WT_SIZE - 1 - i) as f32) / 64.0);
                let idx = 0.max(s + i as i64).min(xl - 1) as usize;
                out[i] = x[idx] * fade;
            }
        }
        // make the end meet the start (a perfect period already does; this removes any small step)
        let step = out[WT_SIZE - 1] - out[0];
        for i in 0..WT_SIZE {
            out[i] -= step * i as f32 / WT_SIZE as f32;
        }
    }
    t.normalise(true);
    t.build();
    let message = if pitched {
        const NAMES: [&str; 12] = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
        let n = t.root_note;
        format!(
            "Sampled sound: pitch {}{} {:+.0} ct, tuned to the keys ({} frames)",
            NAMES[n.rem_euclid(12) as usize],
            n / 12 - 1,
            t.cents_off as f64,
            n_frames
        )
    } else {
        format!("Sampled sound without a clear pitch: loaded as {} raw frames (not tunable)", n_frames)
    };
    ImportResult { table: Some(t), message }
}

// ============================================================================================================
// WAV files
// PCM 8/16/24/32-bit and float 32/64-bit, any channel count (mixed to mono). Serum / Surge tables carry a "clm "
// chunk such as "<!>2048 10000000 wavetable": the number after <!> is the frame length.

#[derive(Clone, Debug, Default)]
pub struct WavData {
    pub mono: Vec<f32>,
    pub sample_rate: f64,
    pub frame_hint: i32,
}

fn atoi(s: &[u8]) -> i32 {
    let mut i = 0;
    while i < s.len() && (s[i] == b' ' || (9..=13).contains(&s[i])) {
        i += 1;
    }
    let mut neg = false;
    if i < s.len() && (s[i] == b'-' || s[i] == b'+') {
        neg = s[i] == b'-';
        i += 1;
    }
    let mut v: i64 = 0;
    while i < s.len() && s[i].is_ascii_digit() {
        v = (v * 10 + (s[i] - b'0') as i64).min(i32::MAX as i64);
        i += 1;
    }
    (if neg { -v } else { v }) as i32
}

pub fn decode_wav(d: &[u8]) -> Result<WavData, String> {
    let n = d.len();
    let u16_at = |o: usize| d[o] as u32 | ((d[o + 1] as u32) << 8);
    let u32_at = |o: usize| u16_at(o) | (u16_at(o + 2) << 16);
    if n < 44 || &d[0..4] != b"RIFF" || &d[8..12] != b"WAVE" {
        return Err("Not a WAV file.".into());
    }
    let (mut fmt, mut ch, mut bits, mut rate) = (0i32, 0i32, 0i32, 0u32);
    let (mut data_off, mut data_len) = (0usize, 0usize);
    let mut frame_hint = 0;
    let mut pos = 12usize;
    while pos + 8 <= n {
        let len = u32_at(pos + 4) as usize;
        let body = pos + 8;
        let avail = len.min(n - body);
        let id = &d[pos..pos + 4];
        if id == b"fmt " && avail >= 16 {
            fmt = u16_at(body) as i32;
            ch = u16_at(body + 2) as i32;
            rate = u32_at(body + 4);
            bits = u16_at(body + 14) as i32;
            if fmt == 0xFFFE && avail >= 26 {
                fmt = u16_at(body + 24) as i32; // WAVE_FORMAT_EXTENSIBLE: first two bytes of the sub-format GUID
            }
        } else if id == b"data" {
            data_off = body;
            data_len = avail;
        } else if id == b"clm " {
            let txt = &d[body..body + avail];
            if let Some(k) = txt.windows(3).position(|w| w == b"<!>") {
                frame_hint = atoi(&txt[k + 3..]);
            }
        }
        pos = body.saturating_add(len).saturating_add(len & 1);
    }
    if ch < 1 || ch > 16 || rate < 8000 || data_len == 0 {
        return Err("That WAV file has no usable audio.".into());
    }
    let is_float = fmt == 3;
    if !((fmt == 1 && matches!(bits, 8 | 16 | 24 | 32)) || (is_float && (bits == 32 || bits == 64))) {
        return Err("Unsupported WAV format (use PCM or float).".into());
    }
    let bytes = bits as usize / 8;
    let chu = ch as usize;
    let frames = data_len / (bytes * chu);
    let mut mono = vec![0.0f32; frames];
    for i in 0..frames {
        let mut acc = 0.0f64;
        for c in 0..chu {
            let o = data_off + (i * chu + c) * bytes;
            let p = &d[o..o + bytes];
            let v: f64 = if is_float && bits == 32 {
                f32::from_le_bytes([p[0], p[1], p[2], p[3]]) as f64
            } else if is_float {
                f64::from_le_bytes([p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]])
            } else if bits == 8 {
                (p[0] as i32 - 128) as f64 / 128.0
            } else if bits == 16 {
                i16::from_le_bytes([p[0], p[1]]) as f64 / 32768.0
            } else if bits == 24 {
                let x = i32::from_le_bytes([0, p[0], p[1], p[2]]) >> 8;
                x as f64 / 8388608.0
            } else {
                i32::from_le_bytes([p[0], p[1], p[2], p[3]]) as f64 / 2147483648.0
            };
            acc += v;
        }
        mono[i] = (acc / chu as f64) as f32;
    }
    Ok(WavData { mono, sample_rate: rate as f64, frame_hint })
}

/// Decode a WAV file's bytes and import it (table file or sampled sound, chosen automatically).
pub fn import_wav_bytes(d: &[u8], name: &str) -> ImportResult {
    match decode_wav(d) {
        Ok(w) => import_wavetable(&w.mono, w.sample_rate, name, 0, w.frame_hint),
        Err(e) => ImportResult { table: None, message: e },
    }
}

// ============================================================================================================
// Keeping a table inside presets and host sessions.
// "SDWT", version 1, frames, flags, root note, rootHz, centsOff, name, then the frames as 16-bit. The band-limited
// levels are rebuilt on load, so what is stored is small (64 frames = 256 kB).

pub fn serialize_wavetable(t: &Wavetable) -> Vec<u8> {
    let mut o: Vec<u8> = Vec::with_capacity(32 + t.name.len() + t.base.len() * 2);
    o.extend_from_slice(b"SDWT");
    let put32 = |o: &mut Vec<u8>, v: u32| o.extend_from_slice(&v.to_le_bytes());
    put32(&mut o, 1);
    put32(&mut o, t.frames as u32);
    put32(&mut o, (t.pitched as u32) | ((t.table_file as u32) << 1));
    put32(&mut o, t.root_note as u32);
    put32(&mut o, t.root_hz.to_bits());
    put32(&mut o, t.cents_off.to_bits());
    put32(&mut o, t.name.len() as u32);
    o.extend_from_slice(t.name.as_bytes());
    for &v in &t.base {
        let q = (v.clamp(-1.0, 1.0) * 32767.0).round() as i32 as i16;
        o.extend_from_slice(&q.to_le_bytes());
    }
    o
}

pub fn deserialize_wavetable(d: &[u8]) -> Option<Wavetable> {
    let n = d.len();
    if n < 32 || &d[0..4] != b"SDWT" {
        return None;
    }
    let u32_at = |o: usize| u32::from_le_bytes([d[o], d[o + 1], d[o + 2], d[o + 3]]);
    if u32_at(4) != 1 {
        return None;
    }
    let frames = u32_at(8) as usize;
    let flags = u32_at(12);
    let name_len = u32_at(28) as usize;
    if frames < 1 || frames > WT_MAX_FRAMES || name_len > 400 {
        return None;
    }
    let data_off = 32 + name_len;
    if n < data_off + frames * WT_SIZE * 2 {
        return None;
    }
    let mut t = Wavetable {
        frames,
        user_made: true,
        pitched: flags & 1 != 0,
        table_file: flags & 2 != 0,
        root_note: u32_at(16) as i32,
        root_hz: f32::from_bits(u32_at(20)),
        cents_off: f32::from_bits(u32_at(24)),
        name: String::from_utf8_lossy(&d[32..32 + name_len]).into_owned(),
        ..Default::default()
    };
    t.base = (0..frames * WT_SIZE)
        .map(|i| i16::from_le_bytes([d[data_off + 2 * i], d[data_off + 2 * i + 1]]) as f32 / 32767.0)
        .collect();
    t.build();
    Some(t)
}

// ============================================================================================================
#[cfg(test)]
mod tests {
    use super::*;

    struct Rng(u64);
    impl Rng {
        fn next_f64(&mut self) -> f64 {
            self.0 ^= self.0 << 13;
            self.0 ^= self.0 >> 7;
            self.0 ^= self.0 << 17;
            ((self.0 >> 11) as f64 + 0.5) / (1u64 << 53) as f64
        }
        fn normal(&mut self) -> f32 {
            let (u1, u2) = (self.next_f64(), self.next_f64());
            ((-2.0 * u1.ln()).sqrt() * (2.0 * PI * u2).cos()) as f32
        }
    }

    /// a decaying, slightly bright tone at f Hz with a little noise
    fn tone(f: f64, sr: f64, seconds: f64, noise: f32) -> Vec<f32> {
        let mut g = Rng(0x9E3779B97F4A7C15);
        (0..(sr * seconds) as usize)
            .map(|i| {
                let t = i as f64 / sr;
                let mut v = 0.0;
                for h in 1..=8 {
                    v += (2.0 * PI * f * h as f64 * t + 0.3 * h as f64).sin() / h as f64;
                }
                let n = if noise > 0.0 { noise * g.normal() } else { 0.0 };
                (v * (-1.5 * t).exp()) as f32 + n
            })
            .collect()
    }

    /// share of energy in the lowest 16 integer harmonics: a perfect one-cycle frame has everything on integer bins
    fn harmonic_purity(frame: &[f32]) -> f64 {
        let p = Wavetable::power_spectrum(frame);
        let (mut tot, mut on) = (0.0, 0.0);
        for k in 1..WT_SIZE / 2 {
            tot += p[k];
            if k <= 16 {
                on += p[k];
            }
        }
        on / tot.max(1e-12)
    }

    fn wav(x: &[f32], bits: i32, ch: usize, sr: f64, clm: Option<&str>) -> Vec<u8> {
        let mut o: Vec<u8> = Vec::new();
        let p32 = |o: &mut Vec<u8>, v: u32| o.extend_from_slice(&v.to_le_bytes());
        let p16 = |o: &mut Vec<u8>, v: u32| o.extend_from_slice(&(v as u16).to_le_bytes());
        let bytes = bits as usize / 8;
        let fmt = if bits == 32 { 3 } else { 1 };
        let data_len = x.len() * ch * bytes;
        o.extend_from_slice(b"RIFF");
        p32(&mut o, 0);
        o.extend_from_slice(b"WAVEfmt ");
        p32(&mut o, 16);
        p16(&mut o, fmt);
        p16(&mut o, ch as u32);
        p32(&mut o, sr as u32);
        p32(&mut o, (sr as usize * ch * bytes) as u32);
        p16(&mut o, (ch * bytes) as u32);
        p16(&mut o, bits as u32);
        if let Some(c) = clm {
            o.extend_from_slice(b"clm ");
            p32(&mut o, c.len() as u32);
            o.extend_from_slice(c.as_bytes());
            if c.len() & 1 == 1 {
                o.push(0);
            }
        }
        o.extend_from_slice(b"data");
        p32(&mut o, data_len as u32);
        for &v in x {
            for _ in 0..ch {
                match bits {
                    16 => p16(&mut o, (v * 32767.0).round() as i32 as u32),
                    24 => {
                        let q = (v * 8388607.0).round() as i32;
                        o.extend_from_slice(&q.to_le_bytes()[..3]);
                    }
                    _ => p32(&mut o, v.to_bits()),
                }
            }
        }
        o
    }

    #[test]
    fn builtin_table() {
        let t = make_builtin_wavetable();
        assert_eq!(t.frames, 8);
        let pk = t.base.iter().fold(0.0f32, |p, v| p.max(v.abs()));
        assert!(pk > 0.9 && pk <= 0.96, "built-in peak {pk}");
        let hf = |level: usize| -> f64 {
            let m = &t.mip[level];
            (1..WT_SIZE)
                .map(|i| {
                    let d = m[4 * (WT_SIZE + 1) + i] - m[4 * (WT_SIZE + 1) + i - 1];
                    (d * d) as f64
                })
                .sum()
        };
        assert!(hf(8) < hf(0) * 0.2, "levels not band-limited ({} vs {})", hf(8), hf(0));
        assert_eq!(Wavetable::level_for(0.0005), 0);
        assert_eq!(Wavetable::level_for(0.2), WT_LEVELS - 1);
        // reading never panics, even at odd arguments
        for &(p, ph) in &[(0.0, 0.0), (1.0, 1.0), (-1.0, -0.5), (2.0, 3.0), (0.5, 0.9999)] {
            assert!(t.read(p, ph, 3).is_finite());
        }
    }

    #[test]
    fn sampled_sounds_are_detected_and_tuned() {
        struct Case(f64, f64);
        for Case(f, sr) in [Case(110.0, 44100.0), Case(220.7, 44100.0), Case(261.0, 48000.0), Case(440.0, 48000.0), Case(733.3, 44100.0), Case(55.0, 48000.0)] {
            let r = import_wavetable(&tone(f, sr, 1.0, 0.002), sr, "test", 0, 0);
            let t = r.table.unwrap_or_else(|| panic!("import failed at {f} Hz: {}", r.message));
            let cents = 1200.0 * (t.root_hz as f64 / f).log2();
            println!("sample {f} Hz @{sr}: found {:.2} Hz ({cents:+.2} ct), {} frames, \"{}\"", t.root_hz, t.frames, r.message);
            assert!(t.pitched && cents.abs() < 0.5, "pitch {f} Hz found as {} ({cents:+.2} ct)", t.root_hz);
            assert!(t.frames >= 2, "too few frames {}", t.frames);
            let worst = (0..t.frames).map(|k| harmonic_purity(&t.base[k * WT_SIZE..(k + 1) * WT_SIZE])).fold(1.0, f64::min);
            assert!(worst > 0.97, "a frame is not one clean cycle (purity {worst:.3})");
            // the fundamental ends up in harmonic 1 of frame 0
            let p = Wavetable::power_spectrum(&t.base[..WT_SIZE]);
            let arg = (1..20).max_by(|&a, &b| p[a].total_cmp(&p[b])).unwrap();
            assert_eq!(arg, 1, "fundamental ends up in harmonic {arg}");
        }
    }

    #[test]
    fn table_file_is_used_as_is() {
        let mut x = Vec::new();
        for f in 0..4 {
            for i in 0..WT_SIZE {
                let ph = 2.0 * PI * i as f64 / WT_SIZE as f64;
                x.push((ph.sin() + (f as f64 / 3.0) * (3.0 * ph).sin()) as f32);
            }
        }
        let r = import_wavetable(&x, 44100.0, "tab", 0, 0);
        let t = r.table.expect(&r.message);
        assert!(t.table_file && t.frames == 4);
        let h3 = |pos: f32| -> f64 {
            let (mut a, mut b) = (0.0, 0.0);
            for i in 0..WT_SIZE {
                let ph = i as f64 / WT_SIZE as f64;
                let v = t.read(pos, ph as f32, 0) as f64;
                a += v * (2.0 * PI * 3.0 * ph).sin();
                b += v * (2.0 * PI * 3.0 * ph).cos();
            }
            a.hypot(b)
        };
        assert!(h3(1.0) > h3(0.0) * 3.0, "scanning does not change the table ({} vs {})", h3(0.0), h3(1.0));
    }

    #[test]
    fn noise_loads_as_raw_frames_and_rubbish_is_refused() {
        let mut g = Rng(5);
        let x: Vec<f32> = (0..44100 + 777).map(|_| 0.3 * g.normal()).collect();
        let r = import_wavetable(&x, 44100.0, "noise", 0, 0);
        let t = r.table.expect("noise should load");
        assert!(!t.pitched && t.frames >= 2, "noise should load as raw frames ({} frames, pitched {})", t.frames, t.pitched);
        assert!(import_wavetable(&vec![0.0; 10], 44100.0, "x", 0, 0).table.is_none(), "tiny file accepted");
        assert!(import_wavetable(&vec![0.0; 50000], 44100.0, "x", 2, 0).table.is_none(), "silent file accepted");
    }

    #[test]
    fn wav_files_and_preset_storage() {
        // a serum-style file: 6 frames of 2048 samples, frame k = the k-th harmonic, with a clm chunk
        let mut frames = Vec::new();
        for f in 0..6 {
            for i in 0..2048 {
                frames.push(0.8 * (6.2831853f32 * (f + 1) as f32 * i as f32 / 2048.0).sin());
            }
        }
        let a = wav(&frames, 16, 1, 44100.0, Some("<!>2048 10000000 wavetable (test)"));
        let r = import_wav_bytes(&a, "six.wav");
        let t = r.table.expect(&r.message);
        assert!(t.frames == 6 && t.table_file, "wav with clm chunk: {}", r.message);

        // frames of 1024 announced by the clm chunk, 24-bit stereo
        let mut small = Vec::new();
        for _ in 0..5 {
            for i in 0..1024 {
                small.push(0.7 * (6.2831853f32 * 2.0 * i as f32 / 1024.0).sin());
            }
        }
        let b = wav(&small, 24, 2, 48000.0, Some("<!>1024 10000000"));
        let rb = import_wav_bytes(&b, "five.wav");
        assert_eq!(rb.table.as_ref().map(|t| t.frames), Some(5), "1024-sample frames: {}", rb.message);

        // a sampled sound as a 32-bit float stereo file: pitch found, tuned
        let smp = tone(233.0, 44100.0, 1.2, 0.0);
        let c = wav(&smp, 32, 2, 44100.0, None);
        let rc = import_wav_bytes(&c, "sample.wav");
        let tc = rc.table.expect(&rc.message);
        assert!(tc.pitched && (tc.root_hz - 233.0).abs() < 1.0, "float stereo sample: {}", rc.message);
        assert!(rc.message.starts_with("Sampled sound: pitch A#3"), "{}", rc.message);

        // broken input never panics
        assert!(import_wav_bytes(&[7u8; 100], "junk").table.is_none());
        let mut trunc = a.clone();
        trunc.truncate(60);
        let _ = import_wav_bytes(&trunc, "cut");
        for cut in [0usize, 1, 11, 12, 20, 43, 44, 45, 100] {
            let _ = import_wav_bytes(&c[..cut.min(c.len())], "cut");
        }
        // a header that claims more data than the file holds
        let mut lying = c.clone();
        lying.truncate(500);
        let _ = import_wav_bytes(&lying, "lying");

        // stored in a preset and read back
        let bytes = serialize_wavetable(&tc);
        let back = deserialize_wavetable(&bytes).expect("round trip");
        assert!(back.frames == tc.frames && back.pitched == tc.pitched && back.root_note == tc.root_note && back.name == "sample.wav");
        let err = back.base.iter().zip(&tc.base).fold(0.0f32, |m, (a, b)| m.max((a - b).abs()));
        assert!(err < 1.0 / 16000.0, "round trip error {err}");
        assert!(deserialize_wavetable(&bytes[..20]).is_none(), "short blob accepted");
        let mut bad = bytes.clone();
        bad[0] = b'X';
        assert!(deserialize_wavetable(&bad).is_none());
        println!("wav + preset storage: ok ({} bytes for {} frames)", bytes.len(), tc.frames);
    }
}
