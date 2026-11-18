//! Seedling synth engine (port of Engine.h / Engine.cpp): eight voices, six sound sources in the A slot,
//! dual-filter, saturation, twelve reverbs, tone controls and a soft clipper.

use crate::params::{idx, NUM_PARAMS, NUM_SOURCES, PARAMS, STEP};
use crate::reverb::{Reverb, ReverbParams};
use crate::wavetable::{make_builtin_wavetable, Wavetable};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::{Arc, Mutex};

pub const PI: f32 = std::f32::consts::PI;
pub const TWO_PI: f32 = std::f32::consts::TAU;

#[inline]
pub fn clampf(v: f32, lo: f32, hi: f32) -> f32 {
    if v < lo { lo } else if v > hi { hi } else { v }
}

/// Knob (0..1) -> real-world value mappings, shared by the engine and the UI figures.
pub mod map {
    pub fn vol_atk(x: f32) -> f32 { 0.002 * 2000f32.powf(x) }
    pub fn vol_dcy(x: f32) -> f32 { 0.010 * 500f32.powf(x) }
    pub fn mod_atk(x: f32) -> f32 { 0.001 * 2000f32.powf(x) }
    pub fn mod_dcy(x: f32) -> f32 { 0.010 * 400f32.powf(x) }
    pub fn lfo_hz(x: f32) -> f32 { 0.1 * 200f32.powf(x) }
    pub fn flt_hz(x: f32) -> f32 { 20.0 * 900f32.powf(x) }
    pub fn flt_q(x: f32) -> f32 { 0.5 * 24f32.powf(x) }
    pub fn rvb_rt60(x: f32) -> f32 { 0.2 * 60f32.powf(x) }
}

/// Everything the audio code needs, derived from the raw knob values.
#[derive(Clone, Copy, Default, Debug)]
pub struct Derived {
    pub env_scale: f32, pub env_kf: f32, pub env_loop: f32, pub atk_mul: f32, pub dcy_mul: f32,
    pub vol_atk: f32, pub vol_dcy: f32, pub vol_sus: f32, pub vol_rel: f32, pub fade_tau: f32,
    pub mod_atk: f32, pub mod_dcy: f32, pub mod_shape: f32, pub mod_vel: f32,
    pub lfo_hz: f32, pub vib_semis: f32, pub trem_depth: f32, pub lfo_delay: f32,
    pub a_form: f32, pub a_noise: f32, pub a_mod: f32, pub a_color: f32, pub a_semi: f32,
    pub fm_mod: f32, pub fm_amt: f32, pub mix_mod: f32, pub mix: f32,
    pub b_form: f32, pub b_noise: f32, pub b_mod: f32, pub sub: f32, pub b_semi: f32, pub b_hold_hz: f32,
    pub flt_type: f32, pub flt_k: f32, pub flt_hz: f32, pub flt_kf: f32, pub flt_mod: f32, pub flt_sep: f32,
    pub sat: f32, pub rvb_mix: f32, pub rvb_atk: f32, pub rvb_rt60: f32, pub rvb_damp: f32, pub rvb_chor: f32,
    pub rvb_size: f32, pub rvb_type: f32,
    pub bass: f32, pub treb: f32, pub pan: f32, pub clip: f32,
    pub src_type: f32,
    pub glide_sec: f32, pub mono: f32,
}

pub fn make_derived(p: &[f32]) -> Derived {
    let mut d = Derived::default();
    d.env_scale = 0.1 * 100f32.powf(p[idx::ENV_TIME]);
    d.env_loop = p[idx::ENV_LOOP];
    d.env_kf = p[idx::ENV_KF];
    d.atk_mul = 4f32.powf((p[idx::ENV_TILT] - 0.5) * 2.0);
    d.dcy_mul = 1.0 / d.atk_mul;

    d.vol_atk = map::vol_atk(p[idx::VOL_ATK]);
    d.vol_dcy = map::vol_dcy(p[idx::VOL_DCY]);
    d.vol_sus = p[idx::VOL_SUS];
    d.vol_rel = clampf(0.12 + 0.45 * d.vol_dcy, 0.12, 2.0);
    d.fade_tau = if p[idx::VOL_FADE] < 0.01 { 0.0 } else { 0.3 * 100f32.powf(1.0 - p[idx::VOL_FADE]) };

    d.mod_atk = map::mod_atk(p[idx::MOD_ATK]);
    d.mod_dcy = map::mod_dcy(p[idx::MOD_DCY]);
    d.mod_shape = p[idx::MOD_SH];
    d.mod_vel = p[idx::MOD_VEL];

    d.lfo_hz = map::lfo_hz(p[idx::LFO_RATE]);
    d.vib_semis = p[idx::LFO_AMT] * (1.0f32).min(2.0 * (1.0 - p[idx::LFO_BAL]));
    d.trem_depth = p[idx::LFO_AMT] * (1.0f32).min(2.0 * p[idx::LFO_BAL]);
    d.lfo_delay = p[idx::LFO_DLY] * 2.0;

    d.a_form = p[idx::A_FORM]; d.a_noise = p[idx::A_NOISE]; d.a_mod = p[idx::A_MOD];
    d.a_color = p[idx::A_COLOR]; d.a_semi = p[idx::A_FREQ];
    d.fm_mod = p[idx::FM_MOD]; d.fm_amt = p[idx::FM_AMT];
    d.mix_mod = p[idx::MIX_MOD]; d.mix = p[idx::OSC_MIX];
    d.b_form = p[idx::B_FORM]; d.b_noise = p[idx::B_NOISE]; d.b_mod = p[idx::B_MOD];
    d.sub = p[idx::SUB_AM]; d.b_semi = p[idx::B_FREQ];
    d.b_hold_hz = if p[idx::B_SH] < 0.01 { 0.0 } else { 18000.0 * 0.01f32.powf(p[idx::B_SH].powf(0.7)) };

    d.flt_type = p[idx::FLT_TYPE];
    d.flt_k = 1.0 / map::flt_q(p[idx::FLT_Q]);
    d.flt_hz = map::flt_hz(p[idx::FLT_FREQ]);
    d.flt_kf = p[idx::FLT_KF];
    d.flt_mod = p[idx::FLT_MOD];
    d.flt_sep = p[idx::FLT_SEP];

    d.sat = p[idx::SATURATE];
    d.rvb_mix = p[idx::RVB_MIX];
    d.rvb_atk = p[idx::RVB_ATK];
    d.rvb_rt60 = map::rvb_rt60(p[idx::RVB_LEN]);
    d.rvb_damp = p[idx::RVB_DAMP];
    d.rvb_chor = p[idx::RVB_CHOR];
    d.rvb_size = p[idx::RVB_SIZE];
    d.rvb_type = p[idx::RVB_TYPE];
    d.src_type = clampf(p[idx::SRC_TYPE], 0.0, (NUM_SOURCES - 1) as f32).round();

    d.glide_sec = if p[idx::GLIDE] < 0.01 { 0.0 } else { 0.004 * 750f32.powf(p[idx::GLIDE]) };
    d.mono = p[idx::MONO];
    d.bass = p[idx::ADJ_BASS]; d.treb = p[idx::ADJ_TREB]; d.pan = p[idx::ADJ_PAN]; d.clip = p[idx::ADJ_CLIP];
    d
}

// ---- oscillator shapes ------------------------------------------------------------------------------------------
#[inline]
pub fn poly_blep(mut t: f32, dt: f32) -> f32 {
    if t < dt { t /= dt; t + t - t * t - 1.0 }
    else if t > 1.0 - dt { t = (t - 1.0) / dt; t * t + t + t + 1.0 }
    else { 0.0 }
}
#[inline]
pub fn saw_shape(ph: f32, dt: f32) -> f32 {
    let mut q = ph + 0.5; q -= q.floor();
    (2.0 * q - 1.0) - poly_blep(q, dt)
}
#[inline]
pub fn tri_shape(ph: f32) -> f32 {
    if ph < 0.25 { 4.0 * ph } else if ph < 0.75 { 2.0 - 4.0 * ph } else { 4.0 * ph - 4.0 }
}
#[inline]
pub fn pulse_shape(ph: f32, w: f32, dt: f32) -> f32 {
    let mut v = if ph < w { 1.0 } else { -1.0 };
    v += poly_blep(ph, dt);
    let mut q = ph - w; q -= q.floor();
    v -= poly_blep(q, dt);
    v -= 2.0 * w - 1.0;
    v / (2.0 - 2.0 * w).max(2.0 * w)
}
pub fn shape_a(i: i32, ph: f32, dt: f32) -> f32 {
    match i {
        0 => (TWO_PI * ph).sin(),
        1 => 1.1 * tri_shape(ph),
        2 => saw_shape(ph, dt),
        _ => 0.7 * pulse_shape(ph, 0.5, dt),
    }
}
pub fn shape_b(i: i32, ph: f32, dt: f32) -> f32 {
    match i {
        0 => 0.8 * pulse_shape(ph, 0.06, dt),
        1 => 0.7 * pulse_shape(ph, 0.5, dt),
        2 => saw_shape(ph, dt),
        _ => 1.1 * tri_shape(ph),
    }
}
pub fn morph_shape(is_b: bool, form: f32, ph: f32, dt: f32) -> f32 {
    let p = clampf(form, 0.0, 1.0) * 3.0;
    let i = 2.min(p as i32);
    let f = p - i as f32;
    let a = if is_b { shape_b(i, ph, dt) } else { shape_a(i, ph, dt) };
    if f <= 0.0 { return a; }
    let b = if is_b { shape_b(i + 1, ph, dt) } else { shape_a(i + 1, ph, dt) };
    a + (b - a) * f
}
#[inline]
pub fn color_warp(ph: f32, color: f32) -> f32 {
    let c = 0.5 - 0.47 * color;
    if ph < c { 0.5 * ph / c } else { 0.5 + 0.5 * (ph - c) / (1.0 - c) }
}
pub fn saturate_curve(x: f32, amount: f32) -> f32 {
    if amount <= 0.0 { return x; }
    let g = 1.0 + 11.0 * amount;
    let wet = (g * x).tanh() / g.sqrt();
    let blend = (amount * 8.0).min(1.0);
    x + blend * (wet - x)
}
pub fn soft_clip(x: f32) -> f32 {
    let ax = x.abs();
    if ax <= 0.8 { return x; }
    let y = 0.8 + 0.2 * ((ax - 0.8) / 0.2).tanh();
    if x < 0.0 { -y } else { y }
}

// ---- biquad -----------------------------------------------------------------------------------------------------
#[derive(Clone, Copy, Debug)]
pub struct Biquad { pub b0: f32, pub b1: f32, pub b2: f32, pub a1: f32, pub a2: f32, z1: f32, z2: f32 }
impl Default for Biquad {
    fn default() -> Self { Biquad { b0: 1.0, b1: 0.0, b2: 0.0, a1: 0.0, a2: 0.0, z1: 0.0, z2: 0.0 } }
}
impl Biquad {
    #[inline]
    pub fn process(&mut self, x: f32) -> f32 {
        let y = self.b0 * x + self.z1;
        self.z1 = self.b1 * x - self.a1 * y + self.z2;
        self.z2 = self.b2 * x - self.a2 * y;
        y
    }
    pub fn reset(&mut self) { self.z1 = 0.0; self.z2 = 0.0; }
    pub fn set_low_shelf(&mut self, sr: f32, f: f32, gain_db: f32) {
        let a = 10f32.powf(gain_db / 40.0);
        let w0 = TWO_PI * f / sr; let (cw, sw) = (w0.cos(), w0.sin());
        let alpha = sw * 0.5 * 1.414_213_6;
        let tsa = 2.0 * a.sqrt() * alpha;
        let a0 = (a + 1.0) + (a - 1.0) * cw + tsa;
        self.b0 = a * ((a + 1.0) - (a - 1.0) * cw + tsa) / a0;
        self.b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cw) / a0;
        self.b2 = a * ((a + 1.0) - (a - 1.0) * cw - tsa) / a0;
        self.a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cw) / a0;
        self.a2 = ((a + 1.0) + (a - 1.0) * cw - tsa) / a0;
    }
    pub fn set_high_shelf(&mut self, sr: f32, f: f32, gain_db: f32) {
        let a = 10f32.powf(gain_db / 40.0);
        let w0 = TWO_PI * f / sr; let (cw, sw) = (w0.cos(), w0.sin());
        let alpha = sw * 0.5 * 1.414_213_6;
        let tsa = 2.0 * a.sqrt() * alpha;
        let a0 = (a + 1.0) - (a - 1.0) * cw + tsa;
        self.b0 = a * ((a + 1.0) + (a - 1.0) * cw + tsa) / a0;
        self.b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cw) / a0;
        self.b2 = a * ((a + 1.0) + (a - 1.0) * cw - tsa) / a0;
        self.a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cw) / a0;
        self.a2 = ((a + 1.0) - (a - 1.0) * cw - tsa) / a0;
    }
    pub fn copy_coeffs(&mut self, o: &Biquad) {
        self.b0 = o.b0; self.b1 = o.b1; self.b2 = o.b2; self.a1 = o.a1; self.a2 = o.a2;
    }
    pub fn magnitude(&self, freq: f32, sr: f32) -> f32 {
        let w = TWO_PI * freq / sr;
        let (c1, s1, c2, s2) = (w.cos(), w.sin(), (2.0 * w).cos(), (2.0 * w).sin());
        let nr = self.b0 + self.b1 * c1 + self.b2 * c2; let ni = -(self.b1 * s1 + self.b2 * s2);
        let dr = 1.0 + self.a1 * c1 + self.a2 * c2; let di = -(self.a1 * s1 + self.a2 * s2);
        ((nr * nr + ni * ni) / (dr * dr + di * di).max(1e-12)).sqrt()
    }
}

// ---- voice ------------------------------------------------------------------------------------------------------
const CHUNK: usize = 32;
pub const VOICES: usize = 8;
const KS_MASK: i32 = 16383;

#[derive(Clone, Copy)]
struct Rng { s: u32 }
impl Rng {
    #[inline]
    fn next(&mut self) -> f32 {
        self.s ^= self.s << 13; self.s ^= self.s >> 17; self.s ^= self.s << 5;
        (self.s as i32) as f32 * (1.0 / 2147483648.0)
    }
}

/// Zavalishin TPT state-variable filter.
#[derive(Clone, Copy, Default)]
struct Svf { ic1: f32, ic2: f32 }
impl Svf {
    fn reset(&mut self) { self.ic1 = 0.0; self.ic2 = 0.0; }
    /// type 0..1: low-pass -> band-pass -> high-pass
    #[inline]
    fn process(&mut self, x: f32, g: f32, k: f32, ty: f32) -> f32 {
        let a1 = 1.0 / (1.0 + g * (g + k));
        let a2 = g * a1; let a3 = g * a2;
        let v3 = x - self.ic2;
        let v1 = a1 * self.ic1 + a2 * v3;
        let v2 = self.ic2 + a2 * self.ic1 + a3 * v3;
        self.ic1 = 2.0 * v1 - self.ic1;
        self.ic2 = 2.0 * v2 - self.ic2;
        let lp = v2; let bp = k * v1; let hp = x - k * v1 - v2;
        let w = ty * 2.0;
        if w < 1.0 { lp + (bp - lp) * w } else { bp + (hp - bp) * (w - 1.0) }
    }
}

const FORMANTS: [[f32; 3]; 5] = [[800.0, 1150.0, 2800.0], [400.0, 1600.0, 2700.0], [350.0, 1700.0, 2700.0], [450.0, 800.0, 2830.0], [325.0, 700.0, 2530.0]];
const FORMANT_GAIN: [f32; 3] = [1.0, 0.7, 0.45];
const MODES: [[f32; 6]; 5] = [
    [1.0, 2.0, 3.0, 4.0, 5.0, 6.0], [1.0, 3.9, 9.9, 17.6, 27.6, 39.8], [1.0, 2.76, 5.4, 8.93, 13.34, 18.64],
    [0.56, 0.92, 1.19, 1.71, 2.0, 2.74], [1.0, 1.59, 2.14, 2.3, 2.65, 2.92],
];
const UNISON_OFF: [f32; 7] = [-1.0, -0.62, -0.27, 0.0, 0.27, 0.62, 1.0];

struct Voice {
    active: bool, key_down: bool, sustained: bool,
    note: i32, vel: f32, start_order: i64,
    vstage: i32, vlvl: f32, fade_gain: f32,
    glide_note: f32, mod_t: f32, age_s: f32,
    ph_a: f32, ph_b: f32, ph_sub: f32, ph_lfo: f32, hold_ph: f32, held: f32,
    n_a: f32, n_b: f32,
    rng: Rng,
    f1: Svf, f2: Svf,
    primed: bool,
    ks: Vec<f32>, ks_w: i32, ks_lp: f32, ks_c: f32, ks_g: f32,
    ph_u: [f32; 7],
    fv: [Svf; 3],
    my1: [f32; 6], my2: [f32; 6], m_c: [f32; 6], m_r2: [f32; 6], m_in: [f32; 6],
    strike_pending: bool,
}

impl Voice {
    fn new() -> Self {
        Voice {
            active: false, key_down: false, sustained: false, note: 60, vel: 1.0, start_order: 0,
            vstage: 0, vlvl: 0.0, fade_gain: 1.0, glide_note: 60.0, mod_t: 0.0, age_s: 0.0,
            ph_a: 0.0, ph_b: 0.0, ph_sub: 0.0, ph_lfo: 0.0, hold_ph: 0.0, held: 0.0, n_a: 0.0, n_b: 0.0,
            rng: Rng { s: 2463534242 }, f1: Svf::default(), f2: Svf::default(), primed: false,
            ks: vec![0.0; (KS_MASK + 1) as usize], ks_w: 0, ks_lp: 0.0, ks_c: 0.5, ks_g: 0.99,
            ph_u: [0.0; 7], fv: [Svf::default(); 3], my1: [0.0; 6], my2: [0.0; 6], m_c: [0.0; 6], m_r2: [0.0; 6], m_in: [0.0; 6],
            strike_pending: false,
        }
    }

    fn start(&mut self, n: i32, v: f32, order: i64, seed_bits: u32) {
        self.active = true; self.key_down = true; self.sustained = false;
        self.note = n; self.vel = v; self.start_order = order; self.glide_note = n as f32;
        self.vstage = 1; self.vlvl = 0.0; self.fade_gain = 1.0;
        self.mod_t = 0.0; self.age_s = 0.0;
        self.ph_a = 0.0; self.ph_b = 0.0; self.ph_sub = 0.0; self.hold_ph = 0.0; self.held = 0.0; self.ph_lfo = 0.0;
        self.n_a = 0.0; self.n_b = 0.0;
        self.rng.s = 2463534242u32 ^ seed_bits.wrapping_mul(2654435761);
        self.f1.reset(); self.f2.reset();
        self.primed = false;
        for f in self.fv.iter_mut() { f.reset(); }
        self.ks_lp = 0.0;
        for k in 0..6 { self.my1[k] = 0.0; self.my2[k] = 0.0; }
    }

    fn release(&mut self) {
        self.key_down = false; self.sustained = false;
        if self.vstage != 0 { self.vstage = 3; }
    }

    /// Set up the A-slot source the first time a note renders (the knob values are known then).
    fn prime(&mut self, d: &Derived, sr: f32, base_hz: f32) {
        let src = d.src_type as i32;
        if src == 5 {
            for k in 0..7 { self.ph_u[k] = 0.5 * (self.rng.next() + 1.0); }
        }
        if src == 2 {
            // pluck: fill the string with a (softened) noise burst
            let f = base_hz.max(20.0);
            let len = ((KS_MASK - 8) as usize).min((sr / f).ceil() as usize + 4);
            let c = 0.9 * (1.0 - d.a_noise);
            let (mut y, mut mean) = (0.0f32, 0.0f32);
            for i in 0..len { y = (1.0 - c) * self.rng.next() + c * y; self.ks[i] = y; mean += y; }
            mean /= len as f32;
            let mut e2 = 0.0f32;
            for i in 0..len { self.ks[i] -= mean; e2 += self.ks[i] * self.ks[i]; }
            let norm = 0.4 / (e2 / len as f32 + 1e-9).sqrt();
            for i in 0..len { self.ks[i] *= norm; }
            for i in len..=(KS_MASK as usize) { self.ks[i] = 0.0; }
            self.ks_w = len as i32; self.ks_lp = 0.0;
        }
        if src == 4 {
            let k = 0.6 + 0.4 * self.vel;
            for i in 0..6 { self.my1[i] = 0.0; self.my2[i] = -k / (1.0 + 0.6 * i as f32); }
            self.strike_pending = true;
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn render(&mut self, out: &mut [f32], d: &Derived, sr: f32, bend: f32, wt: &Wavetable) {
        let n = out.len();
        let note_rel = (self.note - 60) as f32;
        let vs = d.env_scale * (-d.env_kf * note_rel / 12.0).exp2();

        let atk_s = (d.vol_atk * d.atk_mul * vs * sr).max(24.0);
        let dcy_s = (d.vol_dcy * d.dcy_mul * vs * sr).max(24.0);
        let dcy_co = 1.0 - (-5.0 / dcy_s).exp();
        let rel_s = (d.vol_rel * vs * sr).max(64.0);
        let rel_co = (-5.0 / rel_s).exp();
        let fade_mul = if d.fade_tau > 0.0 { (-1.0 / (d.fade_tau * vs * sr)).exp() } else { 1.0 };

        let m_atk_s = (d.mod_atk * d.atk_mul * vs * sr).max(8.0);
        let m_dcy_s = (d.mod_dcy * d.dcy_mul * vs * sr).max(16.0);
        let m_exp = 1.0 + 5.0 * d.mod_shape;
        let loop_s = if d.env_loop > 0.02 { (0.015 * sr).max((m_atk_s + m_dcy_s) * (1.0 + 3.0 * (1.0 - d.env_loop))) } else { 0.0 };
        let vel_mod = 1.0 - d.mod_vel * (1.0 - self.vel);
        let vel_amp = 0.35 + 0.65 * self.vel;

        let mut pitch_rel = self.glide_note - 60.0;
        let base_of = |pitch_rel: f32, semi: f32| 440.0 * ((pitch_rel - 9.0 + semi + bend) / 12.0).exp2();
        let mut base_a = base_of(pitch_rel, d.a_semi);
        let mut base_b = base_of(pitch_rel, d.b_semi);
        let glide_co = if d.glide_sec > 0.0 { 1.0 - (-3.0 / (d.glide_sec * sr)).exp() } else { 1.0 };
        let lfo_inc = d.lfo_hz / sr;
        let lfo_delay_s = d.lfo_delay * sr;
        let hold_inc = if d.b_hold_hz > 0.0 { d.b_hold_hz / sr } else { 0.0 };
        let src = d.src_type as i32;
        if !self.primed { self.prime(d, sr, base_a); self.primed = true; }
        let g_sep = (d.flt_sep * 3.0).exp2();
        let mut fc_base = d.flt_hz * (d.flt_kf * pitch_rel / 12.0).exp2();
        let fc_max = 0.45 * sr;

        for i in 0..n {
            // glide
            if self.glide_note != self.note as f32 {
                self.glide_note += (self.note as f32 - self.glide_note) * glide_co;
                if (self.note as f32 - self.glide_note).abs() < 1e-3 { self.glide_note = self.note as f32; }
                pitch_rel = self.glide_note - 60.0;
                base_a = base_of(pitch_rel, d.a_semi); base_b = base_of(pitch_rel, d.b_semi);
                fc_base = d.flt_hz * (d.flt_kf * pitch_rel / 12.0).exp2();
            }

            // LFO
            self.ph_lfo += lfo_inc; if self.ph_lfo >= 1.0 { self.ph_lfo -= 1.0; }
            let ramp = if lfo_delay_s > 1.0 { (self.age_s / lfo_delay_s).min(1.0) } else { 1.0 };
            self.age_s += 1.0;
            let lfo = (TWO_PI * self.ph_lfo).sin() * ramp;

            // modulation envelope
            let mut m;
            if self.mod_t < m_atk_s { m = self.mod_t / m_atk_s; }
            else { let u = (self.mod_t - m_atk_s) / m_dcy_s; m = if u >= 1.0 { 0.0 } else { (1.0 - u).powf(m_exp) }; }
            self.mod_t += 1.0;
            if loop_s > 0.0 && self.mod_t >= loop_s { self.mod_t -= loop_s; }
            m *= vel_mod;

            // volume envelope
            match self.vstage {
                1 => { self.vlvl += 1.0 / atk_s; if self.vlvl >= 1.0 { self.vlvl = 1.0; self.vstage = 2; } }
                2 => { self.vlvl += (d.vol_sus - self.vlvl) * dcy_co; self.fade_gain *= fade_mul; }
                3 => { self.vlvl *= rel_co; }
                _ => {}
            }
            let env = self.vlvl * self.fade_gain;
            if (self.vstage == 3 && env < 1e-4) || (self.vstage == 2 && env < 1e-4) {
                self.active = false; self.vstage = 0;
                return;
            }

            // pitch
            let vib_mul = if d.vib_semis > 1e-4 { (d.vib_semis * lfo / 12.0).exp2() } else { 1.0 };
            let dt_a = (base_a * vib_mul / sr).min(0.49);
            let dt_b = (base_b * vib_mul / sr).min(0.49);

            // oscillator B (also the FM source)
            self.ph_b += dt_b; if self.ph_b >= 1.0 { self.ph_b -= 1.0; }
            let mut b = morph_shape(true, d.b_form + d.b_mod * m, self.ph_b, dt_b);
            if hold_inc > 0.0 {
                self.hold_ph += hold_inc;
                if self.hold_ph >= 1.0 { self.hold_ph -= self.hold_ph.floor(); self.held = b; }
                b = self.held;
            }
            self.n_b += 0.35 * (self.rng.next() - self.n_b);
            b += d.b_noise * 1.6 * self.n_b;
            self.ph_sub += dt_b * 0.5; if self.ph_sub >= 1.0 { self.ph_sub -= 1.0; }
            b += d.sub * (TWO_PI * self.ph_sub).sin();

            // the A slot
            let fm_depth = clampf(d.fm_amt + d.fm_mod * m, 0.0, 1.0);
            let fm_ph = fm_depth * fm_depth * 1.5 * b;
            self.n_a += 0.35 * (self.rng.next() - self.n_a);
            let n_a = self.n_a;
            let a: f32 = match src {
                1 => {
                    self.ph_a += dt_a; if self.ph_a >= 1.0 { self.ph_a -= 1.0; }
                    let mut pm = self.ph_a + fm_ph; pm -= pm.floor();
                    let pw = if d.a_color > 0.0 { color_warp(pm, d.a_color) } else { pm };
                    wt.read(d.a_form + d.a_mod * m, pw, Wavetable::level_for(dt_a)) + d.a_noise * 1.6 * n_a
                }
                2 => {
                    let f = (dt_a * sr).max(20.0);
                    if (i & 7) == 0 {
                        let bright = clampf(d.a_form + d.a_mod * m, 0.0, 1.0);
                        self.ks_c = 0.5 * (1.0 - bright) + 0.004;
                        let w0 = TWO_PI * f / sr;
                        let mag = (1.0 - 2.0 * self.ks_c * (1.0 - self.ks_c) * (1.0 - w0.cos())).max(0.25).sqrt();
                        let t = 0.1 * 120f32.powf(d.a_color);
                        self.ks_g = (0.99995f32).min((-6.9 / (t * f)).exp() / mag);
                    }
                    let (c, g) = (self.ks_c, self.ks_g);
                    let delay = (sr / f - c).max(2.0);
                    let rp = self.ks_w as f32 - delay;
                    let i0 = rp.floor() as i32;
                    let fr = rp - i0 as f32;
                    let at = |k: i32| self.ks[(k & KS_MASK) as usize];
                    let (xm1, x0, x1, x2) = (at(i0 - 1), at(i0), at(i0 + 1), at(i0 + 2));
                    let c1 = 0.5 * (x1 - xm1);
                    let c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
                    let c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
                    let y = ((c3 * fr + c2) * fr + c1) * fr + x0;
                    let w = (self.ks_w & KS_MASK) as usize;
                    self.ks[w] = ((1.0 - c) * y + c * self.ks_lp) * g;
                    self.ks_lp = y;
                    self.ks_w += 1;
                    y * 1.4
                }
                3 => {
                    self.ph_a += dt_a; if self.ph_a >= 1.0 { self.ph_a -= 1.0; }
                    let vp = clampf(d.a_form + d.a_mod * m, 0.0, 1.0) * 4.0;
                    let vi = 3.min(vp as usize);
                    let vf = vp - vi as f32;
                    let src0 = saw_shape(self.ph_a, dt_a) * (1.0 - d.a_color)
                        + 0.9 * pulse_shape(self.ph_a, 0.14, dt_a) * d.a_color + d.a_noise * 1.6 * n_a;
                    let mut v = 0.0;
                    for k in 0..3 {
                        let fc = clampf(FORMANTS[vi][k] + (FORMANTS[vi + 1][k] - FORMANTS[vi][k]) * vf, 80.0, 0.45 * sr);
                        v += FORMANT_GAIN[k] * self.fv[k].process(src0, (PI * fc / sr).tan(), 0.14, 0.5);
                    }
                    v * 1.5
                }
                4 => {
                    if (i & 7) == 0 {
                        let sp = clampf(d.a_form + d.a_mod * m, 0.0, 1.0) * 4.0;
                        let si = 3.min(sp as usize);
                        let sf = sp - si as f32;
                        let t0 = 0.05 * 160f32.powf(d.a_color);
                        let f0 = (base_a * vib_mul).max(20.0);
                        for k in 0..6 {
                            let fk = f0 * (MODES[si][k] + (MODES[si + 1][k] - MODES[si][k]) * sf);
                            let tk = t0 / (1.0 + 0.7 * k as f32);
                            let r = if fk < 0.45 * sr { (-6.9 / (tk * sr)).exp() } else { 0.0 };
                            let w = TWO_PI * fk / sr;
                            self.m_c[k] = 2.0 * r * w.cos();
                            self.m_r2[k] = r * r;
                            self.m_in[k] = (1.0 - r) * 3.0 / (1.0 + 0.6 * k as f32);
                            if self.strike_pending { self.my2[k] *= w.sin() * if r > 0.0 { 1.0 } else { 0.0 }; }
                        }
                        self.strike_pending = false;
                    }
                    let x = d.a_noise * 0.5 * n_a * 6.0;
                    let mut v = 0.0;
                    for k in 0..6 {
                        let y = self.m_c[k] * self.my1[k] - self.m_r2[k] * self.my2[k] + x * self.m_in[k];
                        self.my2[k] = self.my1[k]; self.my1[k] = y;
                        v += y;
                    }
                    v * 0.9
                }
                5 => {
                    let spread = 0.04 + 0.55 * clampf(d.a_form + d.a_mod * m, 0.0, 1.0).powf(1.5);
                    let shape_form = 0.667 + 0.333 * d.a_color;
                    let mut v = 0.0;
                    for k in 0..7 {
                        let dtk = (dt_a * (UNISON_OFF[k] * spread / 12.0).exp2()).min(0.49);
                        self.ph_u[k] += dtk; if self.ph_u[k] >= 1.0 { self.ph_u[k] -= 1.0; }
                        let mut pm = self.ph_u[k] + fm_ph; pm -= pm.floor();
                        v += morph_shape(false, shape_form, pm, dtk);
                    }
                    v * 0.42 + d.a_noise * 1.6 * n_a
                }
                _ => {
                    self.ph_a += dt_a; if self.ph_a >= 1.0 { self.ph_a -= 1.0; }
                    let mut pm = self.ph_a + fm_ph; pm -= pm.floor();
                    let pw = if d.a_color > 0.0 { color_warp(pm, d.a_color) } else { pm };
                    morph_shape(false, d.a_form + d.a_mod * m, pw, dt_a) + d.a_noise * 1.6 * n_a
                }
            };

            // mix
            let mx = clampf(d.mix + d.mix_mod * m, 0.0, 1.0);
            let mut sig = a * (1.0 - mx).sqrt() + b * mx.sqrt();

            // filter: two parallel SVFs, "separation" splits their cutoffs
            let fc1 = clampf(fc_base * (d.flt_mod * m * 4.0).exp2(), 20.0, fc_max);
            let fc2 = clampf(fc1 * g_sep, 20.0, fc_max);
            let g1 = (PI * fc1 / sr).tan(); let g2 = (PI * fc2 / sr).tan();
            sig = 0.5 * (self.f1.process(sig, g1, d.flt_k, d.flt_type) + self.f2.process(sig, g2, d.flt_k, d.flt_type));

            // amp
            let trem = 1.0 - d.trem_depth * 0.5 * (1.0 - lfo);
            out[i] += sig * env * vel_amp * trem * 0.25;
        }
    }
}

// ---- user wavetable hand-off between the UI thread and the audio thread -----------------------------------------
/// Shared slot holding the user wavetable. The UI thread `set`s it; the audio thread polls `version` (an atomic) each
/// block and only then `try_lock`s, so it never waits.
#[derive(Clone)]
pub struct WavetableSlot {
    inner: Arc<(Mutex<Option<Arc<Wavetable>>>, AtomicU32)>,
}
impl Default for WavetableSlot {
    fn default() -> Self { WavetableSlot { inner: Arc::new((Mutex::new(None), AtomicU32::new(0))) } }
}
impl WavetableSlot {
    pub fn new() -> Self { Self::default() }
    pub fn set(&self, t: Option<Arc<Wavetable>>) {
        if let Ok(mut g) = self.inner.0.lock() { *g = t; }
        self.inner.1.fetch_add(1, Ordering::Release);
    }
    pub fn get(&self) -> Option<Arc<Wavetable>> { self.inner.0.lock().ok().and_then(|g| g.clone()) }
    pub fn version(&self) -> u32 { self.inner.1.load(Ordering::Acquire) }
    fn try_get(&self) -> Option<Option<Arc<Wavetable>>> { self.inner.0.try_lock().ok().map(|g| g.clone()) }
}

// ---- engine -----------------------------------------------------------------------------------------------------
pub struct Engine {
    sr: f32,
    tgt: [f32; NUM_PARAMS], cur: [f32; NUM_PARAMS],
    snap: bool,
    d: Derived,
    voices: Vec<Voice>,
    order: i64,
    bend: f32,
    sustain: bool,
    held: Vec<i32>,
    last_pitch: f32,
    mix_buf: [f32; CHUNK],
    dc_x: f32, dc_y: f32,
    builtin: Arc<Wavetable>,
    user: Option<Arc<Wavetable>>,
    slot: WavetableSlot,
    slot_version: u32,
    rvb: Reverb,
    low_l: Biquad, low_r: Biquad, high_l: Biquad, high_r: Biquad,
    last_bass: f32, last_treb: f32,
}

impl Engine {
    pub fn new(sample_rate: f32) -> Self {
        let mut tgt = [0.0; NUM_PARAMS];
        for (i, p) in PARAMS.iter().enumerate() { tgt[i] = p.def; }
        let mut e = Engine {
            sr: sample_rate, tgt, cur: tgt, snap: true, d: Derived::default(),
            voices: (0..VOICES).map(|_| Voice::new()).collect(),
            order: 0, bend: 0.0, sustain: false, held: Vec::with_capacity(256), last_pitch: -1.0,
            mix_buf: [0.0; CHUNK], dc_x: 0.0, dc_y: 0.0,
            builtin: Arc::new(make_builtin_wavetable()), user: None,
            slot: WavetableSlot::new(), slot_version: 0,
            rvb: Reverb::new(sample_rate),
            low_l: Biquad::default(), low_r: Biquad::default(), high_l: Biquad::default(), high_r: Biquad::default(),
            last_bass: 1e9, last_treb: 1e9,
        };
        e.reset();
        e
    }

    /// Change the sample rate (allocates; call outside the audio callback).
    pub fn prepare(&mut self, sample_rate: f32) {
        self.sr = sample_rate;
        self.rvb = Reverb::new(sample_rate);
        self.reset();
    }

    pub fn reset(&mut self) {
        for v in self.voices.iter_mut() { v.active = false; v.vstage = 0; }
        self.held.clear(); self.last_pitch = -1.0;
        self.dc_x = 0.0; self.dc_y = 0.0;
        self.rvb.reset();
        for b in [&mut self.low_l, &mut self.low_r, &mut self.high_l, &mut self.high_r] { b.reset(); }
        self.last_bass = 1e9; self.last_treb = 1e9;
        self.snap = true;
    }

    /// kNumParams real-world values.
    pub fn set_params(&mut self, values: &[f32]) { self.tgt.copy_from_slice(&values[..NUM_PARAMS]); }

    /// Hand the engine the slot the UI thread puts custom wavetables into.
    pub fn set_wavetable_slot(&mut self, slot: WavetableSlot) {
        self.user = slot.get();
        self.slot_version = slot.version();
        self.slot = slot;
    }

    pub fn set_pitch_bend(&mut self, semitones: f32) { self.bend = semitones; }
    pub fn set_sustain(&mut self, down: bool) {
        self.sustain = down;
        if !down { for v in self.voices.iter_mut() { if v.active && v.sustained { v.release(); } } }
    }
    pub fn all_notes_off(&mut self) {
        for v in self.voices.iter_mut() { if v.active { v.release(); } }
        self.sustain = false; self.held.clear();
    }

    fn mono_mode(&self) -> bool { self.tgt[idx::MONO] >= 0.5 }

    fn mono_note_on(&mut self, note: i32, v: f32) {
        self.held.retain(|&n| n != note);
        let legato = !self.held.is_empty() && self.voices[0].active && self.voices[0].key_down;
        self.held.push(note);
        for i in 1..self.voices.len() { if self.voices[i].active { self.voices[i].release(); } }
        let v = clampf(v, 0.0, 1.0);
        if legato { self.voices[0].note = note; self.voices[0].vel = v; return; }

        let (was_active, old_lvl, old_ga, old_pa, old_pb, old_ps, old_f1, old_f2) = {
            let o = &self.voices[0];
            (o.active, o.vlvl, o.fade_gain, o.ph_a, o.ph_b, o.ph_sub, o.f1, o.f2)
        };
        self.order += 1;
        let order = self.order;
        let last_pitch = self.last_pitch;
        let vo = &mut self.voices[0];
        vo.start(note, v, order, order as u32);
        vo.glide_note = if last_pitch >= 0.0 { last_pitch } else { note as f32 };
        if was_active {
            vo.vlvl = old_lvl * old_ga;
            vo.ph_a = old_pa; vo.ph_b = old_pb; vo.ph_sub = old_ps;
            vo.f1 = old_f1; vo.f2 = old_f2;
        }
    }

    fn mono_note_off(&mut self, note: i32) {
        self.held.retain(|&n| n != note);
        if !self.voices[0].active { return; }
        if let Some(&last) = self.held.last() { self.voices[0].note = last; return; }
        if self.voices[0].key_down {
            if self.sustain { self.voices[0].key_down = false; self.voices[0].sustained = true; }
            else { self.voices[0].release(); }
        }
    }

    pub fn note_on(&mut self, note: i32, vel: f32) {
        if self.mono_mode() { self.mono_note_on(note, vel); return; }
        self.held.clear();
        let mut pick: Option<usize> = None;
        for (i, v) in self.voices.iter().enumerate() { if v.active && v.note == note { pick = Some(i); break; } }
        if pick.is_none() { for (i, v) in self.voices.iter().enumerate() { if !v.active { pick = Some(i); break; } } }
        if pick.is_none() {
            for (i, v) in self.voices.iter().enumerate() {
                if v.vstage == 3 && pick.map_or(true, |p| v.start_order < self.voices[p].start_order) { pick = Some(i); }
            }
            if pick.is_none() {
                let mut p = 0;
                for (i, v) in self.voices.iter().enumerate() { if v.start_order < self.voices[p].start_order { p = i; } }
                pick = Some(p);
            }
        }
        self.order += 1;
        let order = self.order;
        self.voices[pick.unwrap()].start(note, clampf(vel, 0.0, 1.0), order, order as u32);
    }

    pub fn note_off(&mut self, note: i32) {
        if self.mono_mode() { self.mono_note_off(note); return; }
        let sustain = self.sustain;
        for v in self.voices.iter_mut() {
            if v.active && v.key_down && v.note == note {
                if sustain { v.key_down = false; v.sustained = true; } else { v.release(); }
            }
        }
    }

    fn update_params(&mut self, chunk: usize) {
        let co = 1.0 - (-(chunk as f32) / (self.sr * 0.015)).exp();
        for i in 0..NUM_PARAMS {
            if self.snap || (PARAMS[i].flags & STEP) != 0 { self.cur[i] = self.tgt[i]; continue; }
            let diff = self.tgt[i] - self.cur[i];
            let range = PARAMS[i].hi - PARAMS[i].lo;
            self.cur[i] = if diff.abs() < 1e-4 * range { self.tgt[i] } else { self.cur[i] + diff * co };
        }
        self.snap = false;
        self.d = make_derived(&self.cur);
        let d = self.d;
        self.rvb.set_params(&ReverbParams {
            rt60: d.rvb_rt60, damp: d.rvb_damp, size: d.rvb_size, atk: d.rvb_atk, chor: d.rvb_chor, mix: d.rvb_mix,
            kind: d.rvb_type.round().max(0.0) as usize,
        });
        if d.bass != self.last_bass {
            self.low_l.set_low_shelf(self.sr, 150.0, d.bass * 12.0);
            let c = self.low_l; self.low_r.copy_coeffs(&c);
            self.last_bass = d.bass;
        }
        if d.treb != self.last_treb {
            self.high_l.set_high_shelf(self.sr, 6000.0, d.treb * 12.0);
            let c = self.high_l; self.high_r.copy_coeffs(&c);
            self.last_treb = d.treb;
        }
    }

    /// Renders `l.len()` samples (overwrites both channels).
    pub fn render(&mut self, l_out: &mut [f32], r_out: &mut [f32]) {
        let n = l_out.len().min(r_out.len());
        if self.slot.version() != self.slot_version {
            if let Some(t) = self.slot.try_get() { self.user = t; self.slot_version = self.slot.version(); }
        }
        let mut pos = 0;
        while pos < n {
            let c = CHUNK.min(n - pos);
            self.update_params(c);
            self.mix_buf = [0.0; CHUNK];
            let d = self.d;
            let wt: &Wavetable = self.user.as_deref().unwrap_or(&*self.builtin);
            for v in self.voices.iter_mut() {
                if v.active { v.render(&mut self.mix_buf[..c], &d, self.sr, self.bend, wt); }
            }
            if self.mono_mode() && self.voices[0].active { self.last_pitch = self.voices[0].glide_note; }

            let pan_a = (d.pan + 1.0) * PI * 0.25;
            let (g_l, g_r) = (pan_a.cos() * 1.414_213_6, pan_a.sin() * 1.414_213_6);
            let pre = 1.0 + 4.0 * d.clip;
            let post = 1.0 / pre.sqrt();

            for i in 0..c {
                let x0 = self.mix_buf[i];
                self.dc_y = x0 - self.dc_x + 0.995 * self.dc_y; self.dc_x = x0;
                let x = saturate_curve(self.dc_y, d.sat);

                let (wl, wr) = self.rvb.process(x);
                let mut l = x * self.rvb.mix_cos + wl * self.rvb.mix_sin;
                let mut r = x * self.rvb.mix_cos + wr * self.rvb.mix_sin;

                l = self.high_l.process(self.low_l.process(l));
                r = self.high_r.process(self.low_r.process(r));
                l *= g_l; r *= g_r;

                l = soft_clip(l * pre) * post;
                r = soft_clip(r * pre) * post;
                if !l.is_finite() { l = 0.0; }
                if !r.is_finite() { r = 0.0; }
                l_out[pos + i] = l;
                r_out[pos + i] = r;
            }
            pos += c;
        }
    }
}
