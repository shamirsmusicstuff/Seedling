#pragma once
// Randomize, version 2: "archetypes".
//
// Seeds below 1,000,000 are the original generator (every older seed grows the same patch as before, with the plain
// oscillator as the sound source). Seeds from 1,000,000 up pick a *recipe* first - pad, pluck, bell, bass, drone, vowel
// choir ... - and only then fill in the knobs, each from a range that fits the recipe. The recipe decides the sound
// source, the register, the envelope shape, how bright and how wet it is, and which reverbs suit it.
//
// A recipe is a handful of range overrides on top of the generic soft ranges (kParams[].softLo/softHi); everything it
// does not mention is drawn from the generic range. The soft <-> wild slider still widens every range towards the full
// parameter range, so "wild" can leave the recipe.
//
// Pure C++ (no JUCE).

#include "Params.h"
#include <cstdint>

namespace seed
{

constexpr uint32_t kFirstArchetypeSeed = 1000000u;
constexpr uint32_t kSeedRange          = 1000000u;       // Randomize draws kFirstArchetypeSeed .. kFirstArchetypeSeed + kSeedRange - 1

namespace archetype
{
    struct Ov { int p = -1; float lo = 0.f, hi = 0.f; };

    enum Src : uint32_t { sOsc = 1u << 0, sWave = 1u << 1, sPluck = 1u << 2, sVowel = 1u << 3, sStruck = 1u << 4, sUnison = 1u << 5 };
    enum Rev : uint32_t
    {
        rPlate = 1u << 0, rVintage = 1u << 1, rSmooth = 1u << 2, rChamber = 1u << 3, rTiled = 1u << 4, rHall = 1u << 5,
        rDense = 1u << 6, rParallel = 1u << 7, rSpring = 1u << 8, rShimmer = 1u << 9, rReverse = 1u << 10, rGated = 1u << 11
    };

    struct Archetype
    {
        const char* name;
        float weight;
        uint32_t src, rev;
        Ov ov[28];
    };

    inline constexpr Archetype kList[] =
    {
        // ---- pads and beds ---------------------------------------------------------------------------------------
        { "Soft Pad", 3.f, sUnison, rHall | rDense | rSmooth | rParallel,
          { { vol_atk, .55f, .85f }, { vol_dcy, .6f, .9f }, { vol_sus, .6f, .9f }, { mod_atk, .4f, .8f }, { mod_dcy, .5f, .9f }, { a_form, .3f, .7f }, { a_color, 0.f, .35f },
            { flt_freq, .35f, .65f }, { flt_q, .05f, .3f }, { flt_mod, 0.f, .4f }, { lfo_amt, .1f, .35f }, { a_freq, -12.f, 0.f }, { osc_mix, .15f, .5f },
            { rvb_mix, .2f, .45f }, { rvb_len, .55f, .9f } } },
        { "Warm Pad", 2.f, sOsc, rHall | rSmooth | rVintage,
          { { vol_atk, .5f, .8f }, { vol_dcy, .6f, .9f }, { vol_sus, .6f, .9f }, { a_form, .3f, .7f }, { b_form, .5f, .9f }, { osc_mix, .3f, .6f }, { b_freq, -12.f, 12.f },
            { flt_freq, .3f, .6f }, { flt_mod, .1f, .5f }, { lfo_amt, .1f, .3f }, { a_freq, -12.f, 0.f }, { rvb_mix, .2f, .45f }, { rvb_len, .5f, .85f } } },
        { "Choir", 2.f, sVowel, rHall | rDense | rParallel | rShimmer,
          { { vol_atk, .5f, .8f }, { vol_dcy, .6f, .9f }, { vol_sus, .6f, .9f }, { a_form, 0.f, 1.f }, { a_mod, -.5f, .5f }, { a_color, 0.f, .5f }, { a_noise, .05f, .35f },
            { mod_atk, .3f, .7f }, { lfo_amt, .1f, .3f }, { flt_freq, .6f, .9f }, { a_freq, -12.f, 0.f }, { rvb_mix, .25f, .5f }, { rvb_len, .55f, .85f } } },
        { "Glass Pad", 2.f, sWave, rShimmer | rHall | rDense | rSmooth,
          { { vol_atk, .5f, .8f }, { vol_dcy, .6f, .9f }, { vol_sus, .55f, .85f }, { a_form, .6f, 1.f }, { a_mod, -.5f, .5f }, { mod_atk, .4f, .8f }, { flt_freq, .5f, .85f },
            { a_freq, 0.f, 14.f }, { lfo_amt, .05f, .25f }, { rvb_mix, .25f, .5f }, { rvb_len, .6f, .9f } } },
        { "Strings", 2.f, sUnison, rHall | rDense | rPlate,
          { { vol_atk, .45f, .7f }, { vol_dcy, .6f, .9f }, { vol_sus, .7f, .95f }, { a_form, .35f, .6f }, { flt_freq, .4f, .65f }, { flt_mod, 0.f, .35f }, { mod_atk, .3f, .6f },
            { lfo_amt, .1f, .3f }, { a_freq, -7.f, 7.f }, { rvb_mix, .2f, .4f }, { rvb_len, .5f, .8f } } },
        { "Drone", 1.5f, sUnison, rHall | rDense | rShimmer | rParallel,
          { { vol_atk, .75f, 1.f }, { vol_sus, .8f, 1.f }, { env_loop, .3f, .8f }, { mod_atk, .5f, .9f }, { lfo_amt, .2f, .4f }, { lfo_rate, .05f, .3f }, { a_freq, -24.f, -7.f },
            { a_form, .2f, .6f }, { flt_freq, .3f, .6f }, { flt_mod, .1f, .5f }, { rvb_mix, .3f, .55f }, { rvb_len, .7f, .95f } } },
        // ---- plucked and struck ----------------------------------------------------------------------------------
        { "Pluck", 3.f, sPluck, rPlate | rChamber | rTiled | rSpring | rVintage,
          { { vol_atk, 0.f, .1f }, { vol_dcy, .5f, .8f }, { vol_sus, .5f, .9f }, { a_form, .3f, .8f }, { a_color, .35f, .8f }, { a_noise, .1f, .6f }, { a_mod, 0.f, .4f },
            { flt_freq, .55f, .9f }, { flt_mod, 0.f, .4f }, { a_freq, -7.f, 7.f }, { rvb_mix, .12f, .35f }, { rvb_len, .3f, .6f } } },
        { "Harp", 1.5f, sPluck, rHall | rPlate | rSmooth,
          { { vol_atk, 0.f, .05f }, { vol_dcy, .6f, .9f }, { vol_sus, .6f, 1.f }, { a_form, .2f, .5f }, { a_color, .7f, .95f }, { a_noise, 0.f, .3f }, { flt_freq, .6f, .9f },
            { a_freq, 0.f, 12.f }, { rvb_mix, .2f, .45f }, { rvb_len, .5f, .85f } } },
        { "Marimba", 2.f, sStruck, rChamber | rTiled | rPlate | rSpring | rGated,
          { { vol_atk, 0.f, .05f }, { vol_dcy, .3f, .6f }, { vol_sus, 0.f, .3f }, { a_form, .2f, .3f }, { a_color, .2f, .5f }, { a_noise, 0.f, .1f }, { flt_freq, .6f, .9f },
            { a_freq, -7.f, 7.f }, { osc_mix, 0.f, .2f }, { rvb_mix, .1f, .3f }, { rvb_len, .3f, .55f } } },
        { "Glockenspiel", 1.5f, sStruck, rPlate | rHall | rShimmer | rSmooth,
          { { vol_atk, 0.f, .05f }, { vol_dcy, .4f, .7f }, { vol_sus, 0.f, .2f }, { a_form, .45f, .55f }, { a_color, .35f, .7f }, { flt_freq, .7f, .95f }, { a_freq, 7.f, 14.f },
            { osc_mix, 0.f, .2f }, { rvb_mix, .2f, .45f }, { rvb_len, .45f, .8f } } },
        { "Bell", 2.f, sStruck, rHall | rDense | rShimmer | rParallel,
          { { vol_atk, 0.f, .05f }, { vol_dcy, .5f, .85f }, { vol_sus, 0.f, .2f }, { a_form, .7f, .8f }, { a_color, .55f, .9f }, { a_noise, 0.f, .1f }, { flt_freq, .6f, .9f },
            { a_freq, -7.f, 7.f }, { osc_mix, 0.f, .25f }, { rvb_mix, .25f, .5f }, { rvb_len, .5f, .85f } } },
        { "Gong", 1.f, sStruck, rHall | rDense | rParallel | rReverse,
          { { vol_atk, 0.f, .08f }, { vol_dcy, .6f, .9f }, { vol_sus, 0.f, .2f }, { a_form, .9f, 1.f }, { a_color, .5f, .9f }, { a_freq, -12.f, 0.f }, { flt_freq, .4f, .75f },
            { osc_mix, 0.f, .3f }, { rvb_mix, .25f, .5f }, { rvb_len, .55f, .9f } } },
        // ---- keys, organ, wavetable ------------------------------------------------------------------------------
        { "Glass Keys", 2.f, sWave, rPlate | rChamber | rHall | rSmooth,
          { { vol_atk, 0.f, .2f }, { vol_dcy, .4f, .75f }, { vol_sus, .05f, .4f }, { a_form, .75f, 1.f }, { a_mod, -.4f, .1f }, { mod_dcy, .3f, .6f }, { flt_freq, .6f, .9f },
            { a_freq, 0.f, 12.f }, { rvb_mix, .15f, .4f }, { rvb_len, .35f, .65f } } },
        { "Organ", 1.5f, sWave, rSpring | rVintage | rTiled | rChamber,
          { { vol_atk, 0.f, .08f }, { vol_dcy, .8f, 1.f }, { vol_sus, .85f, 1.f }, { a_form, .2f, .45f }, { a_mod, 0.f, .2f }, { lfo_amt, .1f, .35f }, { lfo_rate, .35f, .6f },
            { flt_freq, .65f, .95f }, { a_freq, -12.f, 0.f }, { rvb_mix, .1f, .3f }, { rvb_len, .3f, .55f } } },
        { "FM Keys", 2.f, sOsc, rPlate | rChamber | rSpring | rVintage | rTiled,
          { { fm_amt, .3f, .7f }, { fm_mod, -.7f, -.3f }, { a_form, 0.f, .1f }, { b_form, .5f, .65f }, { vol_atk, 0.f, .08f }, { vol_dcy, .4f, .7f }, { vol_sus, .05f, .35f },
            { mod_dcy, .3f, .6f }, { b_freq, -12.f, 12.f }, { flt_freq, .6f, .9f }, { rvb_mix, .1f, .3f }, { rvb_len, .3f, .6f } } },
        // ---- bass, lead, brass -----------------------------------------------------------------------------------
        { "Warm Bass", 2.5f, sOsc, rPlate | rChamber | rTiled | rVintage,
          { { a_freq, -24.f, -12.f }, { b_freq, -24.f, -12.f }, { sub_am, .3f, .7f }, { a_form, .1f, .7f }, { b_form, .4f, 1.f }, { osc_mix, .1f, .45f }, { vol_atk, 0.f, .1f },
            { vol_dcy, .5f, .8f }, { vol_sus, .6f, .9f }, { flt_freq, .25f, .5f }, { flt_q, .1f, .4f }, { flt_mod, .2f, .6f }, { mod_dcy, .3f, .6f }, { rvb_mix, 0.f, .15f },
            { rvb_len, .2f, .45f } } },
        { "Plucked Bass", 1.5f, sPluck, rPlate | rChamber | rTiled,
          { { a_freq, -24.f, -12.f }, { vol_atk, 0.f, .05f }, { vol_dcy, .4f, .7f }, { vol_sus, .3f, .8f }, { a_form, .2f, .6f }, { a_color, .3f, .6f }, { sub_am, .2f, .6f },
            { b_freq, -24.f, -12.f }, { osc_mix, .1f, .4f }, { flt_freq, .35f, .65f }, { rvb_mix, 0.f, .15f }, { rvb_len, .2f, .4f } } },
        { "Pulse Lead", 2.f, sOsc, rPlate | rVintage | rSpring | rChamber,
          { { a_form, .85f, 1.f }, { b_form, 0.f, .35f }, { osc_mix, .2f, .5f }, { b_freq, -12.f, 12.f }, { vol_atk, 0.f, .1f }, { vol_sus, .7f, 1.f }, { lfo_amt, .2f, .5f },
            { lfo_dly, .3f, .7f }, { flt_q, .25f, .55f }, { flt_freq, .5f, .85f }, { a_freq, 0.f, 12.f }, { rvb_mix, .1f, .3f }, { rvb_len, .3f, .6f } } },
        { "Brass Swell", 1.5f, sOsc, rHall | rPlate | rSmooth,
          { { a_form, .6f, .7f }, { b_form, .5f, .9f }, { osc_mix, .3f, .6f }, { vol_atk, .3f, .55f }, { mod_atk, .3f, .6f }, { flt_mod, .4f, .9f }, { flt_freq, .3f, .55f },
            { saturate, .1f, .3f }, { vol_sus, .6f, .9f }, { a_freq, -7.f, 5.f }, { rvb_mix, .15f, .35f }, { rvb_len, .4f, .7f } } },
        { "Arp Stab", 1.5f, sUnison, rPlate | rChamber | rTiled | rSpring | rGated,
          { { vol_atk, 0.f, .05f }, { vol_dcy, .2f, .45f }, { vol_sus, 0.f, .15f }, { mod_dcy, .2f, .5f }, { flt_mod, .5f, 1.f }, { flt_freq, .35f, .65f }, { a_form, .1f, .5f },
            { a_freq, 0.f, 12.f }, { rvb_mix, .1f, .3f }, { rvb_len, .25f, .5f } } },
        // ---- textures --------------------------------------------------------------------------------------------
        { "Breath", 1.5f, sVowel, rHall | rDense | rParallel | rShimmer,
          { { a_noise, .5f, 1.f }, { b_noise, .3f, .8f }, { flt_type, .3f, .7f }, { flt_q, .4f, .8f }, { vol_atk, .4f, .8f }, { vol_sus, .6f, 1.f }, { lfo_amt, .1f, .4f },
            { lfo_rate, .05f, .3f }, { a_form, 0.f, 1.f }, { a_mod, -.5f, .5f }, { osc_mix, .1f, .5f }, { rvb_mix, .3f, .55f }, { rvb_len, .6f, .9f } } },
        { "Reverse Swell", 1.f, sWave, rReverse | rShimmer,
          { { vol_atk, .6f, .85f }, { vol_dcy, .5f, .8f }, { vol_sus, .3f, .7f }, { a_form, .3f, 1.f }, { a_mod, -.5f, .5f }, { mod_atk, .5f, .9f }, { flt_freq, .4f, .8f },
            { rvb_mix, .3f, .6f }, { rvb_len, .5f, .8f } } },
    };

    constexpr int kCount = (int) (sizeof (kList) / sizeof (kList[0]));

    // Which recipe a seed uses: a pure function of the seed, so the UI can show its name without growing the patch.
    inline int indexForSeed (uint32_t seedValue)
    {
        uint64_t z = (uint64_t) seedValue * 0xD1B54A32D192ED03ull + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        float total = 0.f;
        for (const auto& a : kList) total += a.weight;
        const float u = (float) (z >> 40) * (1.0f / 16777216.0f) * total;
        float acc = 0.f;
        for (int i = 0; i < kCount; ++i) { acc += kList[i].weight; if (u < acc) return i; }
        return kCount - 1;
    }

    inline const char* nameForSeed (uint32_t seedValue)
    {
        return seedValue >= kFirstArchetypeSeed ? kList[indexForSeed (seedValue)].name : nullptr;
    }
}

} // namespace seed
