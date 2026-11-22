//! Little waveform previews for the editor (std only): oscillator shape, saturator curve and reverb tail. All values are -1..1.

use std::f32::consts::TAU;

/// Oscillator preview: `form` morphs sine -> triangle -> saw -> square; `t` is the phase 0..1.
pub fn osc_y(t: f32, form: f32) -> f32 {
    let t = t - t.floor();
    let sine = (t * TAU).sin();
    let tri = if t < 0.25 { 4.0 * t } else if t < 0.75 { 2.0 - 4.0 * t } else { 4.0 * t - 4.0 };
    let saw = 2.0 * t - 1.0;
    let sq = if t < 0.5 { 0.85 } else { -0.85 };
    let f = form.clamp(0.0, 1.0) * 3.0;
    let (a, b, m) = if f < 1.0 { (sine, tri, f) } else if f < 2.0 { (tri, saw, f - 1.0) } else { (saw, sq, f - 2.0) };
    a + (b - a) * m
}

/// Saturator preview: returns `(out, input)` for a sine at phase `t`.
pub fn sat_y(t: f32, drive: f32, clip: f32) -> (f32, f32) {
    let s = (t * TAU).sin();
    let k = 1.0 + 9.0 * drive.clamp(0.0, 1.0);
    let out = (k * s).tanh() / k.tanh();
    let lim = 1.0 - 0.55 * clip.clamp(0.0, 1.0);
    (out.clamp(-lim, lim), s)
}

fn hash(n: u32) -> f32 {
    let mut x = n.wrapping_mul(0x9E37_79B1) ^ 0x85EB_CA6B;
    x ^= x >> 15;
    x = x.wrapping_mul(0x2C1B_3C6D);
    x ^= x >> 12;
    x = x.wrapping_mul(0x297A_2D39);
    x ^= x >> 15;
    (x as f32 / u32::MAX as f32) * 2.0 - 1.0
}

/// Reverb tail preview at sample step `j`: returns `(value, envelope)`.
pub fn rvb_y(j: u32, len: f32, size: f32, atk: f32, damp: f32) -> (f32, f32) {
    let x = j as f32;
    let tau = (0.18 + 0.8 * len) * (0.7 + 0.6 * size) * 150.0;
    let a = 3.0 + 40.0 * atk;
    let env = (1.0 - (-x / a).exp()) * (-x / tau.max(1.0)).exp();
    let d = damp.clamp(0.0, 1.0);
    let sig = hash(j) * (1.0 - d) + (x * 0.45).sin() * d;
    (env * sig, env)
}

/// Envelope page shape (0..1): attack rises to a peak placed by `tilt`, then decays; `time` sets its length and `looping` repeats it.
pub fn env_y(x: f32, time: f32, looping: f32, tilt: f32) -> f32 {
    let len = 0.25 + 0.7 * time.clamp(0.0, 1.0);
    let x = if looping > 0.05 { x % len } else { x };
    if x >= len {
        return 0.0;
    }
    let p = 0.1 + 0.8 * tilt.clamp(0.0, 1.0);
    let u = x / len;
    if u < p {
        let r = u / p;
        1.0 - (1.0 - r).powi(2)
    } else {
        let r = (u - p) / (1.0 - p);
        (1.0 - r).powi(2)
    }
}

/// Volume envelope (0..1): attack, decay to the sustain level, a held sustain that fades by `fade`, then release.
pub fn adsr_y(x: f32, atk: f32, dcy: f32, sus: f32, fade: f32) -> f32 {
    let a = 0.04 + 0.3 * atk.clamp(0.0, 1.0);
    let d = 0.04 + 0.3 * dcy.clamp(0.0, 1.0);
    let (hold, rel) = (0.35, 0.15);
    let tot = a + d + hold + rel;
    let t = x.clamp(0.0, 1.0) * tot;
    let sus = sus.clamp(0.0, 1.0);
    let held = |t: f32| sus * (1.0 - 0.9 * fade.clamp(0.0, 1.0) * ((t - a - d) / hold).clamp(0.0, 1.0));
    if t < a {
        let r = t / a;
        1.0 - (1.0 - r).powi(2)
    } else if t < a + d {
        let r = (t - a) / d;
        1.0 + (sus - 1.0) * (1.0 - (1.0 - r).powi(2))
    } else if t < a + d + hold {
        held(t)
    } else {
        let r = (t - a - d - hold) / rel;
        held(a + d + hold) * (1.0 - r).max(0.0)
    }
}

/// Modulation envelope (0..1): attack then decay bent by `shape`, scaled by velocity sensitivity.
pub fn mod_y(x: f32, atk: f32, dcy: f32, shape: f32, vel: f32) -> f32 {
    let a = 0.03 + 0.4 * atk.clamp(0.0, 1.0);
    let d = 0.1 + 0.6 * dcy.clamp(0.0, 1.0);
    let t = x.clamp(0.0, 1.0) * (a + d) * 1.2;
    let amp = 0.4 + 0.6 * vel.clamp(0.0, 1.0);
    let k = 0.4 + 2.6 * shape.clamp(0.0, 1.0);
    let v = if t < a {
        1.0 - (1.0 - t / a).powf(k)
    } else if t < a + d {
        (1.0 - (t - a) / d).powf(k)
    } else {
        0.0
    };
    amp * v
}

/// LFO (-1..1): `rate` sets the cycles shown, `amt` the depth and `dly` how late it fades in.
pub fn lfo_y(x: f32, rate: f32, amt: f32, dly: f32) -> f32 {
    let cycles = 1.5 + 7.0 * rate.clamp(0.0, 1.0);
    let u = ((x - dly.clamp(0.0, 1.0) * 0.45) / 0.25).clamp(0.0, 1.0);
    let fade = u * u * (3.0 - 2.0 * u);
    (x * cycles * TAU).sin() * amt.clamp(0.0, 1.0) * fade
}

/// Filter response (0..1, about 60 dB of range) at position `x` on a log frequency axis; `ftype` morphs low-pass -> band-pass -> high-pass.
pub fn filter_y(x: f32, ftype: f32, cutoff: f32, q: f32) -> f32 {
    let w = 2.0f32.powf((x - cutoff) * 7.0);
    let qq = 0.5 + 8.0 * q.clamp(0.0, 1.0);
    let den = ((1.0 - w * w).powi(2) + (w / qq).powi(2)).sqrt().max(1e-6);
    let lp = 1.0 / den;
    let bp = (w / qq) / den;
    let hp = w * w / den;
    let t = ftype.clamp(0.0, 1.0) * 2.0;
    let m = if t < 1.0 { lp + (bp - lp) * t } else { bp + (hp - bp) * (t - 1.0) };
    ((20.0 * m.max(1e-4).log10() + 40.0) / 52.0).clamp(0.0, 1.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn osc_in_range_and_morphs() {
        for i in 0..100 {
            for f in [0.0, 0.3, 0.7, 1.0] {
                assert!(osc_y(i as f32 / 100.0, f).abs() <= 1.0001);
            }
        }
        assert!((osc_y(0.25, 0.0) - 1.0).abs() < 1e-5);
        assert!((osc_y(0.25, 1.0) - 0.85).abs() < 1e-5);
    }

    #[test]
    fn saturation_squashes_and_clips() {
        let (soft, _) = sat_y(0.25, 0.0, 0.0);
        assert!((soft - 1.0).abs() < 1e-4);
        let (hard, inp) = sat_y(0.05, 1.0, 0.0);
        assert!(hard > inp);
        let (clipped, _) = sat_y(0.25, 0.0, 1.0);
        assert!((clipped - 0.45).abs() < 1e-4);
    }

    #[test]
    fn reverb_decays() {
        let (_, e0) = rvb_y(0, 0.5, 0.5, 0.2, 0.5);
        let (_, e1) = rvb_y(40, 0.5, 0.5, 0.2, 0.5);
        let (_, e2) = rvb_y(400, 0.5, 0.5, 0.2, 0.5);
        assert!(e0 < 1e-6 && e1 > e2);
        assert!(rvb_y(40, 0.9, 0.5, 0.2, 0.5).1 > e1);
    }

    #[test]
    fn envelopes_stay_in_range() {
        for i in 0..=100 {
            let x = i as f32 / 100.0;
            for p in [0.0, 0.5, 1.0] {
                for (v, lo) in [
                    (env_y(x, p, p, p), 0.0),
                    (adsr_y(x, p, p, p, p), 0.0),
                    (mod_y(x, p, p, p, p), 0.0),
                    (filter_y(x, p, p, p), 0.0),
                    (lfo_y(x, p, p, p), -1.0),
                ] {
                    assert!(v >= lo - 1e-4 && v <= 1.0001, "{v} at {x} {p}");
                }
            }
        }
        assert!(env_y(0.0, 0.5, 0.0, 0.5) < 1e-4);
        assert!((adsr_y(0.0, 0.5, 0.5, 0.7, 0.0)).abs() < 1e-4);
        assert!(lfo_y(0.9, 0.5, 0.0, 0.0).abs() < 1e-6);
    }

    #[test]
    fn filter_types_differ() {
        assert!(filter_y(0.1, 0.0, 0.5, 0.2) > filter_y(0.9, 0.0, 0.5, 0.2));
        assert!(filter_y(0.9, 1.0, 0.5, 0.2) > filter_y(0.1, 1.0, 0.5, 0.2));
    }
}
