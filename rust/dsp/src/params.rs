//! Parameter table for Seedling: every knob from the three pages (ported from Params.h).
//! The order is the seed order: new parameters are only ever appended.

pub const BIPOLAR: u8 = 1;
pub const STAR: u8 = 2;
pub const OFF_BIAS: u8 = 4;
pub const SEMITONE: u8 = 8;
pub const SWITCH: u8 = 16;
pub const NO_RANDOM: u8 = 32;
pub const STEP: u8 = 64;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Group { Env, Vol, Mod, Lfo, OscA, Fm, Mix, OscB, Flt, Sat, Rvb, Adj, Play }

#[derive(Clone, Copy, Debug)]
pub struct ParamDef {
    pub id: &'static str,
    pub name: &'static str,
    pub lo: f32, pub hi: f32, pub def: f32,
    /// range the randomizer draws from at "soft" (chaos = 0)
    pub soft_lo: f32, pub soft_hi: f32,
    pub group: Group,
    /// 0 = I. Envelope and LFO, 1 = II. Oscillators, 2 = III. Filter and effects
    pub page: u8,
    pub flags: u8,
}

pub mod idx {
    pub const ENV_TIME: usize = 0;
    pub const ENV_LOOP: usize = 1;
    pub const ENV_TILT: usize = 2;
    pub const ENV_KF: usize = 3;
    pub const VOL_ATK: usize = 4;
    pub const VOL_DCY: usize = 5;
    pub const VOL_SUS: usize = 6;
    pub const VOL_FADE: usize = 7;
    pub const MOD_ATK: usize = 8;
    pub const MOD_DCY: usize = 9;
    pub const MOD_SH: usize = 10;
    pub const MOD_VEL: usize = 11;
    pub const LFO_RATE: usize = 12;
    pub const LFO_AMT: usize = 13;
    pub const LFO_BAL: usize = 14;
    pub const LFO_DLY: usize = 15;
    pub const A_FORM: usize = 16;
    pub const A_NOISE: usize = 17;
    pub const A_MOD: usize = 18;
    pub const A_COLOR: usize = 19;
    pub const A_FREQ: usize = 20;
    pub const FM_MOD: usize = 21;
    pub const FM_AMT: usize = 22;
    pub const MIX_MOD: usize = 23;
    pub const OSC_MIX: usize = 24;
    pub const B_FORM: usize = 25;
    pub const B_NOISE: usize = 26;
    pub const B_MOD: usize = 27;
    pub const SUB_AM: usize = 28;
    pub const B_FREQ: usize = 29;
    pub const B_SH: usize = 30;
    pub const FLT_TYPE: usize = 31;
    pub const FLT_Q: usize = 32;
    pub const FLT_MOD: usize = 33;
    pub const FLT_SEP: usize = 34;
    pub const FLT_FREQ: usize = 35;
    pub const FLT_KF: usize = 36;
    pub const SATURATE: usize = 37;
    pub const RVB_MIX: usize = 38;
    pub const RVB_ATK: usize = 39;
    pub const RVB_LEN: usize = 40;
    pub const RVB_DAMP: usize = 41;
    pub const RVB_CHOR: usize = 42;
    pub const RVB_SIZE: usize = 43;
    pub const ADJ_BASS: usize = 44;
    pub const ADJ_TREB: usize = 45;
    pub const ADJ_PAN: usize = 46;
    pub const ADJ_CLIP: usize = 47;
    pub const GLIDE: usize = 48;
    pub const MONO: usize = 49;
    pub const RVB_TYPE: usize = 50;
    pub const SRC_TYPE: usize = 51;
}

pub const NUM_PARAMS: usize = 52;

pub const PARAMS: [ParamDef; NUM_PARAMS] = [
    ParamDef { id: "env_time", name: "Env Time", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.35, soft_hi: 0.7, group: Group::Env, page: 0, flags: 0 },
    ParamDef { id: "env_loop", name: "Env Loop", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.3, group: Group::Env, page: 0, flags: OFF_BIAS },
    ParamDef { id: "env_tilt", name: "Env Tilt", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.3, soft_hi: 0.7, group: Group::Env, page: 0, flags: 0 },
    ParamDef { id: "env_kf", name: "Env Key Follow", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.5, group: Group::Env, page: 0, flags: 0 },
    ParamDef { id: "vol_atk", name: "Vol Attack", lo: 0.0, hi: 1.0, def: 0.15, soft_lo: 0.25, soft_hi: 0.7, group: Group::Vol, page: 0, flags: 0 },
    ParamDef { id: "vol_dcy", name: "Vol Decay", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.3, soft_hi: 0.9, group: Group::Vol, page: 0, flags: 0 },
    ParamDef { id: "vol_sus", name: "Vol Sustain", lo: 0.0, hi: 1.0, def: 0.7, soft_lo: 0.4, soft_hi: 0.85, group: Group::Vol, page: 0, flags: 0 },
    ParamDef { id: "vol_fade", name: "Vol Fade", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.5, group: Group::Vol, page: 0, flags: STAR | OFF_BIAS },
    ParamDef { id: "mod_atk", name: "Mod Attack", lo: 0.0, hi: 1.0, def: 0.05, soft_lo: 0.0, soft_hi: 0.6, group: Group::Mod, page: 0, flags: 0 },
    ParamDef { id: "mod_dcy", name: "Mod Decay", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.2, soft_hi: 0.8, group: Group::Mod, page: 0, flags: 0 },
    ParamDef { id: "mod_sh", name: "Mod Shape", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.2, soft_hi: 0.8, group: Group::Mod, page: 0, flags: 0 },
    ParamDef { id: "mod_vel", name: "Mod Velocity", lo: 0.0, hi: 1.0, def: 0.3, soft_lo: 0.0, soft_hi: 0.6, group: Group::Mod, page: 0, flags: 0 },
    ParamDef { id: "lfo_rate", name: "LFO Rate", lo: 0.0, hi: 1.0, def: 0.3, soft_lo: 0.1, soft_hi: 0.5, group: Group::Lfo, page: 0, flags: 0 },
    ParamDef { id: "lfo_amt", name: "LFO Amount", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.35, group: Group::Lfo, page: 0, flags: OFF_BIAS },
    ParamDef { id: "lfo_bal", name: "LFO Balance", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 1.0, group: Group::Lfo, page: 0, flags: 0 },
    ParamDef { id: "lfo_dly", name: "LFO Delay", lo: 0.0, hi: 1.0, def: 0.3, soft_lo: 0.0, soft_hi: 0.6, group: Group::Lfo, page: 0, flags: 0 },
    ParamDef { id: "a_form", name: "A Form", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.45, group: Group::OscA, page: 1, flags: 0 },
    ParamDef { id: "a_noise", name: "A Noise", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.15, group: Group::OscA, page: 1, flags: OFF_BIAS },
    ParamDef { id: "a_mod", name: "A Mod", lo: -1.0, hi: 1.0, def: 0.0, soft_lo: -0.4, soft_hi: 0.4, group: Group::OscA, page: 1, flags: BIPOLAR },
    ParamDef { id: "a_color", name: "A Color", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.5, group: Group::OscA, page: 1, flags: 0 },
    ParamDef { id: "a_freq", name: "A Freq (st)", lo: -24.0, hi: 24.0, def: 0.0, soft_lo: -7.0, soft_hi: 14.0, group: Group::OscA, page: 1, flags: STAR | BIPOLAR | SEMITONE },
    ParamDef { id: "fm_mod", name: "FM Mod", lo: -1.0, hi: 1.0, def: 0.0, soft_lo: -0.5, soft_hi: 0.5, group: Group::Fm, page: 1, flags: BIPOLAR },
    ParamDef { id: "fm_amt", name: "FM Amount", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.35, group: Group::Fm, page: 1, flags: OFF_BIAS },
    ParamDef { id: "mix_mod", name: "Mix Mod", lo: -1.0, hi: 1.0, def: 0.0, soft_lo: -0.4, soft_hi: 0.4, group: Group::Mix, page: 1, flags: BIPOLAR },
    ParamDef { id: "osc_mix", name: "Osc Mix (A-B)", lo: 0.0, hi: 1.0, def: 0.2, soft_lo: 0.1, soft_hi: 0.9, group: Group::Mix, page: 1, flags: 0 },
    ParamDef { id: "b_form", name: "B Form", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.0, soft_hi: 0.6, group: Group::OscB, page: 1, flags: 0 },
    ParamDef { id: "b_noise", name: "B Noise", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.2, group: Group::OscB, page: 1, flags: OFF_BIAS },
    ParamDef { id: "b_mod", name: "B Mod", lo: -1.0, hi: 1.0, def: 0.0, soft_lo: -0.4, soft_hi: 0.4, group: Group::OscB, page: 1, flags: BIPOLAR },
    ParamDef { id: "sub_am", name: "Sub Amount", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.5, group: Group::OscB, page: 1, flags: OFF_BIAS },
    ParamDef { id: "b_freq", name: "B Freq (st)", lo: -24.0, hi: 24.0, def: 0.0, soft_lo: -12.0, soft_hi: 12.0, group: Group::OscB, page: 1, flags: BIPOLAR | SEMITONE },
    ParamDef { id: "b_sh", name: "B Sample&Hold", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.25, group: Group::OscB, page: 1, flags: OFF_BIAS },
    ParamDef { id: "flt_type", name: "Filter Type", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.4, group: Group::Flt, page: 2, flags: 0 },
    ParamDef { id: "flt_q", name: "Filter Q", lo: 0.0, hi: 1.0, def: 0.2, soft_lo: 0.1, soft_hi: 0.5, group: Group::Flt, page: 2, flags: 0 },
    ParamDef { id: "flt_mod", name: "Filter Mod", lo: -1.0, hi: 1.0, def: 0.3, soft_lo: -0.2, soft_hi: 0.6, group: Group::Flt, page: 2, flags: BIPOLAR },
    ParamDef { id: "flt_sep", name: "Filter Separation", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.4, group: Group::Flt, page: 2, flags: OFF_BIAS },
    ParamDef { id: "flt_freq", name: "Filter Freq", lo: 0.0, hi: 1.0, def: 0.75, soft_lo: 0.45, soft_hi: 0.85, group: Group::Flt, page: 2, flags: 0 },
    ParamDef { id: "flt_kf", name: "Filter Key Follow", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.2, soft_hi: 0.8, group: Group::Flt, page: 2, flags: 0 },
    ParamDef { id: "saturate", name: "Saturate", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.25, group: Group::Sat, page: 2, flags: OFF_BIAS },
    ParamDef { id: "rvb_mix", name: "Reverb Mix", lo: 0.0, hi: 1.0, def: 0.3, soft_lo: 0.2, soft_hi: 0.6, group: Group::Rvb, page: 2, flags: 0 },
    ParamDef { id: "rvb_atk", name: "Reverb Attack", lo: 0.0, hi: 1.0, def: 0.2, soft_lo: 0.0, soft_hi: 0.5, group: Group::Rvb, page: 2, flags: 0 },
    ParamDef { id: "rvb_len", name: "Reverb Length", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.3, soft_hi: 0.8, group: Group::Rvb, page: 2, flags: 0 },
    ParamDef { id: "rvb_damp", name: "Reverb Damping", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.3, soft_hi: 0.8, group: Group::Rvb, page: 2, flags: 0 },
    ParamDef { id: "rvb_chor", name: "Reverb Chorus", lo: 0.0, hi: 1.0, def: 0.3, soft_lo: 0.1, soft_hi: 0.7, group: Group::Rvb, page: 2, flags: 0 },
    ParamDef { id: "rvb_size", name: "Reverb Size", lo: 0.0, hi: 1.0, def: 0.5, soft_lo: 0.3, soft_hi: 0.8, group: Group::Rvb, page: 2, flags: 0 },
    ParamDef { id: "adj_bass", name: "Adjust Bass", lo: -1.0, hi: 1.0, def: 0.0, soft_lo: -0.3, soft_hi: 0.2, group: Group::Adj, page: 2, flags: BIPOLAR },
    ParamDef { id: "adj_treb", name: "Adjust Treble", lo: -1.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.4, group: Group::Adj, page: 2, flags: BIPOLAR },
    ParamDef { id: "adj_pan", name: "Adjust Pan", lo: -1.0, hi: 1.0, def: 0.0, soft_lo: -0.25, soft_hi: 0.25, group: Group::Adj, page: 2, flags: BIPOLAR },
    ParamDef { id: "adj_clip", name: "Adjust Clip", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.1, group: Group::Adj, page: 2, flags: STAR | OFF_BIAS },
    ParamDef { id: "glide", name: "Glide", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 0.3, group: Group::Play, page: 1, flags: NO_RANDOM },
    ParamDef { id: "mono", name: "Mono", lo: 0.0, hi: 1.0, def: 0.0, soft_lo: 0.0, soft_hi: 1.0, group: Group::Play, page: 1, flags: SWITCH | NO_RANDOM },
    ParamDef { id: "rvb_type", name: "Reverb Type", lo: 0.0, hi: 11.0, def: 0.0, soft_lo: 0.0, soft_hi: 11.0, group: Group::Rvb, page: 2, flags: STEP },
    ParamDef { id: "src_type", name: "Sound Source", lo: 0.0, hi: 5.0, def: 0.0, soft_lo: 0.0, soft_hi: 5.0, group: Group::OscA, page: 1, flags: STEP },
];

pub const NUM_SOURCES: usize = 6;
pub const SOURCE_NAMES: [&str; NUM_SOURCES] = ["Oscillator", "Wavetable", "Pluck", "Vowel", "Struck", "Unison"];
pub const NUM_REVERB_TYPES: usize = 12;
pub const REVERB_NAMES: [&str; NUM_REVERB_TYPES] = [
    "Plate", "Vintage Plate", "Smooth Plate", "Chamber", "Tiled Room", "Hall",
    "Dense Hall", "Parallel Hall", "Spring", "Shimmer", "Reverse", "Gated",
];
pub const PAGE_TITLES: [&str; 3] = ["Envelope and LFO", "Oscillators", "Filter and effects"];
