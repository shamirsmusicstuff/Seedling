use seedling_dsp::engine::*;
use seedling_dsp::params::*;
use seedling_dsp::randomizer::*;
use seedling_dsp::wavetable::*;
use std::sync::Arc;
use std::time::Instant;

const SR: f32 = 44100.0;

fn defaults() -> Vec<f32> { PARAMS.iter().map(|p| p.def).collect() }

fn render(e: &mut Engine, n: usize) -> (Vec<f32>, Vec<f32>) {
    let mut l = vec![0.0; n];
    let mut r = vec![0.0; n];
    let mut pos = 0;
    while pos < n {
        let c = (n - pos).min(512);
        e.render(&mut l[pos..pos + c], &mut r[pos..pos + c]);
        pos += c;
    }
    (l, r)
}

/// Cents error of the normalised autocorrelation peak near the expected period (several periods, parabolic refine).
fn acf_cents(x: &[f32], from: usize, len: usize, expect_hz: f32) -> f32 {
    let period = SR as f64 / expect_hz as f64;
    let mult = ((400.0 / period).ceil() as usize).max(1);
    let p0 = period * mult as f64;
    let (lo, hi) = ((p0 * 0.985) as usize, (p0 * 1.015) as usize + 1);
    let mut c = vec![0f64; hi + 2];
    let e0: f64 = (0..len).map(|i| (x[from + i] as f64).powi(2)).sum();
    for lag in lo..=hi {
        let (mut s, mut e1) = (0f64, 0f64);
        for i in 0..len { let a = x[from + i] as f64; let b = x[from + i + lag] as f64; s += a * b; e1 += b * b; }
        c[lag] = s / (e0 * e1 + 1e-18).sqrt();
    }
    let mut best = lo;
    for lag in lo..=hi { if c[lag] > c[best] { best = lag; } }
    let mut lag_f = best as f64;
    if best > lo && best < hi {
        let (a, b, d) = (c[best - 1], c[best], c[best + 1]);
        let den = a - 2.0 * b + d;
        if den.abs() > 1e-12 { lag_f += 0.5 * (a - d) / den; }
    }
    (1200.0 * (p0 / lag_f).log2()) as f32
}


fn pitch_patch() -> Vec<f32> {
    let mut p = defaults();
    for (i, v) in [(idx::A_FORM, 0.0), (idx::A_NOISE, 0.0), (idx::A_MOD, 0.0), (idx::A_COLOR, 0.0), (idx::A_FREQ, 0.0),
        (idx::OSC_MIX, 0.0), (idx::MIX_MOD, 0.0), (idx::FM_AMT, 0.0), (idx::FM_MOD, 0.0), (idx::FLT_FREQ, 1.0), (idx::FLT_MOD, 0.0),
        (idx::FLT_KF, 0.0), (idx::FLT_TYPE, 0.0), (idx::FLT_Q, 0.0), (idx::FLT_SEP, 0.0), (idx::VOL_ATK, 0.0), (idx::VOL_SUS, 1.0),
        (idx::VOL_FADE, 0.0), (idx::LFO_AMT, 0.0), (idx::RVB_MIX, 0.0), (idx::SATURATE, 0.0), (idx::ADJ_BASS, 0.0),
        (idx::ADJ_TREB, 0.0), (idx::ADJ_PAN, 0.0), (idx::ADJ_CLIP, 0.0), (idx::ENV_TIME, 0.5), (idx::ENV_KF, 0.0)] { p[i] = v; }
    p
}

#[test]
fn every_source_plays_in_tune_and_is_finite() {
    for src in 0..NUM_SOURCES {
        let mut p = pitch_patch();
        p[idx::SRC_TYPE] = src as f32;
        p[idx::A_COLOR] = if src == 2 { 0.55 } else if src == 4 { 0.6 } else { 0.0 };
        for &note in &[45, 57, 69, 81] {
            let hz = 440.0 * 2f32.powf((note as f32 - 69.0) / 12.0);
            let mut e = Engine::new(SR);
            e.set_params(&p);
            e.note_on(note, 1.0);
            let (l, _) = render(&mut e, (1.8 * SR) as usize);
            assert!(l.iter().all(|v| v.is_finite()), "source {src} note {note} not finite");
            let pk = l.iter().fold(0f32, |m, v| m.max(v.abs()));
            let en: f64 = l[(0.05 * SR) as usize..(0.3 * SR) as usize].iter().map(|v| (*v as f64).powi(2)).sum();
            let rms_db = 10.0 * (en / (0.25 * SR as f64) + 1e-12).log10();
            let c = acf_cents(&l, (0.08 * SR) as usize, (0.12 * SR) as usize, hz);
            println!("  {:<10} note {note}: rms {rms_db:.1} dB, peak {pk:.2}, {c:+.1} ct", SOURCE_NAMES[src]);
            assert!(rms_db > -45.0, "source {src} note {note} is silent ({rms_db} dB)");
            assert!(pk < 1.6, "source {src} note {note} peak {pk}");
            assert!(c.abs() < 12.0, "source {src} note {note}: {c} cents");
        }
    }
}

#[test]
fn random_patches_stay_finite_and_bounded() {
    for seed in (0..40u32).chain(1_000_000..1_000_040) {
        let mut p = [0f32; NUM_PARAMS];
        randomize_values(seed, if seed % 3 == 0 { 1.0 } else { 0.0 }, &mut p);
        let mut e = Engine::new(SR);
        e.set_params(&p);
        for n in [48, 60, 72] { e.note_on(n, 0.9); }
        let (l, r) = render(&mut e, 20000);
        for v in l.iter().chain(r.iter()) { assert!(v.is_finite() && v.abs() < 2.5, "seed {seed}: {v}"); }
        e.all_notes_off();
        let (l, _) = render(&mut e, 4096);
        assert!(l.iter().all(|v| v.is_finite()));
    }
}

#[test]
fn user_wavetable_changes_sound_and_survives_swap() {
    let mut p = defaults();
    p[idx::SRC_TYPE] = 1.0;
    p[idx::RVB_MIX] = 0.0;
    let slot = WavetableSlot::new();
    let mut e = Engine::new(SR);
    e.set_wavetable_slot(slot.clone());
    e.set_params(&p);
    e.note_on(60, 0.8);
    let (a, _) = render(&mut e, 8192);

    // a one-frame saw table: 2048 samples
    let saw: Vec<f32> = (0..2048).map(|i| (i as f32 / 1024.0) - 1.0).collect();
    let r = import_wavetable(&saw, 44100.0, "saw", 0, 2048);
    slot.set(r.table.map(Arc::new));
    e.note_on(60, 0.8);
    let (b, _) = render(&mut e, 8192);
    assert!(b.iter().all(|v| v.is_finite()));
    let diff: f32 = a.iter().zip(&b).map(|(x, y)| (x - y).abs()).sum();
    assert!(diff > 1.0, "user table made no difference");
    slot.set(None);
    let (c, _) = render(&mut e, 4096);
    assert!(c.iter().all(|v| v.is_finite()));
}

#[test]
fn cpu_budget_eight_voices() {
    for src in 0..NUM_SOURCES {
        let mut p = defaults();
        p[idx::SRC_TYPE] = src as f32;
        let mut e = Engine::new(SR);
        e.set_params(&p);
        for n in 0..8 { e.note_on(48 + n * 3, 0.8); }
        let _ = render(&mut e, 4096);
        let t = Instant::now();
        let secs = 2.0f32;
        let _ = render(&mut e, (SR * secs) as usize);
        let load = t.elapsed().as_secs_f32() / secs * 100.0;
        println!("{}: {load:.1}% CPU for 8 voices", SOURCE_NAMES[src]);
        assert!(load < 60.0, "{} too heavy: {load}%", SOURCE_NAMES[src]);
    }
}
