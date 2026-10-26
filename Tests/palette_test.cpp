// Seed -> palette: deterministic, readable, varied. Needs only a C++ compiler (Palette.h has no JUCE in it).
#include "Palette.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <tuple>

using namespace seed;
using namespace seed::palette;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

int main()
{
    const Palette orig = original();
    CHECK (fromSeed (0) == orig, "seed 0 must give the original palette");
    const Rgb oi { 0x0C, 0x12, 0x0C }, oa { 0xC2, 0x01, 0x14 }, op { 0xEC, 0xEB, 0xF3 };
    CHECK (orig.ink == oi && orig.accent == oa && orig.paper == op, "original palette changed");

    // printed for the log (not asserted: libm rounding may differ by one step between platforms)
    const Palette g1 = fromSeed (1), g2 = fromSeed (4242);
    std::printf ("seed 1    ink %02X%02X%02X accent %02X%02X%02X slate %02X%02X%02X mist %02X%02X%02X paper %02X%02X%02X\n",
                 g1.ink.r, g1.ink.g, g1.ink.b, g1.accent.r, g1.accent.g, g1.accent.b, g1.slate.r, g1.slate.g, g1.slate.b,
                 g1.mist.r, g1.mist.g, g1.mist.b, g1.paper.r, g1.paper.g, g1.paper.b);
    std::printf ("seed 4242 ink %02X%02X%02X accent %02X%02X%02X slate %02X%02X%02X mist %02X%02X%02X paper %02X%02X%02X\n",
                 g2.ink.r, g2.ink.g, g2.ink.b, g2.accent.r, g2.accent.g, g2.accent.b, g2.slate.r, g2.slate.g, g2.slate.b,
                 g2.mist.r, g2.mist.g, g2.mist.b, g2.paper.r, g2.paper.g, g2.paper.b);

    const uint32_t N = 60000;
    float minText = 99.f, minAccentText = 99.f, minSlateText = 99.f, minAccentInk = 99.f, minMist = 99.f;
    std::set<std::tuple<int, int, int>> accents;
    int same = 0;
    for (uint32_t sd = 1; sd <= N; ++sd)
    {
        const Palette p = fromSeed (sd);
        if (! (p == fromSeed (sd))) ++same;
        minText       = std::min (minText,       contrast (p.paper, p.ink));
        minAccentText = std::min (minAccentText, contrast (p.paper, p.accent));
        minSlateText  = std::min (minSlateText,  contrast (p.paper, p.slate));
        minAccentInk  = std::min (minAccentInk,  contrast (p.accent, p.ink));
        minMist       = std::min (minMist,       contrast (p.mist, p.ink));
        accents.insert (std::make_tuple (p.accent.r / 8, p.accent.g / 8, p.accent.b / 8));
    }
    std::printf ("%u seeds: min contrast  text/ink %.2f  text/accent %.2f  text/slate %.2f  accent/ink %.2f  mist/ink %.2f;  %zu distinct accents\n",
                 N, minText, minAccentText, minSlateText, minAccentInk, minMist, accents.size());

    CHECK (same == 0, "palette not deterministic");
    CHECK (minText >= 10.f,       "text on ink only %.2f", minText);
    CHECK (minAccentText >= 4.5f, "text on accent only %.2f", minAccentText);
    CHECK (minSlateText >= 5.5f,  "text on slate only %.2f", minSlateText);
    CHECK (minAccentInk >= 2.f,   "accent vs ink only %.2f", minAccentInk);
    CHECK (minMist >= 4.5f,       "mist on ink only %.2f", minMist);
    CHECK (accents.size() > 400,  "palettes look too alike (%zu distinct accents)", accents.size());

    CHECK (mix (orig, g1, 0.f) == orig && mix (orig, g1, 1.f) == g1, "mix endpoints wrong");

    std::printf (failures == 0 ? "PALETTE OK\n" : "%d PALETTE CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
