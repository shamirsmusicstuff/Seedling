//! Archetype recipes for Randomize v2 (seeds from 1,000,000), ported from Archetypes.h.
//! A recipe is a few range overrides on top of the generic soft ranges, plus allowed sound sources and reverbs.

use crate::params::idx::*;

pub const FIRST_ARCHETYPE_SEED: u32 = 1_000_000;
/// Randomize draws FIRST_ARCHETYPE_SEED .. FIRST_ARCHETYPE_SEED + SEED_RANGE - 1
pub const SEED_RANGE: u32 = 1_000_000;

#[derive(Clone, Copy, Debug)]
pub struct Ov { pub p: usize, pub lo: f32, pub hi: f32 }

pub const S_OSC: u32 = 1 << 0;
pub const S_WAVE: u32 = 1 << 1;
pub const S_PLUCK: u32 = 1 << 2;
pub const S_VOWEL: u32 = 1 << 3;
pub const S_STRUCK: u32 = 1 << 4;
pub const S_UNISON: u32 = 1 << 5;

pub const R_PLATE: u32 = 1 << 0;
pub const R_VINTAGE: u32 = 1 << 1;
pub const R_SMOOTH: u32 = 1 << 2;
pub const R_CHAMBER: u32 = 1 << 3;
pub const R_TILED: u32 = 1 << 4;
pub const R_HALL: u32 = 1 << 5;
pub const R_DENSE: u32 = 1 << 6;
pub const R_PARALLEL: u32 = 1 << 7;
pub const R_SPRING: u32 = 1 << 8;
pub const R_SHIMMER: u32 = 1 << 9;
pub const R_REVERSE: u32 = 1 << 10;
pub const R_GATED: u32 = 1 << 11;

#[derive(Clone, Copy, Debug)]
pub struct Archetype {
    pub name: &'static str,
    pub weight: f32,
    pub src: u32,
    pub rev: u32,
    pub ov: &'static [Ov],
}

pub const LIST: [Archetype; 22] = [
    Archetype { name: "Soft Pad", weight: 3., src: S_UNISON, rev: R_HALL | R_DENSE | R_SMOOTH | R_PARALLEL, ov: &[Ov { p: VOL_ATK, lo: 0.55, hi: 0.85 }, Ov { p: VOL_DCY, lo: 0.6, hi: 0.9 }, Ov { p: VOL_SUS, lo: 0.6, hi: 0.9 }, Ov { p: MOD_ATK, lo: 0.4, hi: 0.8 }, Ov { p: MOD_DCY, lo: 0.5, hi: 0.9 }, Ov { p: A_FORM, lo: 0.3, hi: 0.7 }, Ov { p: A_COLOR, lo: 0., hi: 0.35 }, Ov { p: FLT_FREQ, lo: 0.35, hi: 0.65 }, Ov { p: FLT_Q, lo: 0.05, hi: 0.3 }, Ov { p: FLT_MOD, lo: 0., hi: 0.4 }, Ov { p: LFO_AMT, lo: 0.1, hi: 0.35 }, Ov { p: A_FREQ, lo: -12., hi: 0. }, Ov { p: OSC_MIX, lo: 0.15, hi: 0.5 }, Ov { p: RVB_MIX, lo: 0.2, hi: 0.45 }, Ov { p: RVB_LEN, lo: 0.55, hi: 0.9 }] },
    Archetype { name: "Warm Pad", weight: 2., src: S_OSC, rev: R_HALL | R_SMOOTH | R_VINTAGE, ov: &[Ov { p: VOL_ATK, lo: 0.5, hi: 0.8 }, Ov { p: VOL_DCY, lo: 0.6, hi: 0.9 }, Ov { p: VOL_SUS, lo: 0.6, hi: 0.9 }, Ov { p: A_FORM, lo: 0.3, hi: 0.7 }, Ov { p: B_FORM, lo: 0.5, hi: 0.9 }, Ov { p: OSC_MIX, lo: 0.3, hi: 0.6 }, Ov { p: B_FREQ, lo: -12., hi: 12. }, Ov { p: FLT_FREQ, lo: 0.3, hi: 0.6 }, Ov { p: FLT_MOD, lo: 0.1, hi: 0.5 }, Ov { p: LFO_AMT, lo: 0.1, hi: 0.3 }, Ov { p: A_FREQ, lo: -12., hi: 0. }, Ov { p: RVB_MIX, lo: 0.2, hi: 0.45 }, Ov { p: RVB_LEN, lo: 0.5, hi: 0.85 }] },
    Archetype { name: "Choir", weight: 2., src: S_VOWEL, rev: R_HALL | R_DENSE | R_PARALLEL | R_SHIMMER, ov: &[Ov { p: VOL_ATK, lo: 0.5, hi: 0.8 }, Ov { p: VOL_DCY, lo: 0.6, hi: 0.9 }, Ov { p: VOL_SUS, lo: 0.6, hi: 0.9 }, Ov { p: A_FORM, lo: 0., hi: 1. }, Ov { p: A_MOD, lo: -0.5, hi: 0.5 }, Ov { p: A_COLOR, lo: 0., hi: 0.5 }, Ov { p: A_NOISE, lo: 0.05, hi: 0.35 }, Ov { p: MOD_ATK, lo: 0.3, hi: 0.7 }, Ov { p: LFO_AMT, lo: 0.1, hi: 0.3 }, Ov { p: FLT_FREQ, lo: 0.6, hi: 0.9 }, Ov { p: A_FREQ, lo: -12., hi: 0. }, Ov { p: RVB_MIX, lo: 0.25, hi: 0.5 }, Ov { p: RVB_LEN, lo: 0.55, hi: 0.85 }] },
    Archetype { name: "Glass Pad", weight: 2., src: S_WAVE, rev: R_SHIMMER | R_HALL | R_DENSE | R_SMOOTH, ov: &[Ov { p: VOL_ATK, lo: 0.5, hi: 0.8 }, Ov { p: VOL_DCY, lo: 0.6, hi: 0.9 }, Ov { p: VOL_SUS, lo: 0.55, hi: 0.85 }, Ov { p: A_FORM, lo: 0.6, hi: 1. }, Ov { p: A_MOD, lo: -0.5, hi: 0.5 }, Ov { p: MOD_ATK, lo: 0.4, hi: 0.8 }, Ov { p: FLT_FREQ, lo: 0.5, hi: 0.85 }, Ov { p: A_FREQ, lo: 0., hi: 14. }, Ov { p: LFO_AMT, lo: 0.05, hi: 0.25 }, Ov { p: RVB_MIX, lo: 0.25, hi: 0.5 }, Ov { p: RVB_LEN, lo: 0.6, hi: 0.9 }] },
    Archetype { name: "Strings", weight: 2., src: S_UNISON, rev: R_HALL | R_DENSE | R_PLATE, ov: &[Ov { p: VOL_ATK, lo: 0.45, hi: 0.7 }, Ov { p: VOL_DCY, lo: 0.6, hi: 0.9 }, Ov { p: VOL_SUS, lo: 0.7, hi: 0.95 }, Ov { p: A_FORM, lo: 0.35, hi: 0.6 }, Ov { p: FLT_FREQ, lo: 0.4, hi: 0.65 }, Ov { p: FLT_MOD, lo: 0., hi: 0.35 }, Ov { p: MOD_ATK, lo: 0.3, hi: 0.6 }, Ov { p: LFO_AMT, lo: 0.1, hi: 0.3 }, Ov { p: A_FREQ, lo: -7., hi: 7. }, Ov { p: RVB_MIX, lo: 0.2, hi: 0.4 }, Ov { p: RVB_LEN, lo: 0.5, hi: 0.8 }] },
    Archetype { name: "Drone", weight: 1.5, src: S_UNISON, rev: R_HALL | R_DENSE | R_SHIMMER | R_PARALLEL, ov: &[Ov { p: VOL_ATK, lo: 0.75, hi: 1. }, Ov { p: VOL_SUS, lo: 0.8, hi: 1. }, Ov { p: ENV_LOOP, lo: 0.3, hi: 0.8 }, Ov { p: MOD_ATK, lo: 0.5, hi: 0.9 }, Ov { p: LFO_AMT, lo: 0.2, hi: 0.4 }, Ov { p: LFO_RATE, lo: 0.05, hi: 0.3 }, Ov { p: A_FREQ, lo: -24., hi: -7. }, Ov { p: A_FORM, lo: 0.2, hi: 0.6 }, Ov { p: FLT_FREQ, lo: 0.3, hi: 0.6 }, Ov { p: FLT_MOD, lo: 0.1, hi: 0.5 }, Ov { p: RVB_MIX, lo: 0.3, hi: 0.55 }, Ov { p: RVB_LEN, lo: 0.7, hi: 0.95 }] },
    Archetype { name: "Pluck", weight: 3., src: S_PLUCK, rev: R_PLATE | R_CHAMBER | R_TILED | R_SPRING | R_VINTAGE, ov: &[Ov { p: VOL_ATK, lo: 0., hi: 0.1 }, Ov { p: VOL_DCY, lo: 0.5, hi: 0.8 }, Ov { p: VOL_SUS, lo: 0.5, hi: 0.9 }, Ov { p: A_FORM, lo: 0.3, hi: 0.8 }, Ov { p: A_COLOR, lo: 0.35, hi: 0.8 }, Ov { p: A_NOISE, lo: 0.1, hi: 0.6 }, Ov { p: A_MOD, lo: 0., hi: 0.4 }, Ov { p: FLT_FREQ, lo: 0.55, hi: 0.9 }, Ov { p: FLT_MOD, lo: 0., hi: 0.4 }, Ov { p: A_FREQ, lo: -7., hi: 7. }, Ov { p: RVB_MIX, lo: 0.12, hi: 0.35 }, Ov { p: RVB_LEN, lo: 0.3, hi: 0.6 }] },
    Archetype { name: "Harp", weight: 1.5, src: S_PLUCK, rev: R_HALL | R_PLATE | R_SMOOTH, ov: &[Ov { p: VOL_ATK, lo: 0., hi: 0.05 }, Ov { p: VOL_DCY, lo: 0.6, hi: 0.9 }, Ov { p: VOL_SUS, lo: 0.6, hi: 1. }, Ov { p: A_FORM, lo: 0.2, hi: 0.5 }, Ov { p: A_COLOR, lo: 0.7, hi: 0.95 }, Ov { p: A_NOISE, lo: 0., hi: 0.3 }, Ov { p: FLT_FREQ, lo: 0.6, hi: 0.9 }, Ov { p: A_FREQ, lo: 0., hi: 12. }, Ov { p: RVB_MIX, lo: 0.2, hi: 0.45 }, Ov { p: RVB_LEN, lo: 0.5, hi: 0.85 }] },
    Archetype { name: "Marimba", weight: 2., src: S_STRUCK, rev: R_CHAMBER | R_TILED | R_PLATE | R_SPRING | R_GATED, ov: &[Ov { p: VOL_ATK, lo: 0., hi: 0.05 }, Ov { p: VOL_DCY, lo: 0.3, hi: 0.6 }, Ov { p: VOL_SUS, lo: 0., hi: 0.3 }, Ov { p: A_FORM, lo: 0.2, hi: 0.3 }, Ov { p: A_COLOR, lo: 0.2, hi: 0.5 }, Ov { p: A_NOISE, lo: 0., hi: 0.1 }, Ov { p: FLT_FREQ, lo: 0.6, hi: 0.9 }, Ov { p: A_FREQ, lo: -7., hi: 7. }, Ov { p: OSC_MIX, lo: 0., hi: 0.2 }, Ov { p: RVB_MIX, lo: 0.1, hi: 0.3 }, Ov { p: RVB_LEN, lo: 0.3, hi: 0.55 }] },
    Archetype { name: "Glockenspiel", weight: 1.5, src: S_STRUCK, rev: R_PLATE | R_HALL | R_SHIMMER | R_SMOOTH, ov: &[Ov { p: VOL_ATK, lo: 0., hi: 0.05 }, Ov { p: VOL_DCY, lo: 0.4, hi: 0.7 }, Ov { p: VOL_SUS, lo: 0., hi: 0.2 }, Ov { p: A_FORM, lo: 0.45, hi: 0.55 }, Ov { p: A_COLOR, lo: 0.35, hi: 0.7 }, Ov { p: FLT_FREQ, lo: 0.7, hi: 0.95 }, Ov { p: A_FREQ, lo: 7., hi: 14. }, Ov { p: OSC_MIX, lo: 0., hi: 0.2 }, Ov { p: RVB_MIX, lo: 0.2, hi: 0.45 }, Ov { p: RVB_LEN, lo: 0.45, hi: 0.8 }] },
    Archetype { name: "Bell", weight: 2., src: S_STRUCK, rev: R_HALL | R_DENSE | R_SHIMMER | R_PARALLEL, ov: &[Ov { p: VOL_ATK, lo: 0., hi: 0.05 }, Ov { p: VOL_DCY, lo: 0.5, hi: 0.85 }, Ov { p: VOL_SUS, lo: 0., hi: 0.2 }, Ov { p: A_FORM, lo: 0.7, hi: 0.8 }, Ov { p: A_COLOR, lo: 0.55, hi: 0.9 }, Ov { p: A_NOISE, lo: 0., hi: 0.1 }, Ov { p: FLT_FREQ, lo: 0.6, hi: 0.9 }, Ov { p: A_FREQ, lo: -7., hi: 7. }, Ov { p: OSC_MIX, lo: 0., hi: 0.25 }, Ov { p: RVB_MIX, lo: 0.25, hi: 0.5 }, Ov { p: RVB_LEN, lo: 0.5, hi: 0.85 }] },
    Archetype { name: "Gong", weight: 1., src: S_STRUCK, rev: R_HALL | R_DENSE | R_PARALLEL | R_REVERSE, ov: &[Ov { p: VOL_ATK, lo: 0., hi: 0.08 }, Ov { p: VOL_DCY, lo: 0.6, hi: 0.9 }, Ov { p: VOL_SUS, lo: 0., hi: 0.2 }, Ov { p: A_FORM, lo: 0.9, hi: 1. }, Ov { p: A_COLOR, lo: 0.5, hi: 0.9 }, Ov { p: A_FREQ, lo: -12., hi: 0. }, Ov { p: FLT_FREQ, lo: 0.4, hi: 0.75 }, Ov { p: OSC_MIX, lo: 0., hi: 0.3 }, Ov { p: RVB_MIX, lo: 0.25, hi: 0.5 }, Ov { p: RVB_LEN, lo: 0.55, hi: 0.9 }] },
    Archetype { name: "Glass Keys", weight: 2., src: S_WAVE, rev: R_PLATE | R_CHAMBER | R_HALL | R_SMOOTH, ov: &[Ov { p: VOL_ATK, lo: 0., hi: 0.2 }, Ov { p: VOL_DCY, lo: 0.4, hi: 0.75 }, Ov { p: VOL_SUS, lo: 0.05, hi: 0.4 }, Ov { p: A_FORM, lo: 0.75, hi: 1. }, Ov { p: A_MOD, lo: -0.4, hi: 0.1 }, Ov { p: MOD_DCY, lo: 0.3, hi: 0.6 }, Ov { p: FLT_FREQ, lo: 0.6, hi: 0.9 }, Ov { p: A_FREQ, lo: 0., hi: 12. }, Ov { p: RVB_MIX, lo: 0.15, hi: 0.4 }, Ov { p: RVB_LEN, lo: 0.35, hi: 0.65 }] },
    Archetype { name: "Organ", weight: 1.5, src: S_WAVE, rev: R_SPRING | R_VINTAGE | R_TILED | R_CHAMBER, ov: &[Ov { p: VOL_ATK, lo: 0., hi: 0.08 }, Ov { p: VOL_DCY, lo: 0.8, hi: 1. }, Ov { p: VOL_SUS, lo: 0.85, hi: 1. }, Ov { p: A_FORM, lo: 0.2, hi: 0.45 }, Ov { p: A_MOD, lo: 0., hi: 0.2 }, Ov { p: LFO_AMT, lo: 0.1, hi: 0.35 }, Ov { p: LFO_RATE, lo: 0.35, hi: 0.6 }, Ov { p: FLT_FREQ, lo: 0.65, hi: 0.95 }, Ov { p: A_FREQ, lo: -12., hi: 0. }, Ov { p: RVB_MIX, lo: 0.1, hi: 0.3 }, Ov { p: RVB_LEN, lo: 0.3, hi: 0.55 }] },
    Archetype { name: "FM Keys", weight: 2., src: S_OSC, rev: R_PLATE | R_CHAMBER | R_SPRING | R_VINTAGE | R_TILED, ov: &[Ov { p: FM_AMT, lo: 0.3, hi: 0.7 }, Ov { p: FM_MOD, lo: -0.7, hi: -0.3 }, Ov { p: A_FORM, lo: 0., hi: 0.1 }, Ov { p: B_FORM, lo: 0.5, hi: 0.65 }, Ov { p: VOL_ATK, lo: 0., hi: 0.08 }, Ov { p: VOL_DCY, lo: 0.4, hi: 0.7 }, Ov { p: VOL_SUS, lo: 0.05, hi: 0.35 }, Ov { p: MOD_DCY, lo: 0.3, hi: 0.6 }, Ov { p: B_FREQ, lo: -12., hi: 12. }, Ov { p: FLT_FREQ, lo: 0.6, hi: 0.9 }, Ov { p: RVB_MIX, lo: 0.1, hi: 0.3 }, Ov { p: RVB_LEN, lo: 0.3, hi: 0.6 }] },
    Archetype { name: "Warm Bass", weight: 2.5, src: S_OSC, rev: R_PLATE | R_CHAMBER | R_TILED | R_VINTAGE, ov: &[Ov { p: A_FREQ, lo: -24., hi: -12. }, Ov { p: B_FREQ, lo: -24., hi: -12. }, Ov { p: SUB_AM, lo: 0.3, hi: 0.7 }, Ov { p: A_FORM, lo: 0.1, hi: 0.7 }, Ov { p: B_FORM, lo: 0.4, hi: 1. }, Ov { p: OSC_MIX, lo: 0.1, hi: 0.45 }, Ov { p: VOL_ATK, lo: 0., hi: 0.1 }, Ov { p: VOL_DCY, lo: 0.5, hi: 0.8 }, Ov { p: VOL_SUS, lo: 0.6, hi: 0.9 }, Ov { p: FLT_FREQ, lo: 0.25, hi: 0.5 }, Ov { p: FLT_Q, lo: 0.1, hi: 0.4 }, Ov { p: FLT_MOD, lo: 0.2, hi: 0.6 }, Ov { p: MOD_DCY, lo: 0.3, hi: 0.6 }, Ov { p: RVB_MIX, lo: 0., hi: 0.15 }, Ov { p: RVB_LEN, lo: 0.2, hi: 0.45 }] },
    Archetype { name: "Plucked Bass", weight: 1.5, src: S_PLUCK, rev: R_PLATE | R_CHAMBER | R_TILED, ov: &[Ov { p: A_FREQ, lo: -24., hi: -12. }, Ov { p: VOL_ATK, lo: 0., hi: 0.05 }, Ov { p: VOL_DCY, lo: 0.4, hi: 0.7 }, Ov { p: VOL_SUS, lo: 0.3, hi: 0.8 }, Ov { p: A_FORM, lo: 0.2, hi: 0.6 }, Ov { p: A_COLOR, lo: 0.3, hi: 0.6 }, Ov { p: SUB_AM, lo: 0.2, hi: 0.6 }, Ov { p: B_FREQ, lo: -24., hi: -12. }, Ov { p: OSC_MIX, lo: 0.1, hi: 0.4 }, Ov { p: FLT_FREQ, lo: 0.35, hi: 0.65 }, Ov { p: RVB_MIX, lo: 0., hi: 0.15 }, Ov { p: RVB_LEN, lo: 0.2, hi: 0.4 }] },
    Archetype { name: "Pulse Lead", weight: 2., src: S_OSC, rev: R_PLATE | R_VINTAGE | R_SPRING | R_CHAMBER, ov: &[Ov { p: A_FORM, lo: 0.85, hi: 1. }, Ov { p: B_FORM, lo: 0., hi: 0.35 }, Ov { p: OSC_MIX, lo: 0.2, hi: 0.5 }, Ov { p: B_FREQ, lo: -12., hi: 12. }, Ov { p: VOL_ATK, lo: 0., hi: 0.1 }, Ov { p: VOL_SUS, lo: 0.7, hi: 1. }, Ov { p: LFO_AMT, lo: 0.2, hi: 0.5 }, Ov { p: LFO_DLY, lo: 0.3, hi: 0.7 }, Ov { p: FLT_Q, lo: 0.25, hi: 0.55 }, Ov { p: FLT_FREQ, lo: 0.5, hi: 0.85 }, Ov { p: A_FREQ, lo: 0., hi: 12. }, Ov { p: RVB_MIX, lo: 0.1, hi: 0.3 }, Ov { p: RVB_LEN, lo: 0.3, hi: 0.6 }] },
    Archetype { name: "Brass Swell", weight: 1.5, src: S_OSC, rev: R_HALL | R_PLATE | R_SMOOTH, ov: &[Ov { p: A_FORM, lo: 0.6, hi: 0.7 }, Ov { p: B_FORM, lo: 0.5, hi: 0.9 }, Ov { p: OSC_MIX, lo: 0.3, hi: 0.6 }, Ov { p: VOL_ATK, lo: 0.3, hi: 0.55 }, Ov { p: MOD_ATK, lo: 0.3, hi: 0.6 }, Ov { p: FLT_MOD, lo: 0.4, hi: 0.9 }, Ov { p: FLT_FREQ, lo: 0.3, hi: 0.55 }, Ov { p: SATURATE, lo: 0.1, hi: 0.3 }, Ov { p: VOL_SUS, lo: 0.6, hi: 0.9 }, Ov { p: A_FREQ, lo: -7., hi: 5. }, Ov { p: RVB_MIX, lo: 0.15, hi: 0.35 }, Ov { p: RVB_LEN, lo: 0.4, hi: 0.7 }] },
    Archetype { name: "Arp Stab", weight: 1.5, src: S_UNISON, rev: R_PLATE | R_CHAMBER | R_TILED | R_SPRING | R_GATED, ov: &[Ov { p: VOL_ATK, lo: 0., hi: 0.05 }, Ov { p: VOL_DCY, lo: 0.2, hi: 0.45 }, Ov { p: VOL_SUS, lo: 0., hi: 0.15 }, Ov { p: MOD_DCY, lo: 0.2, hi: 0.5 }, Ov { p: FLT_MOD, lo: 0.5, hi: 1. }, Ov { p: FLT_FREQ, lo: 0.35, hi: 0.65 }, Ov { p: A_FORM, lo: 0.1, hi: 0.5 }, Ov { p: A_FREQ, lo: 0., hi: 12. }, Ov { p: RVB_MIX, lo: 0.1, hi: 0.3 }, Ov { p: RVB_LEN, lo: 0.25, hi: 0.5 }] },
    Archetype { name: "Breath", weight: 1.5, src: S_VOWEL, rev: R_HALL | R_DENSE | R_PARALLEL | R_SHIMMER, ov: &[Ov { p: A_NOISE, lo: 0.5, hi: 1. }, Ov { p: B_NOISE, lo: 0.3, hi: 0.8 }, Ov { p: FLT_TYPE, lo: 0.3, hi: 0.7 }, Ov { p: FLT_Q, lo: 0.4, hi: 0.8 }, Ov { p: VOL_ATK, lo: 0.4, hi: 0.8 }, Ov { p: VOL_SUS, lo: 0.6, hi: 1. }, Ov { p: LFO_AMT, lo: 0.1, hi: 0.4 }, Ov { p: LFO_RATE, lo: 0.05, hi: 0.3 }, Ov { p: A_FORM, lo: 0., hi: 1. }, Ov { p: A_MOD, lo: -0.5, hi: 0.5 }, Ov { p: OSC_MIX, lo: 0.1, hi: 0.5 }, Ov { p: RVB_MIX, lo: 0.3, hi: 0.55 }, Ov { p: RVB_LEN, lo: 0.6, hi: 0.9 }] },
    Archetype { name: "Reverse Swell", weight: 1., src: S_WAVE, rev: R_REVERSE | R_SHIMMER, ov: &[Ov { p: VOL_ATK, lo: 0.6, hi: 0.85 }, Ov { p: VOL_DCY, lo: 0.5, hi: 0.8 }, Ov { p: VOL_SUS, lo: 0.3, hi: 0.7 }, Ov { p: A_FORM, lo: 0.3, hi: 1. }, Ov { p: A_MOD, lo: -0.5, hi: 0.5 }, Ov { p: MOD_ATK, lo: 0.5, hi: 0.9 }, Ov { p: FLT_FREQ, lo: 0.4, hi: 0.8 }, Ov { p: RVB_MIX, lo: 0.3, hi: 0.6 }, Ov { p: RVB_LEN, lo: 0.5, hi: 0.8 }] },
];

pub const COUNT: usize = LIST.len();

/// Which recipe a seed uses: a pure function of the seed, so the UI can show its name without growing the patch.
pub fn index_for_seed(seed: u32) -> usize {
    let mut z = (seed as u64).wrapping_mul(0xD1B54A32D192ED03).wrapping_add(0x9E3779B97F4A7C15);
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB);
    z ^= z >> 31;
    let mut total = 0.0f32;
    for a in LIST.iter() { total += a.weight; }
    let u = (z >> 40) as f32 * (1.0f32 / 16777216.0f32) * total;
    let mut acc = 0.0f32;
    for (i, a) in LIST.iter().enumerate() {
        acc += a.weight;
        if u < acc { return i; }
    }
    COUNT - 1
}

/// Recipe name for archetype seeds, None for legacy seeds.
pub fn name_for_seed(seed: u32) -> Option<&'static str> {
    if seed >= FIRST_ARCHETYPE_SEED { Some(LIST[index_for_seed(seed)].name) } else { None }
}
