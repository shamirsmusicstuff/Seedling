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
}
