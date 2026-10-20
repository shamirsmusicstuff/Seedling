// Headless checks for the Seedling engine: tuning accuracy, stability of random patches, levels, CPU.
//   g++ -O2 -std=c++17 -I Source Tests/engine_test.cpp Source/Engine.cpp -o engine_test && ./engine_test
#include "Engine.h"
#include "Randomizer.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace seed;

static const float kSr = 48000.f;

struct Render { std::vector<float> L, R; };

static Render renderNotes (const float* params, const std::vector<int>& notes, float vel, float holdSec, float totalSec)
{
    Engine e;
    e.prepare (kSr);
    e.setParams (params);
    const int total = (int) (totalSec * kSr), hold = (int) (holdSec * kSr);
    Render r; r.L.assign ((size_t) total, 0.f); r.R.assign ((size_t) total, 0.f);
    int pos = 0; bool on = false, off = false;
    while (pos < total)
    {
        int n = std::min (256, total - pos);
        if (! on) { for (int nt : notes) e.noteOn (nt, vel); on = true; }
        if (! off && pos >= hold) { for (int nt : notes) e.noteOff (nt); off = true; }
        e.render (r.L.data() + pos, r.R.data() + pos, n);
        pos += n;
    }
    return r;
}

// frequency from positive-going zero crossings in [from, to)
static float estimateHz (const std::vector<float>& x, int from, int to)
{
    int first = -1, last = -1, count = 0;
    for (int i = from + 1; i < to; ++i)
        if (x[(size_t) i - 1] <= 0.f && x[(size_t) i] > 0.f)
        {
            const float frac = -x[(size_t) i - 1] / (x[(size_t) i] - x[(size_t) i - 1]);
            const float t = (float) (i - 1) + frac;
            if (first < 0) first = (int) (t * 1000.f);
            last = (int) (t * 1000.f);
            ++count;
        }
    if (count < 3) return 0.f;
    return (float) (count - 1) * kSr / ((float) (last - first) / 1000.f);
}

static void defaults (float* p) { for (int i = 0; i < kNumParams; ++i) p[i] = kParams[i].def; }

// a clean, sine-only, unfiltered patch for pitch measurements
static void pitchPatch (float* p)
{
    defaults (p);
    p[a_form] = 0; p[a_noise] = 0; p[a_mod] = 0; p[a_color] = 0; p[a_freq] = 0;
    p[osc_mix] = 0; p[mix_mod] = 0; p[fm_amt] = 0; p[fm_mod] = 0;
    p[flt_freq] = 1; p[flt_mod] = 0; p[flt_kf] = 0; p[flt_type] = 0; p[flt_q] = 0; p[flt_sep] = 0;
    p[vol_atk] = 0; p[vol_sus] = 1; p[vol_fade] = 0; p[lfo_amt] = 0;
    p[rvb_mix] = 0; p[saturate] = 0; p[adj_bass] = 0; p[adj_treb] = 0; p[adj_pan] = 0; p[adj_clip] = 0;
    p[env_time] = 0.5f; p[env_kf] = 0;
}

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

int main()
{
    std::printf ("== 1. Tuning ==\n");
    {
        float p[kNumParams];
        const int notes[] = { 36, 48, 60, 69, 81 };
        for (int nt : notes)
        {
            pitchPatch (p);
            auto r = renderNotes (p, { nt }, 1.f, 1.f, 0.8f);
            const float hz = estimateHz (r.L, 12000, 36000);
            const float expect = 440.f * std::pow (2.f, (float) (nt - 69) / 12.f);
            std::printf ("  note %3d: %.3f Hz (expected %.3f)\n", nt, hz, expect);
            CHECK (std::fabs (hz / expect - 1.f) < 0.002f, "pitch off for note %d", nt);
        }

        // Randomised (detuned) A, then Correct Tune: the result must be an exact octave of the key.
        int offBefore = 0, offAfter = 0, tested = 0;
        for (uint32_t s = 1; s <= 40; ++s)
        {
            float v[kNumParams];
            randomizeValues (s, 0.5f, v);
            pitchPatch (p);
            p[a_freq] = v[a_freq];
            const float semis = p[a_freq];
            const float nearest = snapToOctave (semis);
            if (std::fabs (semis - nearest) > 0.05f) ++offBefore;

            float t[kNumParams]; std::memcpy (t, p, sizeof (t));
            correctTune (t);
            auto r = renderNotes (t, { 60 }, 1.f, 1.f, 0.8f);
            const float hz = estimateHz (r.L, 12000, 36000);
            const float expect = 261.6256f * std::pow (2.f, t[a_freq] / 12.f);
            ++tested;
            if (std::fabs (hz / expect - 1.f) > 0.003f) ++offAfter;
            CHECK (std::fabs (std::fmod (t[a_freq], 12.f)) < 1e-4f, "a_freq %.3f not an octave", t[a_freq]);
            const float bs = t[b_freq];
            const float allowed[] = { -24, -19, -12, -7, -5, 0, 5, 7, 12, 19, 24 };
            bool ok = false; for (float a : allowed) ok |= std::fabs (a - bs) < 1e-4f;
            CHECK (ok, "b_freq %.3f not a perfect interval", bs);
        }
        std::printf ("  %d/%d random patches had a detuned A before Correct Tune; %d/%d off-pitch after\n", offBefore, tested, offAfter, tested);
        CHECK (offAfter == 0, "pitch error remains after Correct Tune");
    }

    std::printf ("== 2. Random patches (chord, 3.5 s) ==\n");
    for (float chaos : { 0.f, 0.5f, 1.f })
    {
        int silent = 0, bad = 0, loud = 0, n = 0;
        double sumPeak = 0, sumRmsDb = 0;
        float worstPeak = 0;
        for (uint32_t s = 1; s <= 300; ++s)
        {
            float v[kNumParams];
            randomizeValues (s, chaos, v);
            correctTune (v);
            auto r = renderNotes (v, { 57, 64, 69 }, 0.8f, 2.0f, 3.5f);
            double sq = 0; float peak = 0; bool nan = false;
            const int a = 0, b = (int) (2.0f * kSr);
            for (int i = 0; i < (int) r.L.size(); ++i)
            {
                const float l = r.L[(size_t) i], rr = r.R[(size_t) i];
                if (! std::isfinite (l) || ! std::isfinite (rr)) nan = true;
                peak = std::max ({ peak, std::fabs (l), std::fabs (rr) });
                if (i >= a && i < b) sq += (double) l * l + (double) rr * rr;
            }
            const double rms = std::sqrt (sq / (2.0 * (b - a)));
            const double db = 20.0 * std::log10 (rms + 1e-9);
            ++n; sumPeak += peak; sumRmsDb += db; worstPeak = std::max (worstPeak, peak);
            if (nan) ++bad;
            if (db < -60.0) ++silent;
            if (peak > 0.99f) ++loud;
        }
        std::printf ("  chaos %.1f: %d patches | NaN/inf %d | silent(<-60dB) %d | hit ceiling %d | mean peak %.2f | mean RMS %.1f dB | worst peak %.2f\n",
                     chaos, n, bad, silent, loud, sumPeak / n, sumRmsDb / n, worstPeak);
        CHECK (bad == 0, "non-finite samples at chaos %.1f", chaos);
        CHECK (worstPeak <= 1.0001f, "output exceeded full scale (%.3f)", worstPeak);
        if (chaos == 0.f) CHECK (silent * 20 <= n, "too many silent soft patches (%d of %d)", silent, n);
    }

    std::printf ("== 3. Defaults, reverb tail, CPU ==\n");
    {
        float p[kNumParams]; defaults (p);
        p[rvb_mix] = 0.5f; p[rvb_len] = 0.5f;
        auto r = renderNotes (p, { 60 }, 0.8f, 0.3f, 6.f);
        auto rmsAt = [&] (float t0, float t1)
        {
            double s = 0; int a = (int) (t0 * kSr), b = (int) (t1 * kSr);
            for (int i = a; i < b; ++i) s += (double) r.L[(size_t) i] * r.L[(size_t) i];
            return 20.0 * std::log10 (std::sqrt (s / (b - a)) + 1e-9);
        };
        std::printf ("  default patch, 0.3 s note: RMS 0.1-0.3 s %.1f dB | 1-1.5 s %.1f dB | 3-3.5 s %.1f dB | 5-5.5 s %.1f dB\n",
                     rmsAt (0.1f, 0.3f), rmsAt (1.f, 1.5f), rmsAt (3.f, 3.5f), rmsAt (5.f, 5.5f));
        CHECK (rmsAt (0.1f, 0.3f) > -40.f, "default patch is too quiet");
        CHECK (rmsAt (5.f, 5.5f) < rmsAt (1.f, 1.5f), "reverb tail does not decay");

        float v[kNumParams]; randomizeValues (7, 1.f, v);
        auto t0 = std::chrono::steady_clock::now();
        auto rr = renderNotes (v, { 48, 52, 55, 59, 62, 66, 69, 72 }, 0.9f, 4.f, 5.f);
        auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double> (t1 - t0).count();
        std::printf ("  8 voices, 5 s of audio rendered in %.3f s  (%.1f%% of one core, unoptimised build flags aside)\n", secs, 100.0 * secs / 5.0);
        (void) rr;
    }

    std::printf (failures == 0 ? "\nALL CHECKS PASSED\n" : "\n%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
