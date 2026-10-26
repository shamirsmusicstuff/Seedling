// DuskPlate (the reverb): stable, the right decay time, damping darkens the tail, deterministic, size and modulation behave.
// Plain C++, no JUCE.
#include "DuskPlate.h"
#include <cstdio>
#include <random>
#include <vector>

using namespace seed;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static Derived make (float rt, float damp, float chor, float size, float atk = 0.f)
{
    Derived d {};
    d.rvbMix = 1.f; d.rvbAtk = atk; d.rvbRt60 = rt; d.rvbDamp = damp; d.rvbChor = chor; d.rvbSize = size;
    return d;
}

// impulse response (left+right), length seconds
static std::vector<float> impulse (const Derived& d, float sr, float seconds)
{
    dusk::Plate p; p.prepare (sr); p.setParams (d);
    std::vector<float> out ((size_t) (seconds * sr));
    for (size_t i = 0; i < out.size(); ++i)
    {
        float l, r; p.process (i == 0 ? 1.f : 0.f, l, r);
        out[i] = l + r;
    }
    return out;
}

// RT60 from the Schroeder backward integral: slope between -5 and -25 dB, extrapolated to -60.
static float rt60 (const std::vector<float>& h, float sr)
{
    std::vector<double> e (h.size());
    double acc = 0;
    for (size_t i = h.size(); i-- > 0;) { acc += (double) h[i] * h[i]; e[i] = acc; }
    int i5 = -1, i25 = -1;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double db = 10.0 * std::log10 (e[i] / e[0] + 1e-30);
        if (i5 < 0 && db <= -5.0) i5 = (int) i;
        if (i25 < 0 && db <= -25.0) { i25 = (int) i; break; }
    }
    if (i5 < 0 || i25 < 0) return -1.f;
    return (float) (i25 - i5) / sr * 3.f;
}

int main()
{
    const float sr = 48000.f;

    // 1. decay time follows the length knob (damping at its lowest so the treble loss does not skew it)
    for (float want : { 0.6f, 1.5f, 4.f })
    {
        const float got = rt60 (impulse (make (want, 0.f, 0.f, 0.5f), sr, want * 2.5f + 2.f), sr);
        std::printf ("T60 target %.1f s -> measured %.2f s\n", want, got);
        CHECK (got > want * 0.6f && got < want * 1.5f, "T60 %.2f s for a %.2f s target", got, want);
    }

    // 2. damping makes the tail darker and shorter in the highs: compare late energy of a high-passed tail
    {
        auto hp = [] (const std::vector<float>& h)
        {
            std::vector<float> o (h.size()); float z = 0.f;
            for (size_t i = 0; i < h.size(); ++i) { z += 0.4f * (h[i] - z); o[i] = h[i] - z; }
            return o;
        };
        auto late = [&] (const std::vector<float>& h) { double e = 0; for (size_t i = (size_t) (1.0f * sr); i < h.size(); ++i) e += (double) h[i] * h[i]; return e; };
        const double bright = late (hp (impulse (make (3.f, 0.05f, 0.f, 0.5f), sr, 4.f)));
        const double dark   = late (hp (impulse (make (3.f, 0.95f, 0.f, 0.5f), sr, 4.f)));
        std::printf ("late high-band energy: damping 0.05 %.3g, 0.95 %.3g (%.1f dB)\n", bright, dark, 10 * std::log10 (dark / bright));
        CHECK (dark < bright * 0.25, "damping did not darken the tail");
    }

    // 3. size changes the early build-up: a bigger size puts the energy later
    {
        auto centroid = [&] (const std::vector<float>& h)
        {
            double a = 0, b = 0;
            for (size_t i = 0; i < (size_t) (0.5f * sr) && i < h.size(); ++i) { a += (double) i * h[i] * h[i]; b += (double) h[i] * h[i]; }
            return a / std::max (1e-30, b) / sr;
        };
        const double small = centroid (impulse (make (1.f, 0.3f, 0.f, 0.05f), sr, 1.f));
        const double big   = centroid (impulse (make (1.f, 0.3f, 0.f, 0.95f), sr, 1.f));
        std::printf ("energy centroid (first 0.5 s): size 0.05 %.3f s, size 0.95 %.3f s\n", small, big);
        CHECK (big > small, "bigger size should centre the energy later");
    }

    // 4. deterministic (same input, same output, run to run)
    {
        const auto a = impulse (make (2.f, 0.4f, 0.6f, 0.5f), sr, 1.f), b = impulse (make (2.f, 0.4f, 0.6f, 0.5f), sr, 1.f);
        CHECK (a == b, "not deterministic");
    }

    // 5. modulation changes the tail but keeps its level (chorus 0 vs 1)
    {
        const auto a = impulse (make (2.f, 0.4f, 0.f, 0.5f), sr, 2.f), b = impulse (make (2.f, 0.4f, 1.f, 0.5f), sr, 2.f);
        double ea = 0, eb = 0, diff = 0;
        for (size_t i = 0; i < a.size(); ++i) { ea += (double) a[i] * a[i]; eb += (double) b[i] * b[i]; diff += (double) (a[i] - b[i]) * (a[i] - b[i]); }
        std::printf ("modulation: energy %.3g vs %.3g, difference %.3g\n", ea, eb, diff);
        CHECK (diff > 0.01 * ea, "chorus knob does nothing");
        CHECK (eb > 0.5 * ea && eb < 2.0 * ea, "modulation changed the level too much");
    }

    // 6. never blows up, whatever the knobs do, with loud noise and while the knobs move
    {
        std::mt19937 g (7);
        std::normal_distribution<float> n (0.f, 0.5f);
        std::uniform_real_distribution<float> u (0.f, 1.f);
        dusk::Plate p; p.prepare (44100.f);
        float peak = 0.f; bool finite = true;
        for (int block = 0; block < 2400; ++block)                   // ~ 35 s
        {
            Derived d = make (map::rvbRt60 (u (g)), u (g), u (g), u (g), u (g) * 0.3f);
            if (block % 3 == 0) p.setParams (d);
            for (int i = 0; i < 640; ++i)
            {
                float l, r; p.process (n (g), l, r);
                if (! std::isfinite (l) || ! std::isfinite (r)) finite = false;
                peak = std::max (peak, std::max (std::fabs (l), std::fabs (r)));
            }
        }
        std::printf ("stress: peak %.2f\n", peak);
        CHECK (finite, "NaN or inf");
        CHECK (peak < 40.f, "peak %.1f", peak);
    }

    std::printf (failures == 0 ? "REVERB OK\n" : "%d REVERB CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
