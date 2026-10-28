// DuskPlate (the reverb): stable, the right decay time, damping darkens the tail, deterministic, size and modulation behave.
// Plain C++, no JUCE.
#include "DuskVerbs.h"
#include "Randomizer.h"
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

    // 7. all twelve reverbs: finite, deterministic, similar loudness, sane decay, safe to switch between while playing
    {
        std::mt19937 g (11);
        std::normal_distribution<float> n (0.f, 0.1f);
        auto render = [&] (int type, float rt, int seconds, std::vector<float>* ir)
        {
            Derived d = make (rt, 0.5f, 0.3f, 0.5f); d.rvbType = (float) type;
            dusk::Reverb r; r.prepare (sr); r.setParams (d);
            double e = 0; int cnt = 0; bool finite = true; float peak = 0.f;
            for (int i = 0; i < (int) (sr * (float) seconds); ++i)
            {
                float in = ir ? (i == 0 ? 1.f : 0.f) : n (g), l, rr;
                r.process (in, l, rr);
                if (! std::isfinite (l) || ! std::isfinite (rr)) finite = false;
                peak = std::max (peak, std::max (std::fabs (l), std::fabs (rr)));
                if (ir) ir->push_back (l + rr);
                else if (i > (int) (sr * 3.f)) { e += (double) l * l + (double) rr * rr; ++cnt; }
            }
            CHECK (finite, "%s: NaN or inf", kReverbNames[type]);
            return cnt ? std::sqrt (e / cnt / 2.0) : (double) peak;
        };

        double ref = 0;
        for (int t = 0; t < kNumReverbTypes; ++t)
        {
            const double rms = render (t, 2.f, 8, nullptr);
            if (t == 0) ref = rms;
            const double db = 20.0 * std::log10 (rms / ref);
            std::printf ("  %-14s wet level %+5.1f dB vs Plate\n", kReverbNames[t], db);
            CHECK (rms > 0.002, "%s is silent", kReverbNames[t]);
            CHECK (std::fabs (db) < 5.0, "%s level %.1f dB away from the Plate", kReverbNames[t], db);

            std::vector<float> a, b;
            render (t, 2.f, 1, &a); render (t, 2.f, 1, &b);
            CHECK (a == b, "%s is not deterministic", kReverbNames[t]);
        }

        // decay time: networks and plates follow the length knob (damping low); the gate ends at its gate length
        for (int t : { 0, 5, 6 })
        {
            std::vector<float> h; render (t, 2.5f, 9, &h);
            const float got = rt60 (h, sr);
            std::printf ("  %-14s T60 target 2.5 s -> %.2f s\n", kReverbNames[t], got);
            CHECK (got > 1.2f && got < 4.5f, "%s T60 %.2f s", kReverbNames[t], got);
        }
        {
            std::vector<float> h; render (11, 1.5f, 3, &h);
            double early = 0, late = 0;
            for (size_t i = 0; i < h.size(); ++i) (i < (size_t) (0.25f * sr) ? early : (i > (size_t) (2.f * sr) ? late : early)) += (double) h[i] * h[i];
            CHECK (late < early * 1e-6, "the gate did not close");
        }
        {
            std::vector<float> hs, hl; render (8, 0.5f, 3, &hs); render (8, 6.f, 3, &hl);
            auto tailE = [&] (const std::vector<float>& h) { double e = 0; for (size_t i = (size_t) (1.5f * sr); i < h.size(); ++i) e += (double) h[i] * h[i]; return e; };
            CHECK (tailE (hl) > tailE (hs) * 3.0, "spring: longer length should ring longer");
        }

        // switching type every ~0.4 s while noise plays: never NaN, never a huge click
        {
            dusk::Reverb r; r.prepare (sr);
            float peak = 0.f; bool finite = true;
            for (int blk = 0; blk < 200; ++blk)
            {
                Derived d = make (1.5f, 0.4f, 0.4f, 0.5f); d.rvbType = (float) ((blk / 4) % kNumReverbTypes);
                r.setParams (d);
                for (int i = 0; i < 4800; ++i)
                {
                    float l, rr; r.process (n (g), l, rr);
                    if (! std::isfinite (l) || ! std::isfinite (rr)) finite = false;
                    peak = std::max (peak, std::max (std::fabs (l), std::fabs (rr)));
                }
            }
            std::printf ("  type switching: peak %.2f\n", peak);
            CHECK (finite && peak < 4.f, "switching types broke the reverb (peak %.2f)", peak);
        }

        // random knobs on every type
        {
            std::uniform_real_distribution<float> u (0.f, 1.f);
            for (int t = 0; t < kNumReverbTypes; ++t)
            {
                dusk::Reverb r; r.prepare (44100.f);
                float peak = 0.f; bool finite = true;
                for (int blk = 0; blk < 600; ++blk)
                {
                    Derived d = make (map::rvbRt60 (u (g)), u (g), u (g), u (g), u (g) * 0.3f); d.rvbType = (float) t;
                    r.setParams (d);
                    for (int i = 0; i < 1024; ++i)
                    {
                        float l, rr; r.process (n (g) * 5.f, l, rr);
                        if (! std::isfinite (l) || ! std::isfinite (rr)) finite = false;
                        peak = std::max (peak, std::max (std::fabs (l), std::fabs (rr)));
                    }
                }
                CHECK (finite && peak < 40.f, "%s: unstable with moving knobs (peak %.1f)", kReverbNames[t], peak);
            }
        }
    }

    // 8. the reverb type is part of Randomize: whole numbers, and every seed range reaches several types; old seeds keep their old values
    {
        int seen[kNumReverbTypes] = {}, kinds = 0;
        for (uint32_t sd = 1; sd <= 600; ++sd)
        {
            float v[kNumParams];
            randomizeValues (sd, 0.2f, v);
            CHECK (v[rvb_type] == std::round (v[rvb_type]) && v[rvb_type] >= 0.f && v[rvb_type] <= 11.f, "rvb_type %.2f is not a type", v[rvb_type]);
            ++seen[(int) v[rvb_type]];
        }
        for (int c : seen) if (c > 0) ++kinds;
        std::printf ("reverb types reached by 600 soft seeds: %d of 12\n", kinds);
        CHECK (kinds >= 10, "Randomize only reaches %d reverb types", kinds);
        CHECK (seen[5] + seen[6] > seen[11] + seen[10], "soft randomization should favour halls over gated/reverse");
        CHECK (kParams[rvb_type].def == 0.f && rvb_type == src_type - 1 && src_type == kNumParams - 1, "rvb_type and src_type must stay the last parameters (appended)");
    }

    std::printf (failures == 0 ? "REVERB OK\n" : "%d REVERB CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
