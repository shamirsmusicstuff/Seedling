// Randomize v2 (archetypes): deterministic, every recipe is reachable, ranges are respected, old seeds untouched,
// and the results are far more varied than the old generator's. Plain C++, no JUCE.
//   g++ -std=c++17 -I Source Tests/archetype_test.cpp -o archetype_test && ./archetype_test
#include "Params.h"
#include "Randomizer.h"
#include <cstdio>
#include <cmath>
#include <map>
#include <vector>
using namespace seed;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

// spread of a set of patches: mean distance between random pairs in "normalised knob space" (range-scaled)
static double spread (const std::vector<std::vector<float>>& ps)
{
    double acc = 0; int n = 0;
    for (size_t a = 0; a + 7 < ps.size(); a += 3)
    {
        const auto& x = ps[a]; const auto& y = ps[a + 7];
        double d = 0; int k = 0;
        for (int i = 0; i < kNumParams; ++i)
        {
            if (kParams[i].flags & (fNoRandom | fSwitch)) continue;
            d += std::fabs (x[(size_t) i] - y[(size_t) i]) / (kParams[i].hi - kParams[i].lo); ++k;
        }
        acc += d / k; ++n;
    }
    return acc / n;
}

int main()
{
    float v[kNumParams], w[kNumParams];

    // 1. the seed number picks the generator
    randomizeValues (29u, 0.2f, v);
    CHECK (v[src_type] == 0.f, "old seeds keep the oscillator source");
    randomizeValues (kFirstArchetypeSeed + 29u, 0.2f, v);
    randomizeValues (kFirstArchetypeSeed + 29u, 0.2f, w);
    bool same = true; for (int i = 0; i < kNumParams; ++i) same = same && v[i] == w[i];
    CHECK (same, "archetype generator not deterministic");

    // 2. every recipe is reachable; names exist
    std::map<int, int> hits, srcHits, revHits;
    std::vector<std::vector<float>> newPatches, oldPatches;
    int outOfRange = 0, outsideRecipe = 0, badSrc = 0, badRev = 0;
    const uint32_t N = 30000;
    for (uint32_t i = 0; i < N; ++i)
    {
        const uint32_t sd = kFirstArchetypeSeed + i * 31u + 5u;
        randomizeValues (sd, 0.f, v);
        const int a = archetype::indexForSeed (sd);
        ++hits[a]; ++srcHits[(int) v[src_type]]; ++revHits[(int) v[rvb_type]];
        const auto& A = archetype::kList[a];
        for (int k = 0; k < kNumParams; ++k)
        {
            if (v[k] < kParams[k].lo - 1e-4f || v[k] > kParams[k].hi + 1e-4f) ++outOfRange;
            for (const auto& o : A.ov)
                if (o.p == k && (v[k] < o.lo - 1e-3f || v[k] > o.hi + 1e-3f)) ++outsideRecipe;
        }
        if (! (A.src & (1u << (int) v[src_type]))) ++badSrc;
        if (! (A.rev & (1u << (int) v[rvb_type]))) ++badRev;
        if (i < 4000) newPatches.push_back (std::vector<float> (v, v + kNumParams));
        randomizeValues (1u + i % 999000u, 0.f, w);
        if (i < 4000) oldPatches.push_back (std::vector<float> (w, w + kNumParams));
    }
    std::printf ("%d recipes; hits per recipe:", archetype::kCount);
    for (int a = 0; a < archetype::kCount; ++a) std::printf (" %s=%d", archetype::kList[a].name, hits[a]);
    std::printf ("\n");
    CHECK ((int) hits.size() == archetype::kCount, "only %d of %d recipes reached", (int) hits.size(), archetype::kCount);
    CHECK (outOfRange == 0, "%d values outside the parameter range", outOfRange);
    CHECK (outsideRecipe == 0, "%d values outside their recipe range (at soft)", outsideRecipe);
    CHECK (badSrc == 0 && badRev == 0, "source/reverb outside the recipe (%d, %d)", badSrc, badRev);

    // 3. all six sources and all twelve reverbs get used
    int srcUsed = 0, revUsed = 0;
    std::printf ("sources:"); for (int s = 0; s < kNumSources; ++s) { std::printf (" %s=%d", kSourceNames[s], srcHits[s]); srcUsed += srcHits[s] > 0; } std::printf ("\n");
    std::printf ("reverbs:"); for (int r = 0; r < kNumReverbTypes; ++r) { std::printf (" %s=%d", kReverbNames[r], revHits[r]); revUsed += revHits[r] > 0; } std::printf ("\n");
    CHECK (srcUsed == kNumSources, "a sound source is never picked");
    CHECK (revUsed == kNumReverbTypes, "a reverb is never picked at soft (%d of %d used)", revUsed, kNumReverbTypes);

    // 4. far more varied than the old generator (which kept most knobs near the middle)
    const double sNew = spread (newPatches), sOld = spread (oldPatches);
    std::printf ("average knob distance between two patches: old generator %.3f, archetypes %.3f (x%.2f)\n", sOld, sNew, sNew / sOld);
    CHECK (sNew > sOld * 1.15, "archetypes are not more varied (%.3f vs %.3f)", sNew, sOld);

    // 5. recipes are distinct from each other: the average of a knob differs between recipes
    {
        std::vector<double> sum ((size_t) archetype::kCount, 0.0); std::vector<int> cnt ((size_t) archetype::kCount, 0);
        for (uint32_t i = 0; i < 6000; ++i)
        {
            const uint32_t sd = kFirstArchetypeSeed + i * 17u;
            randomizeValues (sd, 0.f, v);
            const int a = archetype::indexForSeed (sd);
            sum[(size_t) a] += v[vol_atk]; ++cnt[(size_t) a];
        }
        double lo = 9, hi = -9;
        for (int a = 0; a < archetype::kCount; ++a) { const double m = sum[(size_t) a] / std::max (1, cnt[(size_t) a]); lo = std::min (lo, m); hi = std::max (hi, m); }
        std::printf ("mean vol_atk across recipes: %.2f .. %.2f\n", lo, hi);
        CHECK (hi - lo > 0.5, "recipes do not differ in attack (%.2f..%.2f)", lo, hi);
    }

    // 6. wild widens (and stays in range); auto-tune still applies
    {
        int outOfRangeWild = 0, offRecipeSrc = 0;
        for (uint32_t i = 0; i < 4000; ++i)
        {
            const uint32_t sd = kFirstArchetypeSeed + i * 13u;
            randomizeValues (sd, 1.f, v);
            for (int k = 0; k < kNumParams; ++k) if (v[k] < kParams[k].lo - 1e-4f || v[k] > kParams[k].hi + 1e-4f) ++outOfRangeWild;
            if (! (archetype::kList[archetype::indexForSeed (sd)].src & (1u << (int) v[src_type]))) ++offRecipeSrc;
        }
        CHECK (outOfRangeWild == 0, "wild left the parameter range");
        CHECK (offRecipeSrc > 300, "wild never leaves the recipe's source (%d of 4000)", offRecipeSrc);
        randomizeValues (kFirstArchetypeSeed + 5u, 0.2f, v);
        correctTune (v);
        CHECK (std::fmod (std::fabs (v[a_freq]), 12.f) < 1e-4f, "Correct Tune on an archetype patch");
    }

    std::printf (failures == 0 ? "ARCHETYPES OK\n" : "%d ARCHETYPE CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
