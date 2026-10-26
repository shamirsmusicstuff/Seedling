#pragma once
// DuskPlate: Seedling's reverb, recreated after the "Plate" engine of DuskVerb
// (https://github.com/dusk-audio/dusk-audio-plugins, plugins/DuskVerb, GPL-3.0).
//
// The structure and many of the constants follow DuskVerb's DattorroTank / DuskVerbEngine:
//
//   in -> pre-delay (+ slow swell) -> early reflections ------------------------------+
//                \-> 6 short input diffusers (43..313 samples @44.1k, coeff 0.58-0.65)  |
//                      \-> two cross-coupled "figure-8" tanks (Dattorro 1997)           |
//                            each: modulated allpass -> modulated delay                 |
//                                  -> 12-stage density allpass cascade                  |
//                                  -> 3-band damping (bass / mid / treble decay)        |
//                                  -> static allpass -> modulated delay                 |
//                            end of each tank feeds the start of the other (soft-clipped)
//                      \-> 7 signed output taps per channel from both tanks (DuskVerb's
//                          tap table) -> stereo wet                                    <-+
//
//   Decay:  every tank pass applies gBase = 10^(-3 * loopLength / (T60 * sr)), split over three bands
//           (gLow = gBase^(1/bass), gMid = gBase^(1/mid), gHi = gBase^(1/treble)), exactly as DuskVerb does.
//   Modulation: smoothed random-walk (not a sine), so the tail never settles into a periodic warble.
//
// Seedling's six reverb knobs drive it (mix, attack = pre-delay + swell, length = T60, damping = treble multiply,
// chorus = modulation depth, size = delay scale). Saved seeds and presets keep the same knob values; only the
// sound of the reverb is different.
//
// Plain C++ (no JUCE). Licensed GPL-3.0-or-later together with the rest of this branch (see LICENSE).

#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace seed
{
namespace dusk
{
    // ---- delay line (power-of-two ring, Hermite fractional read) -------------------------------------------------
    struct Line
    {
        std::vector<float> b;
        int mask = 0, w = 0;

        void init (int maxDelay)
        {
            int sz = 1;
            while (sz < maxDelay + 8) sz <<= 1;
            b.assign ((size_t) sz, 0.f);
            mask = sz - 1;
            w = 0;
        }
        void clear() { std::fill (b.begin(), b.end(), 0.f); w = 0; }
        void push (float x) { b[(size_t) w] = x; w = (w + 1) & mask; }
        float at (int d) const { return b[(size_t) ((w - d) & mask)]; }          // d = 1 is the newest sample
        float read (float d) const                                                // 4-point Hermite
        {
            d = std::max (d, 2.f);
            const int i = (int) d;
            const float f = d - (float) i;
            const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }
    };

    // Schroeder allpass with a (possibly fractional, possibly moving) delay.
    struct Allpass
    {
        Line line;
        float len = 1.f;
        void init (int maxDelay) { line.init (maxDelay); }
        float process (float x, float g, float lenNow)
        {
            const float delayed = line.read (lenNow);
            const float in = x + g * delayed;
            line.push (in);
            return delayed - g * in;
        }
    };

    // Smoothed random walk in -1..1 (smoothstep between random targets).
    struct Wander
    {
        uint32_t s = 1;
        float a = 0.f, b = 0.f, ph = 0.f, inc = 0.f;
        void seed (uint32_t v, float rateHz, float sr) { s = v * 2654435761u + 12345u; inc = rateHz / sr; a = rnd(); b = rnd(); ph = (float) (v % 97) / 97.f; }
        float rnd() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) * (2.f / 16777216.f) - 1.f; }
        float next()
        {
            ph += inc;
            if (ph >= 1.f) { ph -= 1.f; a = b; b = rnd(); }
            const float t = ph * ph * (3.f - 2.f * ph);
            return a + (b - a) * t;
        }
    };

    inline float softClip (float x)       // linear up to +-1, then eases towards +-2
    {
        const float a = std::fabs (x);
        if (a <= 1.f) return x;
        const float y = 1.f + std::tanh (a - 1.f);
        return x < 0.f ? -y : y;
    }

    //==========================================================================
    struct Plate
    {
        // ---- DuskVerb DattorroTank constants (samples at 44.1 kHz), hall-scale loop ------------------------------
        static constexpr int kLAP1 = 709,  kLD1 = 4507, kLAP2 = 1871, kLD2 = 3769;
        static constexpr int kRAP1 = 953,  kRD1 = 4219, kRAP2 = 2749, kRD2 = 3299;
        static constexpr int kNumDensity = 12;
        static constexpr int kDensL[kNumDensity] = { 137, 199, 281, 53, 79, 113, 43, 67, 97, 131, 163, 191 };
        static constexpr int kDensR[kNumDensity] = { 149, 211, 263, 61, 89, 127, 47, 71, 101, 139, 167, 193 };
        static constexpr int kNumDiff = 6;
        static constexpr int   kDiffBase[kNumDiff]  = { 43, 71, 103, 167, 239, 313 };
        static constexpr float kDiffCoeff[kNumDiff] = { 0.65f, 0.65f, 0.62f, 0.60f, 0.60f, 0.58f };
        static constexpr int kNumTaps = 7;
        struct Tap { int buf; float pos, sign; };       // buf: 0 L-d1, 1 L-d2, 2 L-ap2, 3 R-d1, 4 R-d2, 5 R-ap2
        static constexpr Tap kTapsL[kNumTaps] = { { 3, 0.120f, 1 }, { 3, 0.675f, 1 }, { 5, 0.480f, -1 }, { 0, 0.450f, 1 }, { 1, 0.540f, -1 }, { 2, 0.210f, -1 }, { 4, 0.310f, -1 } };
        static constexpr Tap kTapsR[kNumTaps] = { { 0, 0.140f, 1 }, { 0, 0.710f, 1 }, { 2, 0.520f, -1 }, { 3, 0.410f, 1 }, { 4, 0.580f, -1 }, { 5, 0.240f, -1 }, { 1, 0.350f, -1 } };
        static constexpr float kMaxScale = 1.3f;        // size 1 -> 0.30 + 0.90 = 1.2 (+ headroom)
        static constexpr float kOutScale = 1.f / 7.f;
        static constexpr float kDecayDiff1 = 0.70f, kDecayDiff2 = 0.50f, kDensityCoeff = 0.55f;
        static constexpr float kLowXover = 450.f, kHighXover = 3800.f;

        struct Tank
        {
            Allpass ap1, ap2, dens[kNumDensity];
            Line d1, d2;
            float ap1Len = 1.f, ap2Len = 1.f, d1Len = 1.f, d2Len = 1.f;               // smoothed
            float ap1Tgt = 1.f, ap2Tgt = 1.f, d1Tgt = 1.f, d2Tgt = 1.f;
            int densLen[kNumDensity] = {};
            float lpLo = 0.f, lpHi = 0.f;                                            // damping crossover states
            float gLow = 0.9f, gMid = 0.9f, gHi = 0.9f;
            float cross = 0.f;
            Wander mAp1, mD1, mD2;
        };

        Tank L, R;
        Allpass diff[kNumDiff];
        Line pre, er;
        float sr = 48000.f, k = 1.0884f;           // k = sr / 44100
        float modDepth = 0.f, preSamples = 0.f, riseCo = 1.f, size = 0.5f, erGain = 0.4f;
        float loCoef = 0.f, hiCoef = 0.f;
        float env = 0.f, gate = 1.f, mixCos = 1.f, mixSin = 0.f;
        bool flip = false, fresh = true;

        // early-reflection taps (ms), own set in DuskVerb's spirit: sparse, increasing, left/right interleaved
        static constexpr int kNumEr = 6;
        static constexpr float kErL[kNumEr] = { 7.3f, 13.1f, 19.7f, 28.3f, 37.9f, 47.3f };
        static constexpr float kErR[kNumEr] = { 9.1f, 15.7f, 23.9f, 31.1f, 41.3f, 53.7f };
        static constexpr float kErG[kNumEr] = { 0.80f, 0.65f, 0.55f, 0.45f, 0.36f, 0.28f };

        void prepare (float sampleRate)
        {
            sr = sampleRate;
            k = sr / 44100.f;
            auto cap = [this] (int base) { return (int) (base * k * kMaxScale) + 64; };
            L.ap1.init (cap (kLAP1)); L.ap2.init (cap (kLAP2)); L.d1.init (cap (kLD1)); L.d2.init (cap (kLD2));
            R.ap1.init (cap (kRAP1)); R.ap2.init (cap (kRAP2)); R.d1.init (cap (kRD1)); R.d2.init (cap (kRD2));
            for (int i = 0; i < kNumDensity; ++i)
            {
                L.densLen[i] = std::max (2, (int) std::lround (kDensL[i] * k));
                R.densLen[i] = std::max (2, (int) std::lround (kDensR[i] * k));
                L.dens[i].init (L.densLen[i] + 8);
                R.dens[i].init (R.densLen[i] + 8);
            }
            for (int i = 0; i < kNumDiff; ++i) diff[i].init ((int) (kDiffBase[i] * k) + 8);
            pre.init ((int) (0.08f * sr) + 16);
            er.init ((int) (0.09f * sr) + 16);
            L.mAp1.seed (11, 0.53f, sr); L.mD1.seed (23, 0.71f, sr); L.mD2.seed (37, 0.43f, sr);
            R.mAp1.seed (53, 0.61f, sr); R.mD1.seed (71, 0.37f, sr); R.mD2.seed (89, 0.83f, sr);
            loCoef = std::exp (-kTwoPi * kLowXover / sr);
            hiCoef = std::exp (-kTwoPi * kHighXover / sr);
            reset();
            fresh = true;
        }

        void reset()
        {
            for (auto* t : { &L, &R })
            {
                t->ap1.line.clear(); t->ap2.line.clear(); t->d1.clear(); t->d2.clear();
                for (auto& a : t->dens) a.line.clear();
                t->lpLo = t->lpHi = t->cross = 0.f;
            }
            for (auto& a : diff) a.line.clear();
            pre.clear(); er.clear();
            env = 0.f; gate = 1.f;
        }

        void snapLengths()
        {
            for (auto* t : { &L, &R }) { t->ap1Len = t->ap1Tgt; t->ap2Len = t->ap2Tgt; t->d1Len = t->d1Tgt; t->d2Len = t->d2Tgt; }
        }

        void setParams (const Derived& d)
        {
            size = 0.30f + 0.90f * d.rvbSize;                           // DuskVerb's delay-scale control
            auto place = [this] (Tank& t, int ap1, int d1, int ap2, int d2)
            {
                t.ap1Tgt = std::max (4.f, ap1 * k * size);
                t.d1Tgt  = std::max (4.f, d1  * k * size);
                t.ap2Tgt = std::max (4.f, ap2 * k * size);
                t.d2Tgt  = std::max (4.f, d2  * k * size);
            };
            place (L, kLAP1, kLD1, kLAP2, kLD2);
            place (R, kRAP1, kRD1, kRAP2, kRD2);

            if (fresh) { snapLengths(); fresh = false; }               // first call after prepare(): no glide from nothing

            // damping knob -> treble multiply (0.15 .. 1.0); bass a little longer, as in a real plate or hall
            const float bassMul = 1.10f, midMul = 1.f, trebMul = 1.f - 0.85f * d.rvbDamp;
            const float t60 = std::max (0.1f, d.rvbRt60);
            for (auto* t : { &L, &R })
            {
                float loop = t->ap1Tgt + t->d1Tgt + t->ap2Tgt + t->d2Tgt;
                for (int i = 0; i < kNumDensity; ++i) loop += (float) t->densLen[i];
                const float g = std::pow (10.f, -3.f * loop / (t60 * sr));
                t->gLow = clampf (std::pow (g, 1.f / bassMul), 0.001f, 0.9999f);
                t->gMid = clampf (std::pow (g, 1.f / midMul),  0.001f, 0.9999f);
                t->gHi  = clampf (std::pow (g, 1.f / trebMul), 0.001f, 0.9999f);
            }

            modDepth = d.rvbChor;                                       // 0 .. 1
            preSamples = d.rvbAtk * 0.06f * sr;
            const float tau = d.rvbAtk * 0.5f * sr;
            riseCo = tau < 1.f ? 1.f : 1.f - std::exp (-1.f / tau);
            mixCos = std::cos (d.rvbMix * kPi * 0.5f);
            mixSin = std::sin (d.rvbMix * kPi * 0.5f);
        }

        float tapRead (const Tank& l, const Tank& r, int buf, float pos) const
        {
            switch (buf)
            {
                case 0: return l.d1.read (std::max (2.f, pos * l.d1Len));
                case 1: return l.d2.read (std::max (2.f, pos * l.d2Len));
                case 2: return l.ap2.line.read (std::max (2.f, pos * l.ap2Len));
                case 3: return r.d1.read (std::max (2.f, pos * r.d1Len));
                case 4: return r.d2.read (std::max (2.f, pos * r.d2Len));
                default: return r.ap2.line.read (std::max (2.f, pos * r.ap2Len));
            }
        }

        void runTank (Tank& t, const Allpass* /*unused*/, float tankIn, bool isLeft)
        {
            const float modK = modDepth * k;
            t.ap1Len += (t.ap1Tgt - t.ap1Len) * 0.0005f;
            t.d1Len  += (t.d1Tgt  - t.d1Len)  * 0.0005f;
            t.ap2Len += (t.ap2Tgt - t.ap2Len) * 0.0005f;
            t.d2Len  += (t.d2Tgt  - t.d2Len)  * 0.0005f;

            // modulated allpass (decay diffusion 1)
            const float ap1Len = t.ap1Len + t.mAp1.next() * 14.f * modK;
            float x = t.ap1.process (tankIn, kDecayDiff1, ap1Len);

            // delay 1 (jittered read)
            const float del1 = t.d1.read (t.d1Len + t.mD1.next() * 8.f * modK);
            t.d1.push (x);
            x = del1;

            // density cascade
            for (int i = 0; i < kNumDensity; ++i)
                x = t.dens[i].process (x, kDensityCoeff, (float) t.densLen[i]);

            // 3-band damping: split into low / mid / high, apply the band gains
            t.lpLo = (1.f - loCoef) * x + loCoef * t.lpLo;
            t.lpHi = (1.f - hiCoef) * x + hiCoef * t.lpHi;
            x = t.gLow * t.lpLo + t.gMid * (t.lpHi - t.lpLo) + t.gHi * (x - t.lpHi);

            // static allpass (decay diffusion 2), delay 2
            x = t.ap2.process (x, kDecayDiff2, t.ap2Len);
            const float del2 = t.d2.read (t.d2Len + t.mD2.next() * 6.f * modK);
            const float bias = flip ? 1e-18f : -1e-18f;
            t.d2.push (x + bias);
            t.cross = std::max (-4.f, std::min (4.f, softClip (del2)));
            (void) isLeft;
        }

        void process (float in, float& outL, float& outR)
        {
            // pre-delay
            pre.push (in);
            float x = pre.at (std::max (1, (int) preSamples + 1));

            // swell: the wet input fades in after silence (rvb_atk)
            env = std::max (std::fabs (in), env * 0.9995f);
            const float target = env > 0.002f ? 1.f : 0.f;
            gate += (target - gate) * (target > gate ? riseCo : 0.002f);
            x *= gate;

            // early reflections
            er.push (x);
            float eL = 0.f, eR = 0.f;
            for (int i = 0; i < kNumEr; ++i)
            {
                eL += kErG[i] * er.at (std::max (1, (int) (kErL[i] * 0.001f * sr * (0.5f + size * 0.7f))));
                eR += kErG[i] * er.at (std::max (1, (int) (kErR[i] * 0.001f * sr * (0.5f + size * 0.7f))));
            }

            // input diffusion
            float s = x;
            for (int i = 0; i < kNumDiff; ++i) s = diff[i].process (s, kDiffCoeff[i], (float) std::max (2, (int) (kDiffBase[i] * k)));

            // the two cross-coupled tanks (figure-8): each is fed the input plus the other's last output
            const float crossL = L.cross, crossR = R.cross;
            runTank (L, nullptr, s * 0.5f + crossR, true);
            runTank (R, nullptr, s * 0.5f + crossL, false);
            flip = ! flip;

            float wl = 0.f, wr = 0.f;
            for (int i = 0; i < kNumTaps; ++i)
            {
                wl += kTapsL[i].sign * tapRead (L, R, kTapsL[i].buf, kTapsL[i].pos);
                wr += kTapsR[i].sign * tapRead (L, R, kTapsR[i].buf, kTapsR[i].pos);
            }
            outL = wl * kOutScale * kOutGain + eL * erGain * 0.25f;
            outR = wr * kOutScale * kOutGain + eR * erGain * 0.25f;
            if (! std::isfinite (outL) || ! std::isfinite (outR)) { reset(); outL = outR = 0.f; }
        }

        static constexpr float kOutGain = 1.7f;        // wet level, matched to the previous reverb (see Tests/reverb_test.cpp)
    };
}
}
