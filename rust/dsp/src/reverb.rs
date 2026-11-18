//! The twelve reverbs of Seedling, recreated after the engines of DuskVerb
//! (<https://github.com/dusk-audio/dusk-audio-plugins>, plugins/DuskVerb, GPL-3.0).
//!
//! ```text
//!    0 Plate          Dattorro cross-coupled plate
//!    1 Vintage Plate  the same tank, darker and steelier (fixed post filter, calmer modulation)
//!    2 Smooth Plate   the same tank, denser and calmer (stronger diffusion, less modulation)
//!    3 Chamber        four cross-coupled lines, no modulation, 48 output taps
//!    4 Tiled Room     sparse early-reflection front + a short, dark 4-line tail
//!    5 Hall           16-line Hadamard feedback network with 3-band decay
//!    6 Dense Hall     8-line network, allpass diffusion in every line, modulation everywhere
//!    7 Parallel Hall  six frequency bands, each with its own 4-line network and its own decay time
//!    8 Spring         three dispersive spring lines (chirp) with a little drive
//!    9 Shimmer        8-line network with an octave-up pitch shifter in the feedback path
//!   10 Reverse        rising early reflections into a dark tail (the swell builds, then decays)
//!   11 Gated          64-tap feed-forward tapped delay with a hard gate (decay = gate length)
//! ```
//!
//! Plate structure (DuskVerb's DattorroTank / DuskVerbEngine):
//! input -> pre-delay (+ slow swell) -> early reflections, and 6 short input diffusers -> two cross-coupled
//! "figure-8" tanks (modulated allpass, modulated delay, 12-stage density allpass cascade, 3-band damping,
//! static allpass, modulated delay) -> 7 signed output taps per channel. Each tank pass applies
//! `gBase = 10^(-3 * loopLength / (T60 * sr))`, split over three bands. Modulation is a smoothed random walk.
//!
//! Each engine is a compact recreation of the design (topology, delay-length families, decay maths), not a
//! bit-exact port. They are driven by Seedling's six reverb knobs (mix, attack, length, damping, chorus,
//! size) plus the reverb type. Pre-delay, swell and the dry/wet crossfade gains are done once, in [`Reverb`].
//!
//! Usage (mirrors the C++ engine): call [`Reverb::set_params`] once per block, then for each sample call
//! [`Reverb::process`] with the mono dry input; it returns the *wet* stereo pair. The caller mixes:
//! `out = dry * reverb.mix_cos + wet * reverb.mix_sin`.
//!
//! Ported from the C++ `DuskPlate.h` / `DuskVerbs.h`. GPL-3.0-or-later (see LICENSE).

use std::array::from_fn;

const PI: f32 = std::f32::consts::PI;
const TWO_PI: f32 = std::f32::consts::TAU;

#[inline]
fn clampf(v: f32, lo: f32, hi: f32) -> f32 {
    if v < lo {
        lo
    } else if v > hi {
        hi
    } else {
        v
    }
}

/// The reverb knobs, as derived by the engine (the `rvb*` fields of the C++ `Derived`).
#[derive(Clone, Copy, Debug)]
pub struct ReverbParams {
    /// Decay time in seconds (0.2 .. 12).
    pub rt60: f32,
    /// Damping 0..1 (treble multiplier = 1 - 0.85 * damp).
    pub damp: f32,
    /// Size 0..1.
    pub size: f32,
    /// Attack 0..1 (pre-delay + swell).
    pub atk: f32,
    /// Chorus / modulation depth 0..1.
    pub chor: f32,
    /// Dry/wet 0..1 (equal-power).
    pub mix: f32,
    /// Reverb type 0..11 (see module docs); larger values are clamped.
    pub kind: usize,
}

impl Default for ReverbParams {
    fn default() -> Self {
        ReverbParams { rt60: 2.0, damp: 0.5, size: 0.5, atk: 0.0, chor: 0.3, mix: 1.0, kind: 0 }
    }
}

pub const NUM_TYPES: usize = 12;
pub const TYPE_NAMES: [&str; NUM_TYPES] = [
    "Plate", "Vintage Plate", "Smooth Plate", "Chamber", "Tiled Room", "Hall",
    "Dense Hall", "Parallel Hall", "Spring", "Shimmer", "Reverse", "Gated",
];

// ---- delay line (power-of-two ring, Hermite fractional read) ------------------------------------------------------
#[derive(Clone, Default)]
struct Line {
    b: Vec<f32>,
    mask: i32,
    w: i32,
}

impl Line {
    fn init(&mut self, max_delay: i32) {
        let mut sz = 1i32;
        while sz < max_delay + 8 {
            sz <<= 1;
        }
        self.b = vec![0.0; sz as usize];
        self.mask = sz - 1;
        self.w = 0;
    }
    fn clear(&mut self) {
        for v in self.b.iter_mut() {
            *v = 0.0;
        }
        self.w = 0;
    }
    #[inline]
    fn push(&mut self, x: f32) {
        self.b[self.w as usize] = x;
        self.w = (self.w + 1) & self.mask;
    }
    /// d = 1 is the newest sample
    #[inline]
    fn at(&self, d: i32) -> f32 {
        self.b[((self.w - d) & self.mask) as usize]
    }
    /// 4-point Hermite
    #[inline]
    fn read(&self, d: f32) -> f32 {
        let d = d.max(2.0);
        let i = d as i32;
        let f = d - i as f32;
        let xm1 = self.at(i - 1);
        let x0 = self.at(i);
        let x1 = self.at(i + 1);
        let x2 = self.at(i + 2);
        let c1 = 0.5 * (x1 - xm1);
        let c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
        let c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
        ((c3 * f + c2) * f + c1) * f + x0
    }
}

/// Schroeder allpass with a (possibly fractional, possibly moving) delay.
#[derive(Clone, Default)]
struct Allpass {
    line: Line,
}

impl Allpass {
    fn init(&mut self, max_delay: i32) {
        self.line.init(max_delay);
    }
    #[inline]
    fn process(&mut self, x: f32, g: f32, len_now: f32) -> f32 {
        let delayed = self.line.read(len_now);
        let inp = x + g * delayed;
        self.line.push(inp);
        delayed - g * inp
    }
}

/// Smoothed random walk in -1..1 (smoothstep between random targets).
#[derive(Clone)]
struct Wander {
    s: u32,
    a: f32,
    b: f32,
    ph: f32,
    inc: f32,
}

impl Default for Wander {
    fn default() -> Self {
        Wander { s: 1, a: 0.0, b: 0.0, ph: 0.0, inc: 0.0 }
    }
}

impl Wander {
    fn seed(&mut self, v: u32, rate_hz: f32, sr: f32) {
        self.s = v.wrapping_mul(2654435761).wrapping_add(12345);
        self.inc = rate_hz / sr;
        self.a = self.rnd();
        self.b = self.rnd();
        self.ph = (v % 97) as f32 / 97.0;
    }
    fn rnd(&mut self) -> f32 {
        self.s ^= self.s << 13;
        self.s ^= self.s >> 17;
        self.s ^= self.s << 5;
        (self.s & 0xffffff) as f32 * (2.0 / 16777216.0) - 1.0
    }
    #[inline]
    fn next(&mut self) -> f32 {
        self.ph += self.inc;
        if self.ph >= 1.0 {
            self.ph -= 1.0;
            self.a = self.b;
            self.b = self.rnd();
        }
        let t = self.ph * self.ph * (3.0 - 2.0 * self.ph);
        self.a + (self.b - self.a) * t
    }
}

/// linear up to +-1, then eases towards +-2
#[inline]
fn soft_clip(x: f32) -> f32 {
    let a = x.abs();
    if a <= 1.0 {
        return x;
    }
    let y = 1.0 + (a - 1.0).tanh();
    if x < 0.0 {
        -y
    } else {
        y
    }
}

fn next_prime(mut n: i32) -> i32 {
    if n < 3 {
        return 3;
    }
    if n % 2 == 0 {
        n += 1;
    }
    loop {
        let mut prime = true;
        let mut d = 3;
        while d * d <= n {
            if n % d == 0 {
                prime = false;
                break;
            }
            d += 2;
        }
        if prime {
            return n;
        }
        n += 2;
    }
}

struct Rng {
    s: u32,
}

impl Rng {
    fn new(v: u32) -> Self {
        Rng { s: v.wrapping_mul(2654435761).wrapping_add(0x9E3779B9) }
    }
    /// 0..1
    fn next(&mut self) -> f32 {
        self.s ^= self.s << 13;
        self.s ^= self.s >> 17;
        self.s ^= self.s << 5;
        (self.s & 0xffffff) as f32 * (1.0 / 16777216.0)
    }
}

// ==========================================================================================================
// Plate
// ==========================================================================================================
const L_AP1: i32 = 709;
const L_D1: i32 = 4507;
const L_AP2: i32 = 1871;
const L_D2: i32 = 3769;
const R_AP1: i32 = 953;
const R_D1: i32 = 4219;
const R_AP2: i32 = 2749;
const R_D2: i32 = 3299;
const NUM_DENSITY: usize = 12;
const DENS_L: [i32; NUM_DENSITY] = [137, 199, 281, 53, 79, 113, 43, 67, 97, 131, 163, 191];
const DENS_R: [i32; NUM_DENSITY] = [149, 211, 263, 61, 89, 127, 47, 71, 101, 139, 167, 193];
const NUM_DIFF: usize = 6;
const DIFF_BASE: [i32; NUM_DIFF] = [43, 71, 103, 167, 239, 313];
const DIFF_COEFF: [f32; NUM_DIFF] = [0.65, 0.65, 0.62, 0.60, 0.60, 0.58];
const NUM_TAPS: usize = 7;

/// buf: 0 L-d1, 1 L-d2, 2 L-ap2, 3 R-d1, 4 R-d2, 5 R-ap2
#[derive(Clone, Copy)]
struct Tap {
    buf: u8,
    pos: f32,
    sign: f32,
}
const fn tap(buf: u8, pos: f32, sign: f32) -> Tap {
    Tap { buf, pos, sign }
}
const TAPS_L: [Tap; NUM_TAPS] = [
    tap(3, 0.120, 1.0), tap(3, 0.675, 1.0), tap(5, 0.480, -1.0), tap(0, 0.450, 1.0),
    tap(1, 0.540, -1.0), tap(2, 0.210, -1.0), tap(4, 0.310, -1.0),
];
const TAPS_R: [Tap; NUM_TAPS] = [
    tap(0, 0.140, 1.0), tap(0, 0.710, 1.0), tap(2, 0.520, -1.0), tap(3, 0.410, 1.0),
    tap(4, 0.580, -1.0), tap(5, 0.240, -1.0), tap(1, 0.350, -1.0),
];
/// size 1 -> 0.30 + 0.90 = 1.2 (+ headroom)
const MAX_SCALE: f32 = 1.3;
const OUT_SCALE: f32 = 1.0 / 7.0;
const LOW_XOVER: f32 = 450.0;
const HIGH_XOVER: f32 = 3800.0;
const NUM_ER: usize = 6;
const ER_L: [f32; NUM_ER] = [7.3, 13.1, 19.7, 28.3, 37.9, 47.3];
const ER_R: [f32; NUM_ER] = [9.1, 15.7, 23.9, 31.1, 41.3, 53.7];
const ER_G: [f32; NUM_ER] = [0.80, 0.65, 0.55, 0.45, 0.36, 0.28];

/// Variants of the same tank: Plate, Vintage Plate and Smooth Plate.
#[derive(Clone, Copy)]
struct PlateConfig {
    density: f32,
    diff1: f32,
    diff2: f32,
    /// added to the six input diffuser coefficients
    in_diff_add: f32,
    /// scales the tank modulation
    mod_scale: f32,
    out_gain: f32,
    er_gain: f32,
    /// post filters (0 = off)
    lp_hz: f32,
    hp_hz: f32,
}

impl Default for PlateConfig {
    fn default() -> Self {
        PlateConfig {
            density: 0.55, diff1: 0.70, diff2: 0.50, in_diff_add: 0.0, mod_scale: 1.0,
            out_gain: 1.7, er_gain: 0.4, lp_hz: 0.0, hp_hz: 0.0,
        }
    }
}

#[derive(Clone)]
struct Tank {
    ap1: Allpass,
    ap2: Allpass,
    dens: [Allpass; NUM_DENSITY],
    d1: Line,
    d2: Line,
    ap1_len: f32,
    ap2_len: f32,
    d1_len: f32,
    d2_len: f32,
    ap1_tgt: f32,
    ap2_tgt: f32,
    d1_tgt: f32,
    d2_tgt: f32,
    dens_len: [i32; NUM_DENSITY],
    lp_lo: f32,
    lp_hi: f32,
    g_low: f32,
    g_mid: f32,
    g_hi: f32,
    cross: f32,
    m_ap1: Wander,
    m_d1: Wander,
    m_d2: Wander,
}

impl Tank {
    fn new() -> Self {
        Tank {
            ap1: Allpass::default(), ap2: Allpass::default(), dens: from_fn(|_| Allpass::default()),
            d1: Line::default(), d2: Line::default(),
            ap1_len: 1.0, ap2_len: 1.0, d1_len: 1.0, d2_len: 1.0,
            ap1_tgt: 1.0, ap2_tgt: 1.0, d1_tgt: 1.0, d2_tgt: 1.0,
            dens_len: [0; NUM_DENSITY], lp_lo: 0.0, lp_hi: 0.0,
            g_low: 0.9, g_mid: 0.9, g_hi: 0.9, cross: 0.0,
            m_ap1: Wander::default(), m_d1: Wander::default(), m_d2: Wander::default(),
        }
    }
}

struct Plate {
    cfg: PlateConfig,
    post_lp_l: f32,
    post_lp_r: f32,
    post_hp_l: f32,
    post_hp_r: f32,
    lp_coef: f32,
    hp_coef: f32,
    l: Tank,
    r: Tank,
    diff: [Allpass; NUM_DIFF],
    er: Line,
    sr: f32,
    k: f32,
    mod_depth: f32,
    size: f32,
    lo_coef: f32,
    hi_coef: f32,
    flip: bool,
    fresh: bool,
}

#[allow(clippy::too_many_arguments)]
fn run_tank(
    t: &mut Tank, mod_depth: f32, k: f32, cfg: &PlateConfig, lo_coef: f32, hi_coef: f32, flip: bool, tank_in: f32,
) {
    let mod_k = mod_depth * k * cfg.mod_scale;
    t.ap1_len += (t.ap1_tgt - t.ap1_len) * 0.0005;
    t.d1_len += (t.d1_tgt - t.d1_len) * 0.0005;
    t.ap2_len += (t.ap2_tgt - t.ap2_len) * 0.0005;
    t.d2_len += (t.d2_tgt - t.d2_len) * 0.0005;

    // modulated allpass (decay diffusion 1)
    let ap1_len = t.ap1_len + t.m_ap1.next() * 14.0 * mod_k;
    let mut x = t.ap1.process(tank_in, cfg.diff1, ap1_len);

    // delay 1 (jittered read)
    let del1 = t.d1.read(t.d1_len + t.m_d1.next() * 8.0 * mod_k);
    t.d1.push(x);
    x = del1;

    // density cascade
    for i in 0..NUM_DENSITY {
        x = t.dens[i].process(x, cfg.density, t.dens_len[i] as f32);
    }

    // 3-band damping: split into low / mid / high, apply the band gains
    t.lp_lo = (1.0 - lo_coef) * x + lo_coef * t.lp_lo;
    t.lp_hi = (1.0 - hi_coef) * x + hi_coef * t.lp_hi;
    x = t.g_low * t.lp_lo + t.g_mid * (t.lp_hi - t.lp_lo) + t.g_hi * (x - t.lp_hi);

    // static allpass (decay diffusion 2), delay 2
    x = t.ap2.process(x, cfg.diff2, t.ap2_len);
    let del2 = t.d2.read(t.d2_len + t.m_d2.next() * 6.0 * mod_k);
    let bias = if flip { 1e-18 } else { -1e-18 };
    t.d2.push(x + bias);
    t.cross = soft_clip(del2).min(4.0).max(-4.0);
}

impl Plate {
    fn new() -> Self {
        Plate {
            cfg: PlateConfig::default(), post_lp_l: 0.0, post_lp_r: 0.0, post_hp_l: 0.0, post_hp_r: 0.0,
            lp_coef: 0.0, hp_coef: 0.0, l: Tank::new(), r: Tank::new(),
            diff: from_fn(|_| Allpass::default()), er: Line::default(),
            sr: 48000.0, k: 1.0884, mod_depth: 0.0, size: 0.5, lo_coef: 0.0, hi_coef: 0.0, flip: false, fresh: true,
        }
    }

    fn prepare(&mut self, sample_rate: f32) {
        self.sr = sample_rate;
        self.k = self.sr / 44100.0;
        let k = self.k;
        let cap = |base: i32| (base as f32 * k * MAX_SCALE) as i32 + 64;
        self.l.ap1.init(cap(L_AP1));
        self.l.ap2.init(cap(L_AP2));
        self.l.d1.init(cap(L_D1));
        self.l.d2.init(cap(L_D2));
        self.r.ap1.init(cap(R_AP1));
        self.r.ap2.init(cap(R_AP2));
        self.r.d1.init(cap(R_D1));
        self.r.d2.init(cap(R_D2));
        for i in 0..NUM_DENSITY {
            self.l.dens_len[i] = 2.max((DENS_L[i] as f32 * k).round() as i32);
            self.r.dens_len[i] = 2.max((DENS_R[i] as f32 * k).round() as i32);
            self.l.dens[i].init(self.l.dens_len[i] + 8);
            self.r.dens[i].init(self.r.dens_len[i] + 8);
        }
        for i in 0..NUM_DIFF {
            self.diff[i].init((DIFF_BASE[i] as f32 * k) as i32 + 8);
        }
        self.er.init((0.09 * self.sr) as i32 + 16);
        let sr = self.sr;
        self.l.m_ap1.seed(11, 0.53, sr);
        self.l.m_d1.seed(23, 0.71, sr);
        self.l.m_d2.seed(37, 0.43, sr);
        self.r.m_ap1.seed(53, 0.61, sr);
        self.r.m_d1.seed(71, 0.37, sr);
        self.r.m_d2.seed(89, 0.83, sr);
        self.lo_coef = (-TWO_PI * LOW_XOVER / sr).exp();
        self.hi_coef = (-TWO_PI * HIGH_XOVER / sr).exp();
        self.reset();
        self.fresh = true;
    }

    fn reset(&mut self) {
        for t in [&mut self.l, &mut self.r] {
            t.ap1.line.clear();
            t.ap2.line.clear();
            t.d1.clear();
            t.d2.clear();
            for a in t.dens.iter_mut() {
                a.line.clear();
            }
            t.lp_lo = 0.0;
            t.lp_hi = 0.0;
            t.cross = 0.0;
        }
        for a in self.diff.iter_mut() {
            a.line.clear();
        }
        self.er.clear();
        self.post_lp_l = 0.0;
        self.post_lp_r = 0.0;
        self.post_hp_l = 0.0;
        self.post_hp_r = 0.0;
    }

    fn snap_lengths(&mut self) {
        for t in [&mut self.l, &mut self.r] {
            t.ap1_len = t.ap1_tgt;
            t.ap2_len = t.ap2_tgt;
            t.d1_len = t.d1_tgt;
            t.d2_len = t.d2_tgt;
        }
    }

    fn set_params(&mut self, p: &ReverbParams) {
        self.size = 0.30 + 0.90 * p.size; // DuskVerb's delay-scale control
        let (k, size) = (self.k, self.size);
        let place = |t: &mut Tank, ap1: i32, d1: i32, ap2: i32, d2: i32| {
            t.ap1_tgt = (ap1 as f32 * k * size).max(4.0);
            t.d1_tgt = (d1 as f32 * k * size).max(4.0);
            t.ap2_tgt = (ap2 as f32 * k * size).max(4.0);
            t.d2_tgt = (d2 as f32 * k * size).max(4.0);
        };
        place(&mut self.l, L_AP1, L_D1, L_AP2, L_D2);
        place(&mut self.r, R_AP1, R_D1, R_AP2, R_D2);

        if self.fresh {
            self.snap_lengths(); // first call after prepare(): no glide from nothing
            self.fresh = false;
        }

        // damping knob -> treble multiply (0.15 .. 1.0); bass a little longer, as in a real plate or hall
        let bass_mul = 1.10f32;
        let mid_mul = 1.0f32;
        let treb_mul = 1.0 - 0.85 * p.damp;
        let t60 = p.rt60.max(0.1);
        let sr = self.sr;
        for t in [&mut self.l, &mut self.r] {
            let mut lp = t.ap1_tgt + t.d1_tgt + t.ap2_tgt + t.d2_tgt;
            for i in 0..NUM_DENSITY {
                lp += t.dens_len[i] as f32;
            }
            let g = 10f32.powf(-3.0 * lp / (t60 * sr));
            t.g_low = clampf(g.powf(1.0 / bass_mul), 0.001, 0.9999);
            t.g_mid = clampf(g.powf(1.0 / mid_mul), 0.001, 0.9999);
            t.g_hi = clampf(g.powf(1.0 / treb_mul), 0.001, 0.9999);
        }

        self.mod_depth = p.chor; // 0 .. 1
        self.lp_coef = if self.cfg.lp_hz > 0.0 { (-TWO_PI * self.cfg.lp_hz / sr).exp() } else { 0.0 };
        self.hp_coef = if self.cfg.hp_hz > 0.0 { (-TWO_PI * self.cfg.hp_hz / sr).exp() } else { 0.0 };
    }

    fn tap_read(&self, buf: u8, pos: f32) -> f32 {
        let (l, r) = (&self.l, &self.r);
        match buf {
            0 => l.d1.read((pos * l.d1_len).max(2.0)),
            1 => l.d2.read((pos * l.d2_len).max(2.0)),
            2 => l.ap2.line.read((pos * l.ap2_len).max(2.0)),
            3 => r.d1.read((pos * r.d1_len).max(2.0)),
            4 => r.d2.read((pos * r.d2_len).max(2.0)),
            _ => r.ap2.line.read((pos * r.ap2_len).max(2.0)),
        }
    }

    /// pre-delay and the slow swell are done by the caller
    fn process(&mut self, x: f32) -> (f32, f32) {
        let (sr, k) = (self.sr, self.k);

        // early reflections
        self.er.push(x);
        let mut e_l = 0.0;
        let mut e_r = 0.0;
        let sc = 0.5 + self.size * 0.7;
        for i in 0..NUM_ER {
            e_l += ER_G[i] * self.er.at(1.max((ER_L[i] * 0.001 * sr * sc) as i32));
            e_r += ER_G[i] * self.er.at(1.max((ER_R[i] * 0.001 * sr * sc) as i32));
        }

        // input diffusion
        let mut s = x;
        for i in 0..NUM_DIFF {
            s = self.diff[i].process(
                s,
                (DIFF_COEFF[i] + self.cfg.in_diff_add).min(0.8),
                2.max((DIFF_BASE[i] as f32 * k) as i32) as f32,
            );
        }

        // the two cross-coupled tanks (figure-8): each is fed the input plus the other's last output
        let cross_l = self.l.cross;
        let cross_r = self.r.cross;
        let (md, cfg, lo, hi, flip) = (self.mod_depth, self.cfg, self.lo_coef, self.hi_coef, self.flip);
        run_tank(&mut self.l, md, k, &cfg, lo, hi, flip, s * 0.5 + cross_r);
        run_tank(&mut self.r, md, k, &cfg, lo, hi, flip, s * 0.5 + cross_l);
        self.flip = !self.flip;

        let mut wl = 0.0;
        let mut wr = 0.0;
        for i in 0..NUM_TAPS {
            wl += TAPS_L[i].sign * self.tap_read(TAPS_L[i].buf, TAPS_L[i].pos);
            wr += TAPS_R[i].sign * self.tap_read(TAPS_R[i].buf, TAPS_R[i].pos);
        }
        let mut out_l = wl * OUT_SCALE * cfg.out_gain + e_l * cfg.er_gain * 0.25;
        let mut out_r = wr * OUT_SCALE * cfg.out_gain + e_r * cfg.er_gain * 0.25;
        if self.lp_coef > 0.0 {
            self.post_lp_l = (1.0 - self.lp_coef) * out_l + self.lp_coef * self.post_lp_l;
            self.post_lp_r = (1.0 - self.lp_coef) * out_r + self.lp_coef * self.post_lp_r;
            out_l = self.post_lp_l;
            out_r = self.post_lp_r;
        }
        if self.hp_coef > 0.0 {
            self.post_hp_l = (1.0 - self.hp_coef) * out_l + self.hp_coef * self.post_hp_l;
            self.post_hp_r = (1.0 - self.hp_coef) * out_r + self.hp_coef * self.post_hp_r;
            out_l -= self.post_hp_l;
            out_r -= self.post_hp_r;
        }
        if !out_l.is_finite() || !out_r.is_finite() {
            self.reset();
            return (0.0, 0.0);
        }
        (out_l, out_r)
    }
}

// ==========================================================================================================
// N-line feedback delay network: Hadamard mixing, per-line 3-band decay, optional in-line allpass, dark loop
// filter, modulation, input diffusion and either end-of-line or many-tap output.
// ==========================================================================================================
#[derive(Clone, Copy)]
struct FdnCfg {
    /// line lengths at size 1, spread geometrically
    len_lo_ms: f32,
    len_hi_ms: f32,
    /// allpass in every line (0 = none)
    ap_coef: f32,
    /// x chorus knob
    mod_scale: f32,
    /// decay multipliers (treble comes from the damping knob)
    bass_mul: f32,
    mid_mul: f32,
    /// extra in-loop low-pass (0 = off)
    dark_hz: f32,
    /// 0 = output the line ends; >0 = that many taps along each line
    taps_per_line: usize,
    out_gain: f32,
    in_diff: usize,
    in_diff_coef: f32,
    /// size knob -> length scale
    size_min: f32,
    size_max: f32,
    in_gain: f32,
}

impl Default for FdnCfg {
    fn default() -> Self {
        FdnCfg {
            len_lo_ms: 20.0, len_hi_ms: 70.0, ap_coef: 0.0, mod_scale: 1.0, bass_mul: 1.1, mid_mul: 1.0,
            dark_hz: 0.0, taps_per_line: 0, out_gain: 1.0, in_diff: 4, in_diff_coef: 0.6,
            size_min: 0.4, size_max: 1.6, in_gain: 0.5,
        }
    }
}

struct Fdn<const N: usize> {
    cfg: FdnCfg,
    line: [Line; N],
    ap: [Allpass; N],
    in_ap: [Allpass; 4],
    base_len: [i32; N],
    ap_len: [i32; N],
    in_len: [i32; 4],
    len: [f32; N],
    tgt: [f32; N],
    wan: [Wander; N],
    lp_lo: [f32; N],
    lp_hi: [f32; N],
    dk: [f32; N],
    g_lo: [f32; N],
    g_mid: [f32; N],
    g_hi: [f32; N],
    in_sign: [f32; N],
    out_l: [f32; N],
    out_r: [f32; N],
    tap_pos: [[f32; 8]; N],
    tap_sign: [[f32; 8]; N],
    lo_c: f32,
    hi_c: f32,
    d_c: f32,
    mod_samples: f32,
    out_norm: f32,
    sr: f32,
    k: f32,
    fresh: bool,
    flip: bool,
}

impl<const N: usize> Fdn<N> {
    fn new(sample_rate: f32, c: FdnCfg) -> Self {
        let sr = sample_rate;
        let k = sr / 44100.0;
        let mut f = Fdn::<N> {
            cfg: c,
            line: from_fn(|_| Line::default()),
            ap: from_fn(|_| Allpass::default()),
            in_ap: from_fn(|_| Allpass::default()),
            base_len: [0; N], ap_len: [0; N], in_len: [0; 4],
            len: [0.0; N], tgt: [0.0; N],
            wan: from_fn(|_| Wander::default()),
            lp_lo: [0.0; N], lp_hi: [0.0; N], dk: [0.0; N],
            g_lo: [0.0; N], g_mid: [0.0; N], g_hi: [0.0; N],
            in_sign: [0.0; N], out_l: [0.0; N], out_r: [0.0; N],
            tap_pos: [[0.0; 8]; N], tap_sign: [[0.0; 8]; N],
            lo_c: 0.0, hi_c: 0.0, d_c: 0.0, mod_samples: 0.0, out_norm: 1.0,
            sr, k, fresh: true, flip: false,
        };
        for i in 0..N {
            let fr = if N > 1 { i as f32 / (N - 1) as f32 } else { 0.0 };
            let ms = c.len_lo_ms * (c.len_hi_ms / c.len_lo_ms).powf(fr);
            f.base_len[i] = next_prime((ms * 0.001 * sr) as i32);
            f.line[i].init((f.base_len[i] as f32 * c.size_max) as i32 + 64);
            f.ap_len[i] = next_prime((sr * 0.0023 * (1.0 + 0.43 * i as f32)) as i32);
            f.ap[i].init(f.ap_len[i] + 8);
            f.wan[i].seed((101 + 17 * i) as u32, 0.23 + 0.071 * i as f32, sr);
            f.in_sign[i] = if (i * 7 + 3) % 5 < 2 { -1.0 } else { 1.0 };
            f.out_l[i] = if (i * 5 + 1) % 3 == 0 { -1.0 } else { 1.0 };
            f.out_r[i] = if (i * 3 + 2) % 4 < 2 { 1.0 } else { -1.0 };
            let mut r = Rng::new((7 + i) as u32);
            for t in 0..8 {
                f.tap_pos[i][t] = 0.06 + 0.88 * r.next();
                f.tap_sign[i][t] = if r.next() < 0.5 { -1.0 } else { 1.0 };
            }
        }
        const K_IN: [i32; 4] = [113, 167, 251, 379];
        for i in 0..4 {
            f.in_len[i] = next_prime((K_IN[i] as f32 * k) as i32);
            f.in_ap[i].init(f.in_len[i] + 8);
        }
        f.lo_c = (-TWO_PI * 450.0 / sr).exp();
        f.hi_c = (-TWO_PI * 3800.0 / sr).exp();
        f.reset();
        f
    }

    fn reset(&mut self) {
        for i in 0..N {
            self.line[i].clear();
            self.ap[i].line.clear();
            self.lp_lo[i] = 0.0;
            self.lp_hi[i] = 0.0;
            self.dk[i] = 0.0;
        }
        for a in self.in_ap.iter_mut() {
            a.line.clear();
        }
        self.fresh = true;
    }

    /// size01 0..1, chor 0..1, treb_mul from damping
    fn set_decay(&mut self, t60: f32, treb_mul: f32, size01: f32, chor: f32, bass_override: f32, mid_override: f32) {
        let c = self.cfg;
        let scale = c.size_min + (c.size_max - c.size_min) * size01;
        let bm = if bass_override > 0.0 { bass_override } else { c.bass_mul };
        let mm = if mid_override > 0.0 { mid_override } else { c.mid_mul };
        let t60 = t60.max(0.05);
        // steady-state level grows with the decay time: keep the wet level even
        self.out_norm = 1.0 / t60.max(0.3).sqrt();
        for i in 0..N {
            self.tgt[i] = (self.base_len[i] as f32 * scale).max(8.0);
            let lp = self.tgt[i] + if c.ap_coef > 0.0 { self.ap_len[i] as f32 } else { 0.0 };
            let g = 10f32.powf(-3.0 * lp / (t60 * self.sr));
            self.g_lo[i] = clampf(g.powf(1.0 / bm), 0.001, 0.9999);
            self.g_mid[i] = clampf(g.powf(1.0 / mm), 0.001, 0.9999);
            self.g_hi[i] = clampf(g.powf(1.0 / treb_mul.max(0.05)), 0.001, 0.9999);
            if self.fresh {
                self.len[i] = self.tgt[i];
            }
        }
        self.fresh = false;
        self.d_c = if c.dark_hz > 0.0 { (-TWO_PI * c.dark_hz / self.sr).exp() } else { 0.0 };
        self.mod_samples = chor * c.mod_scale * 6.0 * self.k;
    }

    fn wht(v: &mut [f32; N]) {
        let mut h = 1;
        while h < N {
            let mut i = 0;
            while i < N {
                for j in i..i + h {
                    let a = v[j];
                    let b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }
                i += h << 1;
            }
            h <<= 1;
        }
    }

    /// `extra_fb(ys)` returns a signal that replaces part of the recirculating energy (shimmer); `mix_amt` 0..1.
    fn process_with<F: FnMut(&[f32; N]) -> f32>(&mut self, x: f32, extra_fb: &mut F, mix_amt: f32) -> (f32, f32) {
        let c = self.cfg;
        let mut x = x;
        for i in 0..c.in_diff {
            x = self.in_ap[i].process(x, c.in_diff_coef, self.in_len[i] as f32);
        }

        let mut ys = [0.0f32; N];
        let mut m = [0.0f32; N];
        for i in 0..N {
            self.len[i] += (self.tgt[i] - self.len[i]) * 0.0005;
            let mut y = self.line[i].read(self.len[i] + self.wan[i].next() * self.mod_samples);
            if c.ap_coef > 0.0 {
                y = self.ap[i].process(y, c.ap_coef, self.ap_len[i] as f32);
            }
            self.lp_lo[i] = (1.0 - self.lo_c) * y + self.lo_c * self.lp_lo[i];
            self.lp_hi[i] = (1.0 - self.hi_c) * y + self.hi_c * self.lp_hi[i];
            y = self.g_lo[i] * self.lp_lo[i]
                + self.g_mid[i] * (self.lp_hi[i] - self.lp_lo[i])
                + self.g_hi[i] * (y - self.lp_hi[i]);
            if self.d_c > 0.0 {
                self.dk[i] = (1.0 - self.d_c) * y + self.d_c * self.dk[i];
                y = self.dk[i];
            }
            ys[i] = y;
            m[i] = y;
        }

        let mut wl = 0.0f32;
        let mut wr = 0.0f32;
        if c.taps_per_line > 0 {
            for i in 0..N {
                for t in 0..c.taps_per_line {
                    let v = self.tap_sign[i][t] * self.line[i].read((self.tap_pos[i][t] * self.len[i]).max(2.0));
                    if (i + t) & 1 == 1 {
                        wr += v;
                    } else {
                        wl += v;
                    }
                }
            }
            let s = 2.0 / ((N * c.taps_per_line) as f32).sqrt();
            wl *= s;
            wr *= s;
        } else {
            for i in 0..N {
                wl += self.out_l[i] * ys[i];
                wr += self.out_r[i] * ys[i];
            }
        }
        let norm = c.out_gain * self.out_norm / (N as f32).sqrt();
        let (l, r) = (wl * norm, wr * norm);

        Self::wht(&mut m);
        let sc = 1.0 / (N as f32).sqrt();
        let fb = if mix_amt > 0.0 { extra_fb(&ys) } else { 0.0 };
        let bias = if self.flip { 1e-18 } else { -1e-18 };
        self.flip = !self.flip;
        for i in 0..N {
            let mut v = m[i] * sc;
            if mix_amt > 0.0 {
                v = (1.0 - mix_amt) * v + mix_amt * fb * self.in_sign[i] * 0.9;
            }
            v += self.in_sign[i] * x * c.in_gain + bias;
            self.line[i].push(soft_clip(v).min(4.0).max(-4.0));
        }
        (l, r)
    }

    fn process(&mut self, x: f32) -> (f32, f32) {
        self.process_with(x, &mut |_| 0.0, 0.0)
    }
}

// ==========================================================================================================
// The engines
// ==========================================================================================================
#[inline]
fn t60_of(p: &ReverbParams) -> f32 {
    p.rt60.max(0.1)
}
#[inline]
fn treb_of(p: &ReverbParams) -> f32 {
    1.0 - 0.85 * p.damp
}

/// Hall: 16-line network
struct HallEngine {
    f: Fdn<16>,
}
impl HallEngine {
    fn new(sr: f32) -> Self {
        let c = FdnCfg { len_lo_ms: 22.0, len_hi_ms: 78.0, bass_mul: 1.25, mod_scale: 0.8, out_gain: 0.60, in_diff: 4, ..Default::default() };
        HallEngine { f: Fdn::new(sr, c) }
    }
    fn reset(&mut self) {
        self.f.reset();
    }
    fn set_params(&mut self, p: &ReverbParams) {
        self.f.set_decay(t60_of(p), treb_of(p), p.size, p.chor, 0.0, 0.0);
    }
    fn process(&mut self, x: f32) -> (f32, f32) {
        self.f.process(x)
    }
}

/// Dense Hall: 8 lines with an allpass in each, modulation everywhere
struct DenseHallEngine {
    f: Fdn<8>,
}
impl DenseHallEngine {
    fn new(sr: f32) -> Self {
        let c = FdnCfg {
            len_lo_ms: 26.0, len_hi_ms: 92.0, ap_coef: 0.62, bass_mul: 1.15, mod_scale: 1.6, out_gain: 0.69,
            in_diff: 4, in_diff_coef: 0.65, ..Default::default()
        };
        DenseHallEngine { f: Fdn::new(sr, c) }
    }
    fn reset(&mut self) {
        self.f.reset();
    }
    fn set_params(&mut self, p: &ReverbParams) {
        self.f.set_decay(t60_of(p), treb_of(p), p.size, p.chor, 0.0, 0.0);
    }
    fn process(&mut self, x: f32) -> (f32, f32) {
        self.f.process(x)
    }
}

/// Chamber: four lines, no modulation, 48 taps
struct ChamberEngine {
    f: Fdn<4>,
}
impl ChamberEngine {
    fn new(sr: f32) -> Self {
        let c = FdnCfg {
            len_lo_ms: 17.0, len_hi_ms: 41.0, ap_coef: 0.5, mod_scale: 0.0, taps_per_line: 8, out_gain: 0.57,
            in_diff: 3, ..Default::default()
        };
        ChamberEngine { f: Fdn::new(sr, c) }
    }
    fn reset(&mut self) {
        self.f.reset();
    }
    fn set_params(&mut self, p: &ReverbParams) {
        self.f.set_decay(t60_of(p).min(6.0), treb_of(p), p.size, 0.0, 0.0, 0.0);
    }
    fn process(&mut self, x: f32) -> (f32, f32) {
        self.f.process(x)
    }
}

/// Tiled Room: sparse early front + short dark tail
struct TiledRoomEngine {
    f: Fdn<4>,
    er: Line,
    size: f32,
    tap_l: [i32; 16],
    tap_r: [i32; 16],
    g_l: [f32; 16],
    g_r: [f32; 16],
}
impl TiledRoomEngine {
    fn new(sr: f32) -> Self {
        let c = FdnCfg {
            len_lo_ms: 5.0, len_hi_ms: 17.0, dark_hz: 1100.0, mod_scale: 0.0, out_gain: 0.42, in_diff: 2,
            size_min: 0.5, size_max: 1.5, ..Default::default()
        };
        let mut s = TiledRoomEngine {
            f: Fdn::new(sr, c), er: Line::default(), size: 0.5,
            tap_l: [0; 16], tap_r: [0; 16], g_l: [0.0; 16], g_r: [0.0; 16],
        };
        s.er.init((0.12 * sr) as i32 + 16);
        let mut r = Rng::new(31);
        for i in 0..16 {
            // front-loaded: most taps in the first 30 ms
            let t = 0.0016 + 0.050 * r.next().powf(1.4);
            let u = 0.0016 + 0.050 * r.next().powf(1.4);
            s.tap_l[i] = (t * sr) as i32;
            s.tap_r[i] = (u * sr) as i32;
            s.g_l[i] = (if r.next() < 0.5 { -1.0 } else { 1.0 }) * (0.9 - 0.6 * t / 0.052);
            s.g_r[i] = (if r.next() < 0.5 { -1.0 } else { 1.0 }) * (0.9 - 0.6 * u / 0.052);
        }
        s.reset();
        s
    }
    fn reset(&mut self) {
        self.f.reset();
        self.er.clear();
    }
    fn set_params(&mut self, p: &ReverbParams) {
        self.size = p.size;
        self.f.set_decay((t60_of(p) * 0.3).min(1.3), (treb_of(p) * 0.8).min(1.0), p.size, 0.0, 0.0, 0.0);
    }
    fn process(&mut self, x: f32) -> (f32, f32) {
        self.er.push(x);
        let mut el = 0.0;
        let mut er = 0.0;
        let sc = 0.6 + 0.8 * self.size;
        for i in 0..16 {
            el += self.g_l[i] * self.er.at(1.max((self.tap_l[i] as f32 * sc) as i32));
            er += self.g_r[i] * self.er.at(1.max((self.tap_r[i] as f32 * sc) as i32));
        }
        let (tl, tr) = self.f.process(x);
        (el * 0.09 + tl * 0.33, er * 0.09 + tr * 0.33)
    }
}

/// Parallel Hall: six bands, each its own 4-line network and decay time
struct ParallelHallEngine {
    f: [Fdn<4>; ParallelHallEngine::B],
    lp: [f32; ParallelHallEngine::B - 1],
    co: [f32; ParallelHallEngine::B - 1],
}
impl ParallelHallEngine {
    const B: usize = 6;
    const MUL: [f32; 6] = [1.35, 1.25, 1.1, 0.95, 0.75, 0.5];
    fn new(sr: f32) -> Self {
        const EDGES: [f32; 5] = [150.0, 450.0, 1200.0, 3000.0, 7000.0];
        let f: [Fdn<4>; 6] = from_fn(|b| {
            let c = FdnCfg {
                len_lo_ms: 24.0 + 3.0 * b as f32, len_hi_ms: 61.0 + 5.0 * b as f32, ap_coef: 0.55,
                mod_scale: if b < 2 { 0.0 } else { 1.0 }, out_gain: 1.0, in_diff: 2, bass_mul: 1.0, mid_mul: 1.0,
                ..Default::default()
            };
            Fdn::new(sr, c)
        });
        let co: [f32; 5] = from_fn(|i| (-TWO_PI * EDGES[i] / sr).exp());
        ParallelHallEngine { f, lp: [0.0; 5], co }
    }
    fn reset(&mut self) {
        for x in self.f.iter_mut() {
            x.reset();
        }
        self.lp = [0.0; 5];
    }
    fn set_params(&mut self, p: &ReverbParams) {
        let t60 = t60_of(p);
        let tr = treb_of(p);
        for b in 0..Self::B {
            // damping bites the upper bands
            let tilt = if b >= 3 { tr.powf((b as f32 - 2.0) / 3.0) } else { 1.0 };
            self.f[b].set_decay(t60 * Self::MUL[b] * tilt, 1.0, p.size, p.chor, 1.0, 1.0);
        }
    }
    fn process(&mut self, x: f32) -> (f32, f32) {
        let mut prev = 0.0f32;
        let mut band = [0.0f32; 6];
        for i in 0..Self::B - 1 {
            self.lp[i] = (1.0 - self.co[i]) * x + self.co[i] * self.lp[i];
            band[i] = self.lp[i] - prev;
            prev = self.lp[i];
        }
        band[Self::B - 1] = x - prev;
        let mut l = 0.0;
        let mut r = 0.0;
        for b in 0..Self::B {
            let (bl, br) = self.f[b].process(band[b]);
            l += bl;
            r += br;
        }
        (l, r)
    }
}

/// octave-up granular shifter: two crossfading read taps sweeping 1 sample per sample
struct PitchUp {
    buf: Line,
    grain: i32,
    ph: f32,
}
impl PitchUp {
    fn new(sr: f32) -> Self {
        let grain = (0.085 * sr) as i32;
        let mut buf = Line::default();
        buf.init(grain + 16);
        PitchUp { buf, grain, ph: 0.0 }
    }
    fn clear(&mut self) {
        self.buf.clear();
        self.ph = 0.0;
    }
    fn process(&mut self, x: f32) -> f32 {
        self.buf.push(x);
        self.ph += 1.0 / self.grain as f32;
        if self.ph >= 1.0 {
            self.ph -= 1.0;
        }
        let ph2 = if self.ph + 0.5 >= 1.0 { self.ph - 0.5 } else { self.ph + 0.5 };
        let w1 = 0.5 - 0.5 * (TWO_PI * self.ph).cos();
        let w2 = 0.5 - 0.5 * (TWO_PI * ph2).cos();
        let g = self.grain as f32;
        w1 * self.buf.read(g * (1.0 - self.ph)) + w2 * self.buf.read(g * (1.0 - ph2))
    }
}

/// Shimmer: 8-line network, octave up in the feedback
struct ShimmerEngine {
    f: Fdn<8>,
    pu: PitchUp,
    hp: f32,
    hp_c: f32,
}
impl ShimmerEngine {
    fn new(sr: f32) -> Self {
        let c = FdnCfg {
            len_lo_ms: 30.0, len_hi_ms: 96.0, ap_coef: 0.55, mod_scale: 1.2, out_gain: 0.86, bass_mul: 1.0,
            ..Default::default()
        };
        ShimmerEngine { f: Fdn::new(sr, c), pu: PitchUp::new(sr), hp: 0.0, hp_c: (-TWO_PI * 250.0 / sr).exp() }
    }
    fn reset(&mut self) {
        self.f.reset();
        self.pu.clear();
        self.hp = 0.0;
    }
    fn set_params(&mut self, p: &ReverbParams) {
        self.f.set_decay(t60_of(p).max(0.6), (treb_of(p) + 0.1).min(1.0), p.size, p.chor, 0.0, 0.0);
    }
    fn process(&mut self, x: f32) -> (f32, f32) {
        let Self { f, pu, hp, hp_c } = self;
        let hp_c = *hp_c;
        f.process_with(
            x,
            &mut |ys: &[f32; 8]| {
                let mut s = 0.0;
                for v in ys.iter() {
                    s += *v;
                }
                s *= 0.35355339;
                *hp = (1.0 - hp_c) * s + hp_c * *hp;
                pu.process(s - *hp)
            },
            0.28,
        )
    }
}

/// Reverse: rising early reflections, then a dark tail that peaks late
struct ReverseEngine {
    f: Fdn<8>,
    er: Line,
    dl: Line,
    size: f32,
    sr: f32,
    tp: [i32; 20],
    tg: [f32; 20],
    ts: [f32; 20],
}
impl ReverseEngine {
    fn new(sr: f32) -> Self {
        let c = FdnCfg {
            len_lo_ms: 22.0, len_hi_ms: 70.0, dark_hz: 2600.0, bass_mul: 1.0, mod_scale: 1.0, out_gain: 0.84,
            ..Default::default()
        };
        let mut s = ReverseEngine {
            f: Fdn::new(sr, c), er: Line::default(), dl: Line::default(), size: 0.5, sr,
            tp: [0; 20], tg: [0.0; 20], ts: [0.0; 20],
        };
        s.er.init((0.16 * sr) as i32 + 16);
        s.dl.init((0.16 * sr) as i32 + 16);
        let mut r = Rng::new(77);
        for i in 0..20 {
            let t = (i + 1) as f32 / 20.0;
            s.tp[i] = ((0.004 + 0.070 * t + 0.003 * r.next()) * sr) as i32;
            s.tg[i] = 0.12 + 0.88 * t; // gain rises with time: the "reverse" slope
            s.ts[i] = if r.next() < 0.5 { -1.0 } else { 1.0 };
        }
        s.reset();
        s
    }
    fn reset(&mut self) {
        self.f.reset();
        self.er.clear();
        self.dl.clear();
    }
    fn set_params(&mut self, p: &ReverbParams) {
        self.size = p.size;
        self.f.set_decay(t60_of(p).min(5.0), treb_of(p).min(1.0), p.size, p.chor, 0.0, 0.0);
    }
    fn process(&mut self, x: f32) -> (f32, f32) {
        self.er.push(x);
        self.dl.push(x);
        let sc = 0.7 + 0.6 * self.size;
        let mut el = 0.0;
        let mut er = 0.0;
        for i in 0..20 {
            let v = self.ts[i] * self.tg[i] * self.er.at(1.max((self.tp[i] as f32 * sc) as i32));
            if i & 1 == 1 {
                er += v;
            } else {
                el += v;
            }
        }
        // the tail starts where the swell peaks
        let feed = self.dl.at(1.max((0.07 * sc * self.sr) as i32));
        let (tl, tr) = self.f.process(feed);
        (el * 0.12 + tl * 0.29, er * 0.12 + tr * 0.29)
    }
}

/// Spring: three dispersive lines with feedback and a touch of drive
const SPRINGS: usize = 3;
const SPRING_K: usize = 22;
const SPRING_MS: [f32; SPRINGS] = [33.0, 38.5, 44.0];

#[derive(Clone)]
struct Spring {
    d: Line,
    len: f32,
    tgt: f32,
    fb: f32,
    lp: f32,
    z: [f32; SPRING_K],
    w: Wander,
}

struct SpringEngine {
    sp: [Spring; SPRINGS],
    sr: f32,
    lp_c: f32,
    mod_: f32,
    ap_a: f32,
    fresh: bool,
}
impl SpringEngine {
    fn new(sr: f32) -> Self {
        let sp: [Spring; SPRINGS] = from_fn(|i| {
            let mut d = Line::default();
            d.init((0.09 * sr) as i32 + 64);
            let mut w = Wander::default();
            w.seed((5 + i) as u32, 0.31 + 0.1 * i as f32, sr);
            let l = SPRING_MS[i] * 0.001 * sr;
            Spring { d, len: l, tgt: l, fb: 0.8, lp: 0.0, z: [0.0; SPRING_K], w }
        });
        SpringEngine { sp, sr, lp_c: 0.5, mod_: 0.0, ap_a: 0.62, fresh: true }
    }
    fn reset(&mut self) {
        for s in self.sp.iter_mut() {
            s.d.clear();
            s.lp = 0.0;
            for v in s.z.iter_mut() {
                *v = 0.0;
            }
        }
        self.fresh = true;
    }
    fn set_params(&mut self, p: &ReverbParams) {
        let sr = self.sr;
        let t60 = clampf(p.rt60 * 0.6, 0.25, 4.5);
        let scale = 0.7 + 0.6 * p.size;
        for i in 0..SPRINGS {
            self.sp[i].tgt = SPRING_MS[i] * 0.001 * sr * scale;
            if self.fresh {
                self.sp[i].len = self.sp[i].tgt;
            }
            self.sp[i].fb = 10f32.powf(-3.0 * (self.sp[i].tgt + SPRING_K as f32 * 4.0) / (t60 * sr));
        }
        self.fresh = false;
        self.lp_c = (-TWO_PI * (900.0 + 5200.0 * (1.0 - p.damp)) / sr).exp();
        self.mod_ = p.chor * 5.0 * sr / 48000.0;
        self.ap_a = 0.55 + 0.2 * (1.0 - p.damp);
    }
    fn process(&mut self, x: f32) -> (f32, f32) {
        let mut out = [0.0f32; SPRINGS];
        let (lp_c, ap_a, md) = (self.lp_c, self.ap_a, self.mod_);
        for i in 0..SPRINGS {
            let s = &mut self.sp[i];
            s.len += (s.tgt - s.len) * 0.0005;
            let mut y = s.d.read(s.len + s.w.next() * md);
            y = (y * 1.3).tanh() * 0.77; // the boing: a little drive in the loop
            s.lp = (1.0 - lp_c) * y + lp_c * s.lp;
            y = s.lp;
            // dispersion chain: first-order allpasses, low frequencies travel slower (the chirp)
            let mut v = x + s.fb * y;
            for k in 0..SPRING_K {
                let o = ap_a * v + s.z[k];
                s.z[k] = v - ap_a * o;
                v = o;
            }
            s.d.push(v.min(4.0).max(-4.0));
            out[i] = y;
        }
        (
            (out[0] * 1.0 + out[2] * 0.6 - out[1] * 0.3) * 0.36,
            (out[1] * 1.0 + out[2] * 0.6 - out[0] * 0.3) * 0.36,
        )
    }
}

/// Gated: 64-tap feed-forward delay with a hard gate; "decay" = gate length
struct GatedEngine {
    tdl: Line,
    a1: Allpass,
    a2: Allpass,
    sr: f32,
    gate_s: f32,
    lp_l: f32,
    lp_r: f32,
    lp_c: f32,
    pos_l: [f32; GatedEngine::T],
    pos_r: [f32; GatedEngine::T],
    sg_l: [f32; GatedEngine::T],
    sg_r: [f32; GatedEngine::T],
}
impl GatedEngine {
    const T: usize = 64;
    fn new(sr: f32) -> Self {
        let mut s = GatedEngine {
            tdl: Line::default(), a1: Allpass::default(), a2: Allpass::default(), sr, gate_s: 0.3,
            lp_l: 0.0, lp_r: 0.0, lp_c: 0.5,
            pos_l: [0.0; 64], pos_r: [0.0; 64], sg_l: [0.0; 64], sg_r: [0.0; 64],
        };
        s.tdl.init((1.4 * sr) as i32 + 16);
        s.a1.init((0.02 * sr) as i32);
        s.a2.init((0.02 * sr) as i32);
        let mut r = Rng::new(4242);
        for i in 0..Self::T {
            // jittered grid: even, but not a comb
            s.pos_l[i] = 0.004 + 0.996 * (i as f32 + r.next()) / Self::T as f32;
            s.pos_r[i] = 0.004 + 0.996 * (i as f32 + r.next()) / Self::T as f32;
            s.sg_l[i] = if r.next() < 0.5 { -1.0 } else { 1.0 };
            s.sg_r[i] = if r.next() < 0.5 { -1.0 } else { 1.0 };
        }
        s.reset();
        s
    }
    fn reset(&mut self) {
        self.tdl.clear();
        self.a1.line.clear();
        self.a2.line.clear();
        self.lp_l = 0.0;
        self.lp_r = 0.0;
    }
    fn set_params(&mut self, p: &ReverbParams) {
        self.gate_s = clampf(0.08 + 0.12 * p.rt60 * (0.6 + 0.8 * p.size), 0.08, 1.25);
        self.lp_c = (-TWO_PI * (1500.0 + 9000.0 * (1.0 - p.damp)) / self.sr).exp();
    }
    fn process(&mut self, x: f32) -> (f32, f32) {
        let sr = self.sr;
        let mut x = self.a1.process(x, 0.6, 331.0 * sr / 44100.0);
        x = self.a2.process(x, 0.55, 577.0 * sr / 44100.0);
        self.tdl.push(x);
        let mut wl = 0.0;
        let mut wr = 0.0;
        let fade = 0.012f32; // short ramp at the gate edge, not a click
        let g = self.gate_s;
        for i in 0..Self::T {
            let mut gl = 1.0;
            let mut gr = 1.0;
            if self.pos_l[i] * g > g - fade {
                gl = (g - self.pos_l[i] * g) / fade;
            }
            if self.pos_r[i] * g > g - fade {
                gr = (g - self.pos_r[i] * g) / fade;
            }
            wl += self.sg_l[i] * gl * self.tdl.at(1.max((self.pos_l[i] * g * sr) as i32));
            wr += self.sg_r[i] * gr * self.tdl.at(1.max((self.pos_r[i] * g * sr) as i32));
        }
        self.lp_l = (1.0 - self.lp_c) * wl + self.lp_c * self.lp_l;
        self.lp_r = (1.0 - self.lp_c) * wr + self.lp_c * self.lp_r;
        (self.lp_l * 0.055, self.lp_r * 0.055)
    }
}

// ==========================================================================================================
// The Seedling reverb: pre-delay + swell + type switch (with a short crossfade) + dry/wet gains.
// ==========================================================================================================
pub struct Reverb {
    plate: Plate,
    chamber: ChamberEngine,
    tiled: TiledRoomEngine,
    hall: HallEngine,
    dense: DenseHallEngine,
    par: ParallelHallEngine,
    spring: SpringEngine,
    shimmer: ShimmerEngine,
    rev: ReverseEngine,
    gated: GatedEngine,
    pre: Line,
    sr: f32,
    pre_samples: f32,
    rise_co: f32,
    env: f32,
    gate: f32,
    /// dry gain of the equal-power dry/wet crossfade (cos)
    pub mix_cos: f32,
    /// wet gain of the equal-power dry/wet crossfade (sin)
    pub mix_sin: f32,
    cur: usize,
    prev: Option<usize>,
    xf: f32,
    xf_step: f32,
}

fn is_plate(t: usize) -> bool {
    t <= 2
}

impl Reverb {
    /// Build all twelve engines for the given sample rate (allocates; do not call on the audio thread).
    pub fn new(sample_rate: f32) -> Self {
        let sr = sample_rate;
        let mut pre = Line::default();
        pre.init((0.08 * sr) as i32 + 16);
        let mut plate = Plate::new();
        plate.prepare(sr);
        let mut r = Reverb {
            plate,
            chamber: ChamberEngine::new(sr),
            tiled: TiledRoomEngine::new(sr),
            hall: HallEngine::new(sr),
            dense: DenseHallEngine::new(sr),
            par: ParallelHallEngine::new(sr),
            spring: SpringEngine::new(sr),
            shimmer: ShimmerEngine::new(sr),
            rev: ReverseEngine::new(sr),
            gated: GatedEngine::new(sr),
            pre,
            sr,
            pre_samples: 0.0,
            rise_co: 1.0,
            env: 0.0,
            gate: 1.0,
            mix_cos: 1.0,
            mix_sin: 0.0,
            cur: 0,
            prev: None,
            xf: 1.0,
            xf_step: 1.0 / (0.25 * sr),
        };
        r.apply_plate_config(0);
        r
    }

    /// Clear all tails.
    pub fn reset(&mut self) {
        self.plate.reset();
        self.chamber.reset();
        self.tiled.reset();
        self.hall.reset();
        self.dense.reset();
        self.par.reset();
        self.spring.reset();
        self.shimmer.reset();
        self.rev.reset();
        self.gated.reset();
        self.pre.clear();
        self.env = 0.0;
        self.gate = 1.0;
        self.prev = None;
        self.xf = 1.0;
    }

    /// the three plates share one tank; only the settings differ
    fn apply_plate_config(&mut self, kind: usize) {
        let mut c = PlateConfig::default();
        if kind == 1 {
            c.density = 0.50; c.diff1 = 0.68; c.diff2 = 0.45; c.mod_scale = 0.55; c.lp_hz = 5600.0; c.hp_hz = 110.0;
            c.er_gain = 0.0; c.out_gain = 3.0;
        } else if kind == 2 {
            c.density = 0.68; c.diff1 = 0.74; c.diff2 = 0.60; c.in_diff_add = 0.08; c.mod_scale = 0.5;
            c.er_gain = 0.25; c.out_gain = 1.85;
        }
        self.plate.cfg = c;
    }

    /// Apply the knobs (call once per block, or when a knob changes). A type change fades the old tail out
    /// while the new engine starts clean.
    pub fn set_params(&mut self, p: &ReverbParams) {
        let t = p.kind.min(NUM_TYPES - 1);
        if t != self.cur {
            let same_plate = is_plate(t) && is_plate(self.cur);
            if !same_plate {
                self.prev = Some(self.cur);
                self.xf = 0.0;
                self.engine_reset(t);
            }
            self.cur = t;
        }
        if is_plate(self.cur) {
            self.apply_plate_config(self.cur);
            self.plate.set_params(p);
        } else {
            self.engine_set(self.cur, p);
        }
        if let Some(pv) = self.prev {
            if !is_plate(pv) {
                self.engine_set(pv, p);
            }
        }

        let sr = self.sr;
        self.pre_samples = p.atk * 0.06 * sr;
        let tau = p.atk * 0.5 * sr;
        self.rise_co = if tau < 1.0 { 1.0 } else { 1.0 - (-1.0 / tau).exp() };
        self.mix_cos = (p.mix * PI * 0.5).cos();
        self.mix_sin = (p.mix * PI * 0.5).sin();
    }

    fn engine_reset(&mut self, t: usize) {
        match t {
            0 | 1 | 2 => {
                self.plate.reset();
                self.plate.fresh = true;
            }
            3 => self.chamber.reset(),
            4 => self.tiled.reset(),
            5 => self.hall.reset(),
            6 => self.dense.reset(),
            7 => self.par.reset(),
            8 => self.spring.reset(),
            9 => self.shimmer.reset(),
            10 => self.rev.reset(),
            _ => self.gated.reset(),
        }
    }

    fn engine_set(&mut self, t: usize, p: &ReverbParams) {
        match t {
            3 => self.chamber.set_params(p),
            4 => self.tiled.set_params(p),
            5 => self.hall.set_params(p),
            6 => self.dense.set_params(p),
            7 => self.par.set_params(p),
            8 => self.spring.set_params(p),
            9 => self.shimmer.set_params(p),
            10 => self.rev.set_params(p),
            11 => self.gated.set_params(p),
            _ => {}
        }
    }

    fn run(&mut self, t: usize, x: f32) -> (f32, f32) {
        match t {
            0 | 1 | 2 => self.plate.process(x),
            3 => self.chamber.process(x),
            4 => self.tiled.process(x),
            5 => self.hall.process(x),
            6 => self.dense.process(x),
            7 => self.par.process(x),
            8 => self.spring.process(x),
            9 => self.shimmer.process(x),
            10 => self.rev.process(x),
            _ => self.gated.process(x),
        }
    }

    /// One sample: mono dry input in, wet stereo out (pre-delay and swell applied; no dry/wet mixing,
    /// see `mix_cos` / `mix_sin`).
    pub fn process(&mut self, input: f32) -> (f32, f32) {
        self.pre.push(input);
        let mut x = self.pre.at(1.max(self.pre_samples as i32 + 1));
        // swell: the wet input fades in after silence
        self.env = input.abs().max(self.env * 0.9995);
        let target = if self.env > 0.002 { 1.0 } else { 0.0 };
        self.gate += (target - self.gate) * if target > self.gate { self.rise_co } else { 0.002 };
        x *= self.gate;

        let (mut l, mut r) = self.run(self.cur, x);
        if let Some(pv) = self.prev {
            // the old engine just rings out
            let (pl, pr) = self.run(pv, 0.0);
            let a = self.xf * self.xf * (3.0 - 2.0 * self.xf);
            l = l * a + pl * (1.0 - a);
            r = r * a + pr * (1.0 - a);
            self.xf += self.xf_step;
            if self.xf >= 1.0 {
                self.xf = 1.0;
                self.prev = None;
            }
        }
        if !l.is_finite() || !r.is_finite() {
            self.reset();
            return (0.0, 0.0);
        }
        (l, r)
    }
}

// ==========================================================================================================
#[cfg(test)]
mod tests {
    use super::*;

    /// tiny deterministic generator for the stress tests
    struct Prng(u64);
    impl Prng {
        fn u(&mut self) -> f32 {
            self.0 = self.0.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            ((self.0 >> 40) as f32) / 16777216.0
        }
        fn n(&mut self, sd: f32) -> f32 {
            // sum of 12 uniforms ~ normal
            let mut s = 0.0;
            for _ in 0..12 {
                s += self.u();
            }
            (s - 6.0) * sd
        }
    }

    fn rvb_rt60(x: f32) -> f32 {
        0.2 * 60f32.powf(x)
    }

    fn make(rt: f32, damp: f32, chor: f32, size: f32, atk: f32) -> ReverbParams {
        ReverbParams { rt60: rt, damp, size, atk, chor, mix: 1.0, kind: 0 }
    }

    /// Plate impulse response (left+right)
    fn impulse(p: &ReverbParams, sr: f32, seconds: f32) -> Vec<f32> {
        let mut pl = Plate::new();
        pl.prepare(sr);
        pl.set_params(p);
        (0..(seconds * sr) as usize)
            .map(|i| {
                let (l, r) = pl.process(if i == 0 { 1.0 } else { 0.0 });
                l + r
            })
            .collect()
    }

    /// RT60 from the Schroeder backward integral: slope between -5 and -25 dB, extrapolated to -60.
    fn rt60(h: &[f32], sr: f32) -> f32 {
        let mut e = vec![0f64; h.len()];
        let mut acc = 0f64;
        for i in (0..h.len()).rev() {
            acc += (h[i] as f64) * (h[i] as f64);
            e[i] = acc;
        }
        let (mut i5, mut i25) = (-1i64, -1i64);
        for i in 0..e.len() {
            let db = 10.0 * (e[i] / e[0] + 1e-30).log10();
            if i5 < 0 && db <= -5.0 {
                i5 = i as i64;
            }
            if i25 < 0 && db <= -25.0 {
                i25 = i as i64;
                break;
            }
        }
        if i5 < 0 || i25 < 0 {
            return -1.0;
        }
        (i25 - i5) as f32 / sr * 3.0
    }

    const SR: f32 = 48000.0;

    #[test]
    fn plate_decay_follows_length_knob() {
        for want in [0.6f32, 1.5, 4.0] {
            let got = rt60(&impulse(&make(want, 0.0, 0.0, 0.5, 0.0), SR, want * 2.5 + 2.0), SR);
            println!("T60 target {want:.1} s -> measured {got:.2} s");
            assert!(got > want * 0.6 && got < want * 1.5, "T60 {got} s for a {want} s target");
        }
    }

    #[test]
    fn damping_darkens_the_tail() {
        let hp = |h: &[f32]| {
            let mut z = 0.0;
            h.iter().map(|&v| { z += 0.4 * (v - z); v - z }).collect::<Vec<f32>>()
        };
        let late = |h: &[f32]| h[(1.0 * SR) as usize..].iter().map(|&v| (v as f64) * (v as f64)).sum::<f64>();
        let bright = late(&hp(&impulse(&make(3.0, 0.05, 0.0, 0.5, 0.0), SR, 4.0)));
        let dark = late(&hp(&impulse(&make(3.0, 0.95, 0.0, 0.5, 0.0), SR, 4.0)));
        assert!(dark < bright * 0.25, "damping did not darken the tail ({dark} vs {bright})");
    }

    #[test]
    fn size_moves_energy_later() {
        let centroid = |h: &[f32]| {
            let (mut a, mut b) = (0f64, 0f64);
            for i in 0..((0.5 * SR) as usize).min(h.len()) {
                let e = (h[i] as f64) * (h[i] as f64);
                a += i as f64 * e;
                b += e;
            }
            a / b.max(1e-30) / SR as f64
        };
        let small = centroid(&impulse(&make(1.0, 0.3, 0.0, 0.05, 0.0), SR, 1.0));
        let big = centroid(&impulse(&make(1.0, 0.3, 0.0, 0.95, 0.0), SR, 1.0));
        assert!(big > small, "bigger size should centre the energy later ({big} vs {small})");
    }

    #[test]
    fn plate_is_deterministic_and_modulation_keeps_level() {
        let p = make(2.0, 0.4, 0.6, 0.5, 0.0);
        assert!(impulse(&p, SR, 1.0) == impulse(&p, SR, 1.0), "not deterministic");
        let a = impulse(&make(2.0, 0.4, 0.0, 0.5, 0.0), SR, 2.0);
        let b = impulse(&make(2.0, 0.4, 1.0, 0.5, 0.0), SR, 2.0);
        let (mut ea, mut eb, mut diff) = (0f64, 0f64, 0f64);
        for i in 0..a.len() {
            ea += (a[i] as f64).powi(2);
            eb += (b[i] as f64).powi(2);
            diff += ((a[i] - b[i]) as f64).powi(2);
        }
        assert!(diff > 0.01 * ea, "chorus knob does nothing");
        assert!(eb > 0.5 * ea && eb < 2.0 * ea, "modulation changed the level too much");
    }

    #[test]
    fn plate_survives_stress() {
        let mut g = Prng(7);
        let mut p = Plate::new();
        p.prepare(44100.0);
        let mut peak = 0f32;
        for block in 0..2400 {
            let d = make(rvb_rt60(g.u()), g.u(), g.u(), g.u(), g.u() * 0.3);
            if block % 3 == 0 {
                p.set_params(&d);
            }
            for _ in 0..640 {
                let (l, r) = p.process(g.n(0.5));
                assert!(l.is_finite() && r.is_finite(), "NaN or inf");
                peak = peak.max(l.abs().max(r.abs()));
            }
        }
        assert!(peak < 40.0, "peak {peak}");
    }

    /// render `seconds` of one reverb type; returns (rms of the late tail or peak, impulse response if asked)
    fn render(g: &mut Prng, kind: usize, rt: f32, seconds: usize, ir: bool) -> (f64, Vec<f32>) {
        let mut d = make(rt, 0.5, 0.3, 0.5, 0.0);
        d.kind = kind;
        let mut r = Reverb::new(SR);
        r.set_params(&d);
        let (mut e, mut cnt, mut peak) = (0f64, 0u32, 0f32);
        let mut out = Vec::new();
        for i in 0..(SR as usize * seconds) {
            let inp = if ir { if i == 0 { 1.0 } else { 0.0 } } else { g.n(0.1) };
            let (l, rr) = r.process(inp);
            assert!(l.is_finite() && rr.is_finite(), "{}: NaN or inf", TYPE_NAMES[kind]);
            peak = peak.max(l.abs().max(rr.abs()));
            if ir {
                out.push(l + rr);
            } else if i > (SR * 3.0) as usize {
                e += (l as f64).powi(2) + (rr as f64).powi(2);
                cnt += 1;
            }
        }
        (if cnt > 0 { (e / cnt as f64 / 2.0).sqrt() } else { peak as f64 }, out)
    }

    #[test]
    fn all_twelve_levels_determinism_and_decay() {
        let mut g = Prng(11);
        let mut reference = 0f64;
        for t in 0..NUM_TYPES {
            let (rms, _) = render(&mut g, t, 2.0, 8, false);
            if t == 0 {
                reference = rms;
            }
            let db = 20.0 * (rms / reference).log10();
            println!("  {:<14} wet level {:+5.1} dB vs Plate", TYPE_NAMES[t], db);
            assert!(rms > 0.002, "{} is silent", TYPE_NAMES[t]);
            assert!(db.abs() < 5.0, "{} level {db:.1} dB away from the Plate", TYPE_NAMES[t]);
            let (_, a) = render(&mut g, t, 2.0, 1, true);
            let (_, b) = render(&mut g, t, 2.0, 1, true);
            assert!(a == b, "{} is not deterministic", TYPE_NAMES[t]);
        }
        for t in [0usize, 5, 6] {
            let (_, h) = render(&mut g, t, 2.5, 9, true);
            let got = rt60(&h, SR);
            println!("  {:<14} T60 target 2.5 s -> {got:.2} s", TYPE_NAMES[t]);
            assert!(got > 1.2 && got < 4.5, "{} T60 {got} s", TYPE_NAMES[t]);
        }
        // the gate ends at its gate length
        let (_, h) = render(&mut g, 11, 1.5, 3, true);
        let (mut early, mut late) = (0f64, 0f64);
        for (i, v) in h.iter().enumerate() {
            let e = (*v as f64).powi(2);
            if i > (2.0 * SR) as usize { late += e } else { early += e }
        }
        assert!(late < early * 1e-6, "the gate did not close");
        // spring: longer length rings longer
        let (_, hs) = render(&mut g, 8, 0.5, 3, true);
        let (_, hl) = render(&mut g, 8, 6.0, 3, true);
        let tail = |h: &[f32]| h[(1.5 * SR) as usize..].iter().map(|&v| (v as f64).powi(2)).sum::<f64>();
        assert!(tail(&hl) > tail(&hs) * 3.0, "spring: longer length should ring longer");
    }

    #[test]
    fn switching_types_while_playing_is_safe() {
        let mut g = Prng(13);
        let mut r = Reverb::new(SR);
        let mut peak = 0f32;
        for blk in 0..200 {
            let mut d = make(1.5, 0.4, 0.4, 0.5, 0.0);
            d.kind = (blk / 4) % NUM_TYPES;
            r.set_params(&d);
            for _ in 0..4800 {
                let (l, rr) = r.process(g.n(0.1));
                assert!(l.is_finite() && rr.is_finite());
                peak = peak.max(l.abs().max(rr.abs()));
            }
        }
        println!("  type switching: peak {peak:.2}");
        assert!(peak < 4.0, "switching types broke the reverb (peak {peak:.2})");
    }

    #[test]
    fn random_knobs_never_blow_up_on_any_type() {
        let mut g = Prng(17);
        for t in 0..NUM_TYPES {
            let mut r = Reverb::new(44100.0);
            let mut peak = 0f32;
            for _ in 0..600 {
                let mut d = make(rvb_rt60(g.u()), g.u(), g.u(), g.u(), g.u() * 0.3);
                d.kind = t;
                r.set_params(&d);
                for _ in 0..1024 {
                    let (l, rr) = r.process(g.n(0.5));
                    assert!(l.is_finite() && rr.is_finite(), "{}: NaN", TYPE_NAMES[t]);
                    peak = peak.max(l.abs().max(rr.abs()));
                }
            }
            assert!(peak < 40.0, "{}: unstable with moving knobs (peak {peak:.1})", TYPE_NAMES[t]);
        }
    }

    #[test]
    fn no_denormal_stall() {
        // a tail decaying into silence must stay fast: 3 s of silence after an impulse, per type
        for t in 0..NUM_TYPES {
            let mut d = make(0.5, 0.5, 0.0, 0.5, 0.0);
            d.kind = t;
            let mut r = Reverb::new(SR);
            r.set_params(&d);
            r.process(1.0);
            let start = std::time::Instant::now();
            for _ in 0..(3.0 * SR) as usize {
                r.process(0.0);
            }
            let secs = start.elapsed().as_secs_f32();
            assert!(secs < 3.0 * 3.0, "{} took {secs:.2} s for 3 s of silence (denormals?)", TYPE_NAMES[t]);
        }
    }
}
