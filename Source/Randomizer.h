#pragma once
// Seeded random patch generator + the "Correct Tune" function. Pure C++ (no JUCE).
// The generator uses its own PRNG so the same seed gives the same patch on every OS/compiler.

#include "Params.h"
#include <cmath>
#include <cstdint>

namespace seed
{

struct SplitMix
{
    uint64_t s;
    explicit SplitMix (uint64_t seedValue) : s (seedValue) {}
    uint64_t nextU64()
    {
        s += 0x9E3779B97F4A7C15ull;
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float next() { return (float) (nextU64() >> 40) * (1.0f / 16777216.0f); }   // [0,1)
};

// ---- Correct Tune ------------------------------------------------------------
// Oscillator A is snapped to the nearest octave (so the keys play the right notes),
// oscillator B to the nearest perfect interval (unison, 4th, 5th, octaves) so it
// still sits in tune with the keys and with A (also keeps FM sidebands musical).
inline float snapToOctave (float semitones) { return std::round (semitones / 12.f) * 12.f; }

inline float snapToConsonant (float semitones)
{
    static const float set[] = { -24.f, -19.f, -12.f, -7.f, -5.f, 0.f, 5.f, 7.f, 12.f, 19.f, 24.f };
    float best = set[0];
    for (float s : set)
        if (std::fabs (s - semitones) < std::fabs (best - semitones)) best = s;
    return best;
}

inline void correctTune (float* values)
{
    values[a_freq] = snapToOctave (values[a_freq]);
    values[b_freq] = snapToConsonant (values[b_freq]);
}

// ---- Randomize ---------------------------------------------------------------
// chaos 0 = soft and light (narrow, centre-weighted ranges), 1 = anything goes.
inline void randomizeValues (uint32_t seedValue, float chaos, float* out)
{
    chaos = chaos < 0.f ? 0.f : (chaos > 1.f ? 1.f : chaos);
    SplitMix rng ((uint64_t) seedValue * 0x9E3779B1ull + 0x51ED270Bull);
    rng.next();

    for (int i = 0; i < kNumParams; ++i)
    {
        const ParamDef& p = kParams[i];
        const float lo = p.softLo + (p.lo - p.softLo) * chaos;
        const float hi = p.softHi + (p.hi - p.softHi) * chaos;

        const float u1 = rng.next(), u2 = rng.next(), gate = rng.next();   // always consume 3 -> stable stream
        const float tri = 0.5f * (u1 + u2);
        const float u = tri + (u1 - tri) * chaos;                          // triangular -> uniform
        float v = lo + (hi - lo) * u;

        if ((p.flags & fOffBias) && gate < 0.6f - 0.45f * chaos)
            v = p.def;

        // the "*" knobs that are not about pitch stay close to their defaults
        if ((p.flags & fStar) && ! (p.flags & fSemitone))
            v = p.def + (v - p.def) * 0.5f;

        if (p.flags & fStep) v = std::round (v);
        if (p.flags & fNoRandom) v = p.def;
        out[i] = v < p.lo ? p.lo : (v > p.hi ? p.hi : v);
    }
}

} // namespace seed
