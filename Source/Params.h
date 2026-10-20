#pragma once
// Parameter table for "Seedling" - every knob from the three pictured pages.
// Pure C++ (no JUCE) so the engine, randomizer and tests can share it.

#include <cstdint>

namespace seed
{

enum Group : uint8_t { gEnv, gVol, gMod, gLfo, gOscA, gFm, gMix, gOscB, gFlt, gSat, gRvb, gAdj };

enum Flags : uint8_t
{
    fBipolar  = 1,  // arc is drawn from the centre
    fStar     = 2,  // the "*" knobs in the pictures: randomizer keeps these conservative
    fOffBias  = 4,  // randomizer often leaves this one at its default (off)
    fSemitone = 8   // value is in semitones (tuning-sensitive)
};

struct ParamDef
{
    const char* id;      // shown under the knob, also the host parameter ID
    const char* name;    // friendly name for the host
    float lo, hi, def;   // real range and default
    float softLo, softHi;// range the randomizer draws from at "soft" (chaos = 0)
    uint8_t group;
    uint8_t page;        // 0 = I. Envelope and LFO, 1 = II. Oscillators, 2 = III. Filter and effects
    uint8_t flags;
};

enum P : int
{
    // ---- Page I ------------------------------------------------------------
    env_time, env_loop, env_tilt, env_kf,
    vol_atk, vol_dcy, vol_sus, vol_fade,
    mod_atk, mod_dcy, mod_sh, mod_vel,
    lfo_rate, lfo_amt, lfo_bal, lfo_dly,
    // ---- Page II -----------------------------------------------------------
    a_form, a_noise, a_mod, a_color, a_freq,
    fm_mod, fm_amt,
    mix_mod, osc_mix,
    b_form, b_noise, b_mod, sub_am, b_freq, b_sh,
    // ---- Page III ----------------------------------------------------------
    flt_type, flt_q, flt_mod, flt_sep, flt_freq, flt_kf,
    saturate,
    rvb_mix, rvb_atk, rvb_len, rvb_damp, rvb_chor, rvb_size,
    adj_bass, adj_treb, adj_pan, adj_clip,

    kNumParams
};

inline constexpr ParamDef kParams[kNumParams] =
{
    // id          name                 lo     hi    def    softLo softHi group  page flags
    { "env_time",  "Env Time",          0.f,   1.f,  0.5f,  0.35f, 0.70f, gEnv,  0, 0 },
    { "env_loop",  "Env Loop",          0.f,   1.f,  0.0f,  0.00f, 0.30f, gEnv,  0, fOffBias },
    { "env_tilt",  "Env Tilt",          0.f,   1.f,  0.5f,  0.30f, 0.70f, gEnv,  0, 0 },
    { "env_kf",    "Env Key Follow",    0.f,   1.f,  0.0f,  0.00f, 0.50f, gEnv,  0, 0 },

    { "vol_atk",   "Vol Attack",        0.f,   1.f,  0.15f, 0.25f, 0.70f, gVol,  0, 0 },
    { "vol_dcy",   "Vol Decay",         0.f,   1.f,  0.5f,  0.30f, 0.90f, gVol,  0, 0 },
    { "vol_sus",   "Vol Sustain",       0.f,   1.f,  0.7f,  0.40f, 0.85f, gVol,  0, 0 },
    { "vol_fade",  "Vol Fade",          0.f,   1.f,  0.0f,  0.00f, 0.50f, gVol,  0, fStar | fOffBias },

    { "mod_atk",   "Mod Attack",        0.f,   1.f,  0.05f, 0.00f, 0.60f, gMod,  0, 0 },
    { "mod_dcy",   "Mod Decay",         0.f,   1.f,  0.5f,  0.20f, 0.80f, gMod,  0, 0 },
    { "mod_sh",    "Mod Shape",         0.f,   1.f,  0.5f,  0.20f, 0.80f, gMod,  0, 0 },
    { "mod_vel",   "Mod Velocity",      0.f,   1.f,  0.3f,  0.00f, 0.60f, gMod,  0, 0 },

    { "lfo_rate",  "LFO Rate",          0.f,   1.f,  0.3f,  0.10f, 0.50f, gLfo,  0, 0 },
    { "lfo_amt",   "LFO Amount",        0.f,   1.f,  0.0f,  0.00f, 0.35f, gLfo,  0, fOffBias },
    { "lfo_bal",   "LFO Balance",       0.f,   1.f,  0.0f,  0.00f, 1.00f, gLfo,  0, 0 },
    { "lfo_dly",   "LFO Delay",         0.f,   1.f,  0.3f,  0.00f, 0.60f, gLfo,  0, 0 },

    { "a_form",    "A Form",            0.f,   1.f,  0.0f,  0.00f, 0.45f, gOscA, 1, 0 },
    { "a_noise",   "A Noise",           0.f,   1.f,  0.0f,  0.00f, 0.15f, gOscA, 1, fOffBias },
    { "a_mod",     "A Mod",            -1.f,   1.f,  0.0f, -0.40f, 0.40f, gOscA, 1, fBipolar },
    { "a_color",   "A Color",           0.f,   1.f,  0.0f,  0.00f, 0.50f, gOscA, 1, 0 },
    { "a_freq",    "A Freq (st)",     -24.f,  24.f,  0.0f, -7.00f,14.00f, gOscA, 1, fStar | fBipolar | fSemitone },

    { "fm_mod",    "FM Mod",           -1.f,   1.f,  0.0f, -0.50f, 0.50f, gFm,   1, fBipolar },
    { "fm_amt",    "FM Amount",         0.f,   1.f,  0.0f,  0.00f, 0.35f, gFm,   1, fOffBias },

    { "mix_mod",   "Mix Mod",          -1.f,   1.f,  0.0f, -0.40f, 0.40f, gMix,  1, fBipolar },
    { "osc_mix",   "Osc Mix (A-B)",     0.f,   1.f,  0.2f,  0.10f, 0.90f, gMix,  1, 0 },

    { "b_form",    "B Form",            0.f,   1.f,  0.5f,  0.00f, 0.60f, gOscB, 1, 0 },
    { "b_noise",   "B Noise",           0.f,   1.f,  0.0f,  0.00f, 0.20f, gOscB, 1, fOffBias },
    { "b_mod",     "B Mod",            -1.f,   1.f,  0.0f, -0.40f, 0.40f, gOscB, 1, fBipolar },
    { "sub_am",    "Sub Amount",        0.f,   1.f,  0.0f,  0.00f, 0.50f, gOscB, 1, fOffBias },
    { "b_freq",    "B Freq (st)",     -24.f,  24.f,  0.0f,-12.00f,12.00f, gOscB, 1, fBipolar | fSemitone },
    { "b_sh",      "B Sample&Hold",     0.f,   1.f,  0.0f,  0.00f, 0.25f, gOscB, 1, fOffBias },

    { "flt_type",  "Filter Type",       0.f,   1.f,  0.0f,  0.00f, 0.40f, gFlt,  2, 0 },
    { "flt_q",     "Filter Q",          0.f,   1.f,  0.2f,  0.10f, 0.50f, gFlt,  2, 0 },
    { "flt_mod",   "Filter Mod",       -1.f,   1.f,  0.3f, -0.20f, 0.60f, gFlt,  2, fBipolar },
    { "flt_sep",   "Filter Separation", 0.f,   1.f,  0.0f,  0.00f, 0.40f, gFlt,  2, fOffBias },
    { "flt_freq",  "Filter Freq",       0.f,   1.f,  0.75f, 0.45f, 0.85f, gFlt,  2, 0 },
    { "flt_kf",    "Filter Key Follow", 0.f,   1.f,  0.5f,  0.20f, 0.80f, gFlt,  2, 0 },

    { "saturate",  "Saturate",          0.f,   1.f,  0.0f,  0.00f, 0.25f, gSat,  2, fOffBias },

    { "rvb_mix",   "Reverb Mix",        0.f,   1.f,  0.3f,  0.20f, 0.60f, gRvb,  2, 0 },
    { "rvb_atk",   "Reverb Attack",     0.f,   1.f,  0.2f,  0.00f, 0.50f, gRvb,  2, 0 },
    { "rvb_len",   "Reverb Length",     0.f,   1.f,  0.5f,  0.30f, 0.80f, gRvb,  2, 0 },
    { "rvb_damp",  "Reverb Damping",    0.f,   1.f,  0.5f,  0.30f, 0.80f, gRvb,  2, 0 },
    { "rvb_chor",  "Reverb Chorus",     0.f,   1.f,  0.3f,  0.10f, 0.70f, gRvb,  2, 0 },
    { "rvb_size",  "Reverb Size",       0.f,   1.f,  0.5f,  0.30f, 0.80f, gRvb,  2, 0 },

    { "adj_bass",  "Adjust Bass",      -1.f,   1.f,  0.0f, -0.30f, 0.20f, gAdj,  2, fBipolar },
    { "adj_treb",  "Adjust Treble",    -1.f,   1.f,  0.0f,  0.00f, 0.40f, gAdj,  2, fBipolar },
    { "adj_pan",   "Adjust Pan",       -1.f,   1.f,  0.0f, -0.25f, 0.25f, gAdj,  2, fBipolar },
    { "adj_clip",  "Adjust Clip",       0.f,   1.f,  0.0f,  0.00f, 0.10f, gAdj,  2, fStar | fOffBias },
};

inline constexpr const char* kPageTitles[3] = { "Envelope and LFO", "Oscillators", "Filter and effects" };

// Extra (non-synth) parameters that live next to the synth ones in the plugin state.
inline constexpr const char* kChaosId    = "chaos";      // 0 = soft ... 1 = wild randomization
inline constexpr const char* kAutoTuneId = "auto_tune";  // run Correct Tune after every Randomize

} // namespace seed
