#pragma once
// Seedling's colours come from ONE place: the patch seed. There are no skins to pick and nothing to edit -
// every seed grows its own palette, and the same seed always grows the same palette (on every machine).
//
//   seed 0 (no seed yet)  -> the original Seedling palette  https://coolors.co/0c120c-c20114-6d7275-c7d6d5-ecebf3
//   any other seed        -> a fresh, cohesive palette built in OKLCH from a hash of the seed
//
// Roles:  ink = window base (dark), accent = the big glowing colour, slate = second colour field / tracks,
//         mist = faint third field / soft highlights, paper = all text, lines and the tree (light).
//
// The palette is built from its own hash of the seed. It never touches the patch randomiser, so a saved seed
// still grows exactly the same sound as before.
//
// Readability is guaranteed, not hoped for: after the colours are picked they are nudged until
//   paper on ink >= 10, paper on accent >= 4.5, paper on slate >= 5.5, accent against ink >= 2, mist on ink >= 4.5.
// (Tests/palette_test.cpp checks that for tens of thousands of seeds, on every platform, in CI.)
//
// No JUCE in here on purpose, so the test needs nothing but a C++ compiler.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace seed
{

struct Rgb
{
    uint8_t r = 0, g = 0, b = 0;
    bool operator== (const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!= (const Rgb& o) const { return ! (*this == o); }
};

struct Palette
{
    Rgb ink, accent, slate, mist, paper;
    bool operator== (const Palette& o) const { return ink == o.ink && accent == o.accent && slate == o.slate && mist == o.mist && paper == o.paper; }
};

namespace palette
{
    inline Palette original()
    {
        return { { 0x0C, 0x12, 0x0C }, { 0xC2, 0x01, 0x14 }, { 0x6D, 0x72, 0x75 }, { 0xC7, 0xD6, 0xD5 }, { 0xEC, 0xEB, 0xF3 } };
    }

    // ---- colour maths -------------------------------------------------------------------------------------------
    inline float toLinear (float c)   { return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f); }
    inline float fromLinear (float c) { return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow (c, 1.f / 2.4f) - 0.055f; }

    inline float luminance (Rgb c)
    {
        return 0.2126f * toLinear ((float) c.r / 255.f) + 0.7152f * toLinear ((float) c.g / 255.f) + 0.0722f * toLinear ((float) c.b / 255.f);
    }

    inline float contrast (Rgb a, Rgb b)
    {
        float la = luminance (a), lb = luminance (b);
        if (la < lb) std::swap (la, lb);
        return (la + 0.05f) / (lb + 0.05f);
    }

    struct Lin { float r, g, b; };

    inline Lin oklchToLinear (float L, float C, float hueDeg)
    {
        const float h = hueDeg * 3.14159265358979f / 180.f;
        const float a = C * std::cos (h), b = C * std::sin (h);
        const float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
        const float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
        const float s_ = L - 0.0894841775f * a - 1.2914855480f * b;
        const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
        return {  4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
                 -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
                 -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s };
    }

    inline bool inGamut (const Lin& c)
    {
        const float lo = -0.0005f, hi = 1.0005f;
        return c.r >= lo && c.r <= hi && c.g >= lo && c.g <= hi && c.b >= lo && c.b <= hi;
    }

    inline uint8_t to8 (float lin)
    {
        const float v = fromLinear (std::min (1.f, std::max (0.f, lin)));
        return (uint8_t) std::lround (std::min (1.f, std::max (0.f, v)) * 255.f);
    }

    // OKLCH -> sRGB. A colour the screen cannot show loses chroma (not lightness or hue) until it fits.
    inline Rgb oklch (float L, float C, float hueDeg)
    {
        Lin c = oklchToLinear (L, C, hueDeg);
        if (! inGamut (c))
        {
            float lo = 0.f, hi = C;
            for (int i = 0; i < 16; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                if (inGamut (oklchToLinear (L, mid, hueDeg))) lo = mid; else hi = mid;
            }
            c = oklchToLinear (L, lo, hueDeg);
        }
        return { to8 (c.r), to8 (c.g), to8 (c.b) };
    }

    // ---- the seed -> numbers stream (integer hash; its own, separate from the patch randomiser) --------------------
    struct Stream
    {
        uint64_t s;
        explicit Stream (uint32_t seed) : s ((uint64_t) seed * 0xD1B54A32D192ED03ull ^ 0x5EEDC0101200ull) {}
        float next()
        {
            s += 0x9E3779B97F4A7C15ull;
            uint64_t z = s;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            return (float) (z >> 40) * (1.f / 16777216.f);          // 0 .. 1
        }
        float range (float lo, float hi) { return lo + (hi - lo) * next(); }
        int pick (int n) { return std::min (n - 1, (int) (next() * (float) n)); }
    };

    struct Spec
    {
        float L, C, h;
        Rgb rgb() const { return oklch (L, C, h); }
    };

    inline Palette fromSeed (uint32_t seed)
    {
        if (seed == 0) return original();

        Stream r (seed);
        const float h0 = r.range (0.f, 360.f);

        // hue offsets of (accent, slate, mist) from the base hue: analogous, complementary, split, triad, square, calm
        static const float kSchemes[6][3] = { { 35.f, -35.f, 70.f }, { 180.f, 20.f, 160.f }, { 150.f, 210.f, 30.f },
                                              { 120.f, 240.f, 60.f }, { 90.f, 180.f, 270.f }, { 15.f, 30.f, -20.f } };
        const auto& sc = kSchemes[r.pick (6)];
        const float flip = r.next() < 0.5f ? -1.f : 1.f;

        Spec ink    { r.range (0.15f, 0.23f), r.range (0.030f, 0.070f), h0 };
        Spec accent { r.range (0.50f, 0.60f), r.range (0.120f, 0.190f), h0 + flip * sc[0] };
        Spec slate  { r.range (0.40f, 0.50f), r.range (0.050f, 0.120f), h0 + flip * sc[1] };
        Spec mist   { r.range (0.80f, 0.88f), r.range (0.030f, 0.080f), h0 + flip * sc[2] };
        Spec paper  { r.range (0.95f, 0.97f), r.range (0.010f, 0.030f), h0 };

        const Rgb inkRgb = ink.rgb(), paperRgb = paper.rgb();

        // The big colour fields must stand out from the base but still carry white text.
        while (contrast (accent.rgb(), inkRgb) < 2.0f && accent.L < 0.75f) accent.L += 0.01f;
        while (contrast (paperRgb, accent.rgb()) < 4.5f && accent.L > 0.25f) accent.L -= 0.01f;
        while (contrast (paperRgb, slate.rgb()) < 5.5f && slate.L > 0.20f) slate.L -= 0.01f;
        while (contrast (mist.rgb(), inkRgb) < 4.5f && mist.L < 0.95f) mist.L += 0.01f;

        return { inkRgb, accent.rgb(), slate.rgb(), mist.rgb(), paperRgb };
    }

    // Linear blend, used by the editor to glide from one seed's colours to the next.
    inline Rgb mix (Rgb a, Rgb b, float t)
    {
        auto m = [t] (uint8_t x, uint8_t y) { return (uint8_t) std::lround ((float) x + ((float) y - (float) x) * t); };
        return { m (a.r, b.r), m (a.g, b.g), m (a.b, b.b) };
    }

    inline Palette mix (const Palette& a, const Palette& b, float t)
    {
        return { mix (a.ink, b.ink, t), mix (a.accent, b.accent, t), mix (a.slate, b.slate, t), mix (a.mist, b.mist, t), mix (a.paper, b.paper, t) };
    }
}

} // namespace seed
