#pragma once
// The twelve reverbs of Seedling, recreated after the engines of DuskVerb
// (https://github.com/dusk-audio/dusk-audio-plugins, plugins/DuskVerb, GPL-3.0):
//
//    0 Plate          Dattorro cross-coupled plate (DuskPlate.h)
//    1 Vintage Plate  the same tank, darker and steelier (fixed post filter, calmer modulation)
//    2 Smooth Plate   the same tank, denser and calmer (stronger diffusion, less modulation)
//    3 Chamber        four cross-coupled lines, no modulation, 48 output taps
//    4 Tiled Room     sparse early-reflection front + a short, dark 4-line tail
//    5 Hall           16-line Hadamard feedback network with 3-band decay
//    6 Dense Hall     8-line network, allpass diffusion in every line, modulation everywhere
//    7 Parallel Hall  six frequency bands, each with its own 4-line network and its own decay time
//    8 Spring         three dispersive spring lines (chirp) with a little drive
//    9 Shimmer        8-line network with an octave-up pitch shifter in the feedback path
//   10 Reverse        rising early reflections into a dark tail (the swell builds, then decays)
//   11 Gated          64-tap feed-forward tapped delay with a hard gate (decay = gate length)
//
// Each is a compact recreation of the design (topology, delay-length families, decay maths), not a bit-exact port.
// All of them are driven by Seedling's six reverb knobs (mix, attack, length, damping, chorus, size) plus the
// reverb type. Pre-delay, swell and the dry/wet mix are done once, here, in Reverb.
//
// Plain C++ (no JUCE). GPL-3.0-or-later (see LICENSE).

#include "DuskPlate.h"

namespace seed
{
namespace dusk
{
    inline int nextPrime (int n)
    {
        if (n < 3) return 3;
        if (n % 2 == 0) ++n;
        for (;; n += 2)
        {
            bool prime = true;
            for (int d = 3; d * d <= n; d += 2) if (n % d == 0) { prime = false; break; }
            if (prime) return n;
        }
    }

    struct Rng
    {
        uint32_t s;
        explicit Rng (uint32_t v = 1) : s (v * 2654435761u + 0x9E3779B9u) {}
        float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) * (1.f / 16777216.f); }   // 0..1
    };

    //==========================================================================
    // N-line feedback delay network: Hadamard mixing, per-line 3-band decay, optional in-line allpass, dark loop filter,
    // modulation, input diffusion and either end-of-line or many-tap output.
    template <int N>
    struct Fdn
    {
        struct Cfg
        {
            float lenLoMs = 20.f, lenHiMs = 70.f;      // line lengths at size 1, spread geometrically
            float apCoef = 0.f;                        // allpass in every line (0 = none)
            float modScale = 1.f;                      // x chorus knob
            float bassMul = 1.1f, midMul = 1.f;        // decay multipliers (treble comes from the damping knob)
            float darkHz = 0.f;                        // extra in-loop low-pass (0 = off)
            int   tapsPerLine = 0;                     // 0 = output the line ends; >0 = that many taps along each line
            float outGain = 1.f;
            int   inDiff = 4;
            float inDiffCoef = 0.6f;
            float sizeMin = 0.4f, sizeMax = 1.6f;      // size knob -> length scale
            float inGain = 0.5f;
        };

        Cfg cfg;
        Line line[N];
        Allpass ap[N], inAp[4];
        int baseLen[N] = {}, apLen[N] = {}, inLen[4] = {};
        float len[N] = {}, tgt[N] = {};
        Wander wan[N];
        float lpLo[N] = {}, lpHi[N] = {}, dk[N] = {}, gLo[N] = {}, gMid[N] = {}, gHi[N] = {};
        float inSign[N] = {}, outL[N] = {}, outR[N] = {};
        float tapPos[N][8] = {}, tapSign[N][8] = {};
        float loC = 0.f, hiC = 0.f, dC = 0.f, modSamples = 0.f, outNorm = 1.f;
        float sr = 48000.f, k = 1.0884f;
        bool fresh = true, flip = false;

        void init (float sampleRate, const Cfg& c)
        {
            cfg = c;
            sr = sampleRate; k = sr / 44100.f;
            for (int i = 0; i < N; ++i)
            {
                const float f = N > 1 ? (float) i / (float) (N - 1) : 0.f;
                const float ms = cfg.lenLoMs * std::pow (cfg.lenHiMs / cfg.lenLoMs, f);
                baseLen[i] = nextPrime ((int) (ms * 0.001f * sr));
                line[i].init ((int) (baseLen[i] * cfg.sizeMax) + 64);
                apLen[i] = nextPrime ((int) (sr * 0.0023f * (1.f + 0.43f * (float) i)));
                ap[i].init (apLen[i] + 8);
                wan[i].seed ((uint32_t) (101 + 17 * i), 0.23f + 0.071f * (float) i, sr);
                inSign[i]  = ((i * 7 + 3) % 5 < 2) ? -1.f : 1.f;
                outL[i]    = ((i * 5 + 1) % 3 == 0) ? -1.f : 1.f;
                outR[i]    = ((i * 3 + 2) % 4 < 2) ? 1.f : -1.f;
                Rng r ((uint32_t) (7 + i));
                for (int t = 0; t < 8; ++t) { tapPos[i][t] = 0.06f + 0.88f * r.next(); tapSign[i][t] = r.next() < 0.5f ? -1.f : 1.f; }
            }
            static const int kIn[4] = { 113, 167, 251, 379 };
            for (int i = 0; i < 4; ++i) { inLen[i] = nextPrime ((int) (kIn[i] * k)); inAp[i].init (inLen[i] + 8); }
            loC = std::exp (-kTwoPi * 450.f / sr);
            hiC = std::exp (-kTwoPi * 3800.f / sr);
            reset();
        }

        void reset()
        {
            for (int i = 0; i < N; ++i) { line[i].clear(); ap[i].line.clear(); lpLo[i] = lpHi[i] = dk[i] = 0.f; }
            for (auto& a : inAp) a.line.clear();
            fresh = true;
        }

        // size01 0..1, chor 0..1, trebMul from damping
        void setDecay (float t60, float trebMul, float size01, float chor, float bassMulOverride = 0.f, float midMulOverride = 0.f)
        {
            const float scale = cfg.sizeMin + (cfg.sizeMax - cfg.sizeMin) * size01;
            const float bm = bassMulOverride > 0.f ? bassMulOverride : cfg.bassMul;
            const float mm = midMulOverride > 0.f ? midMulOverride : cfg.midMul;
            t60 = std::max (0.05f, t60);
            outNorm = 1.f / std::sqrt (std::max (0.3f, t60));                    // steady-state level grows with the decay time: keep the wet level even
            for (int i = 0; i < N; ++i)
            {
                tgt[i] = std::max (8.f, (float) baseLen[i] * scale);
                const float loop = tgt[i] + (cfg.apCoef > 0.f ? (float) apLen[i] : 0.f);
                const float g = std::pow (10.f, -3.f * loop / (t60 * sr));
                gLo[i]  = clampf (std::pow (g, 1.f / bm), 0.001f, 0.9999f);
                gMid[i] = clampf (std::pow (g, 1.f / mm), 0.001f, 0.9999f);
                gHi[i]  = clampf (std::pow (g, 1.f / std::max (0.05f, trebMul)), 0.001f, 0.9999f);
                if (fresh) len[i] = tgt[i];
            }
            fresh = false;
            dC = cfg.darkHz > 0.f ? std::exp (-kTwoPi * cfg.darkHz / sr) : 0.f;
            modSamples = chor * cfg.modScale * 6.f * k;
        }

        static void wht (float* v)
        {
            for (int h = 1; h < N; h <<= 1)
                for (int i = 0; i < N; i += h << 1)
                    for (int j = i; j < i + h; ++j) { const float a = v[j], b = v[j + h]; v[j] = a + b; v[j + h] = a - b; }
        }

        // extraFb (const float* ys) returns a signal that replaces part of the recirculating energy (shimmer); mixAmt 0..1.
        template <class F>
        void process (float x, float& l, float& r, F&& extraFb, float mixAmt)
        {
            for (int i = 0; i < cfg.inDiff; ++i) x = inAp[i].process (x, cfg.inDiffCoef, (float) inLen[i]);

            float ys[N], m[N];
            for (int i = 0; i < N; ++i)
            {
                len[i] += (tgt[i] - len[i]) * 0.0005f;
                float y = line[i].read (len[i] + wan[i].next() * modSamples);
                if (cfg.apCoef > 0.f) y = ap[i].process (y, cfg.apCoef, (float) apLen[i]);
                lpLo[i] = (1.f - loC) * y + loC * lpLo[i];
                lpHi[i] = (1.f - hiC) * y + hiC * lpHi[i];
                y = gLo[i] * lpLo[i] + gMid[i] * (lpHi[i] - lpLo[i]) + gHi[i] * (y - lpHi[i]);
                if (dC > 0.f) { dk[i] = (1.f - dC) * y + dC * dk[i]; y = dk[i]; }
                ys[i] = y; m[i] = y;
            }

            float wl = 0.f, wr = 0.f;
            if (cfg.tapsPerLine > 0)
            {
                for (int i = 0; i < N; ++i)
                    for (int t = 0; t < cfg.tapsPerLine; ++t)
                    {
                        const float v = tapSign[i][t] * line[i].read (std::max (2.f, tapPos[i][t] * len[i]));
                        ((i + t) & 1 ? wr : wl) += v;
                    }
                const float s = 2.f / std::sqrt ((float) (N * cfg.tapsPerLine));
                wl *= s; wr *= s;
            }
            else
                for (int i = 0; i < N; ++i) { wl += outL[i] * ys[i]; wr += outR[i] * ys[i]; }
            const float norm = cfg.outGain * outNorm / std::sqrt ((float) N);
            l = wl * norm; r = wr * norm;

            wht (m);
            const float sc = 1.f / std::sqrt ((float) N);
            const float fb = mixAmt > 0.f ? extraFb (ys) : 0.f;
            const float bias = flip ? 1e-18f : -1e-18f;
            flip = ! flip;
            for (int i = 0; i < N; ++i)
            {
                float v = m[i] * sc;
                if (mixAmt > 0.f) v = (1.f - mixAmt) * v + mixAmt * fb * inSign[i] * 0.9f;
                v += inSign[i] * x * cfg.inGain + bias;
                line[i].push (std::max (-4.f, std::min (4.f, softClip (v))));
            }
        }

        void process (float x, float& l, float& r) { process (x, l, r, [] (const float*) { return 0.f; }, 0.f); }
    };

    //==========================================================================
    inline float t60Of (const Derived& d) { return std::max (0.1f, d.rvbRt60); }
    inline float trebOf (const Derived& d) { return 1.f - 0.85f * d.rvbDamp; }

    struct HallEngine                       // Hall: 16-line network
    {
        Fdn<16> f;
        void prepare (float sr) { Fdn<16>::Cfg c; c.lenLoMs = 22.f; c.lenHiMs = 78.f; c.bassMul = 1.25f; c.modScale = 0.8f; c.outGain = 0.60f; c.inDiff = 4; f.init (sr, c); }
        void reset() { f.reset(); }
        void setParams (const Derived& d) { f.setDecay (t60Of (d), trebOf (d), d.rvbSize, d.rvbChor); }
        void process (float x, float& l, float& r) { f.process (x, l, r); }
    };

    struct DenseHallEngine                  // Dense Hall: 8 lines with an allpass in each, modulation everywhere
    {
        Fdn<8> f;
        void prepare (float sr) { Fdn<8>::Cfg c; c.lenLoMs = 26.f; c.lenHiMs = 92.f; c.apCoef = 0.62f; c.bassMul = 1.15f; c.modScale = 1.6f; c.outGain = 0.69f; c.inDiff = 4; c.inDiffCoef = 0.65f; f.init (sr, c); }
        void reset() { f.reset(); }
        void setParams (const Derived& d) { f.setDecay (t60Of (d), trebOf (d), d.rvbSize, d.rvbChor); }
        void process (float x, float& l, float& r) { f.process (x, l, r); }
    };

    struct ChamberEngine                    // Chamber: four lines, no modulation, 48 taps
    {
        Fdn<4> f;
        void prepare (float sr) { Fdn<4>::Cfg c; c.lenLoMs = 17.f; c.lenHiMs = 41.f; c.apCoef = 0.5f; c.modScale = 0.f; c.tapsPerLine = 8; c.outGain = 0.57f; c.inDiff = 3; f.init (sr, c); }
        void reset() { f.reset(); }
        void setParams (const Derived& d) { f.setDecay (std::min (t60Of (d), 6.f), trebOf (d), d.rvbSize, 0.f); }
        void process (float x, float& l, float& r) { f.process (x, l, r); }
    };

    struct TiledRoomEngine                  // Tiled Room: sparse early front + short dark tail
    {
        Fdn<4> f;
        Line er;
        float sr = 48000.f, size = 0.5f;
        int tapL[16] = {}, tapR[16] = {};
        float gL[16] = {}, gR[16] = {};
        void prepare (float s)
        {
            sr = s;
            Fdn<4>::Cfg c; c.lenLoMs = 5.f; c.lenHiMs = 17.f; c.darkHz = 1100.f; c.modScale = 0.f; c.outGain = 0.42f; c.inDiff = 2; c.sizeMin = 0.5f; c.sizeMax = 1.5f;
            f.init (sr, c);
            er.init ((int) (0.12f * sr) + 16);
            Rng r (31);
            for (int i = 0; i < 16; ++i)
            {
                const float t = 0.0016f + 0.050f * std::pow (r.next(), 1.4f);              // front-loaded: most taps in the first 30 ms
                const float u = 0.0016f + 0.050f * std::pow (r.next(), 1.4f);
                tapL[i] = (int) (t * sr); tapR[i] = (int) (u * sr);
                gL[i] = (r.next() < 0.5f ? -1.f : 1.f) * (0.9f - 0.6f * t / 0.052f);
                gR[i] = (r.next() < 0.5f ? -1.f : 1.f) * (0.9f - 0.6f * u / 0.052f);
            }
            reset();
        }
        void reset() { f.reset(); er.clear(); }
        void setParams (const Derived& d) { size = d.rvbSize; f.setDecay (std::min (t60Of (d) * 0.3f, 1.3f), std::min (1.f, trebOf (d) * 0.8f), d.rvbSize, 0.f); }
        void process (float x, float& l, float& r)
        {
            er.push (x);
            float el = 0.f, eR = 0.f;
            const float sc = 0.6f + 0.8f * size;
            for (int i = 0; i < 16; ++i) { el += gL[i] * er.at (std::max (1, (int) (tapL[i] * sc))); eR += gR[i] * er.at (std::max (1, (int) (tapR[i] * sc))); }
            float tl, tr; f.process (x, tl, tr);
            l = el * 0.09f + tl * 0.33f;
            r = eR * 0.09f + tr * 0.33f;
        }
    };

    struct ParallelHallEngine               // Parallel Hall: six bands, each its own 4-line network and decay time
    {
        static constexpr int B = 6;
        Fdn<4> f[B];
        float lp[B - 1] = {}, co[B - 1] = {};
        float mul[B] = { 1.35f, 1.25f, 1.1f, 0.95f, 0.75f, 0.5f };
        void prepare (float sr)
        {
            static const float edges[B - 1] = { 150.f, 450.f, 1200.f, 3000.f, 7000.f };
            for (int b = 0; b < B; ++b)
            {
                Fdn<4>::Cfg c; c.lenLoMs = 24.f + 3.f * (float) b; c.lenHiMs = 61.f + 5.f * (float) b; c.apCoef = 0.55f; c.modScale = b < 2 ? 0.f : 1.f; c.outGain = 1.0f; c.inDiff = 2; c.bassMul = c.midMul = 1.f;
                f[b].init (sr, c);
            }
            for (int i = 0; i < B - 1; ++i) co[i] = std::exp (-kTwoPi * edges[i] / sr);
            reset();
        }
        void reset() { for (auto& x : f) x.reset(); for (auto& v : lp) v = 0.f; }
        void setParams (const Derived& d)
        {
            const float t60 = t60Of (d), tr = trebOf (d);
            for (int b = 0; b < B; ++b)
            {
                const float tilt = b >= 3 ? std::pow (tr, (float) (b - 2) / 3.f) : 1.f;            // damping bites the upper bands
                f[b].setDecay (t60 * mul[b] * tilt, 1.f, d.rvbSize, d.rvbChor, 1.f, 1.f);
            }
        }
        void process (float x, float& l, float& r)
        {
            float prev = 0.f, band[B];
            for (int i = 0; i < B - 1; ++i) { lp[i] = (1.f - co[i]) * x + co[i] * lp[i]; band[i] = lp[i] - prev; prev = lp[i]; }
            band[B - 1] = x - prev;
            l = r = 0.f;
            for (int b = 0; b < B; ++b) { float bl, br; f[b].process (band[b], bl, br); l += bl; r += br; }
        }
    };

    struct PitchUp                          // octave-up granular shifter: two crossfading read taps sweeping 1 sample per sample
    {
        Line buf; int grain = 4096; float ph = 0.f;
        void init (float sr) { grain = (int) (0.085f * sr); buf.init (grain + 16); ph = 0.f; }
        void clear() { buf.clear(); ph = 0.f; }
        float process (float x)
        {
            buf.push (x);
            ph += 1.f / (float) grain; if (ph >= 1.f) ph -= 1.f;
            const float ph2 = ph + 0.5f >= 1.f ? ph - 0.5f : ph + 0.5f;
            const float w1 = 0.5f - 0.5f * std::cos (kTwoPi * ph), w2 = 0.5f - 0.5f * std::cos (kTwoPi * ph2);
            return w1 * buf.read ((float) grain * (1.f - ph)) + w2 * buf.read ((float) grain * (1.f - ph2));
        }
    };

    struct ShimmerEngine                    // Shimmer: 8-line network, octave up in the feedback
    {
        Fdn<8> f; PitchUp pu; float hp = 0.f, hpC = 0.f;
        void prepare (float sr) { Fdn<8>::Cfg c; c.lenLoMs = 30.f; c.lenHiMs = 96.f; c.apCoef = 0.55f; c.modScale = 1.2f; c.outGain = 0.86f; c.bassMul = 1.0f; f.init (sr, c); pu.init (sr); hpC = std::exp (-kTwoPi * 250.f / sr); reset(); }
        void reset() { f.reset(); pu.clear(); hp = 0.f; }
        void setParams (const Derived& d) { f.setDecay (std::max (0.6f, t60Of (d)), std::min (1.f, trebOf (d) + 0.1f), d.rvbSize, d.rvbChor); }
        void process (float x, float& l, float& r)
        {
            f.process (x, l, r, [this] (const float* ys)
            {
                float s = 0.f; for (int i = 0; i < 8; ++i) s += ys[i];
                s *= 0.35355339f;
                hp = (1.f - hpC) * s + hpC * hp;
                return pu.process (s - hp);
            }, 0.28f);
        }
    };

    struct ReverseEngine                    // Reverse: rising early reflections, then a dark tail that peaks late
    {
        Fdn<8> f; Line er, dl; float sr = 48000.f, size = 0.5f;
        int tp[20] = {}; float tg[20] = {}, ts[20] = {};
        void prepare (float s)
        {
            sr = s;
            Fdn<8>::Cfg c; c.lenLoMs = 22.f; c.lenHiMs = 70.f; c.darkHz = 2600.f; c.bassMul = 1.0f; c.modScale = 1.f; c.outGain = 0.84f; f.init (sr, c);
            er.init ((int) (0.16f * sr) + 16); dl.init ((int) (0.16f * sr) + 16);
            Rng r (77);
            for (int i = 0; i < 20; ++i)
            {
                const float t = (float) (i + 1) / 20.f;
                tp[i] = (int) ((0.004f + 0.070f * t + 0.003f * r.next()) * sr);
                tg[i] = 0.12f + 0.88f * t;                                                   // gain rises with time: the "reverse" slope
                ts[i] = r.next() < 0.5f ? -1.f : 1.f;
            }
            reset();
        }
        void reset() { f.reset(); er.clear(); dl.clear(); }
        void setParams (const Derived& d) { size = d.rvbSize; f.setDecay (std::min (t60Of (d), 5.f), std::min (1.f, trebOf (d)), d.rvbSize, d.rvbChor); }
        void process (float x, float& l, float& r)
        {
            er.push (x); dl.push (x);
            const float sc = 0.7f + 0.6f * size;
            float el = 0.f, eR = 0.f;
            for (int i = 0; i < 20; ++i)
            {
                const float v = ts[i] * tg[i] * er.at (std::max (1, (int) (tp[i] * sc)));
                (i & 1 ? eR : el) += v;
            }
            float tl, tr; f.process (dl.at (std::max (1, (int) (0.07f * sc * sr))), tl, tr);      // the tail starts where the swell peaks
            l = el * 0.12f + tl * 0.29f;
            r = eR * 0.12f + tr * 0.29f;
        }
    };

    struct SpringEngine                     // Spring: three dispersive lines with feedback and a touch of drive
    {
        static constexpr int S = 3, K = 22;
        struct Spring
        {
            Line d; float len = 1000.f, tgt = 1000.f, fb = 0.8f, lp = 0.f; float z[K] = {}; float last = 0.f; Wander w;
        };
        Spring sp[S];
        float sr = 48000.f, lpC = 0.5f, drive = 1.f, mod = 0.f, apA = 0.62f;
        bool fresh = true;
        void prepare (float s)
        {
            sr = s;
            static const float ms[S] = { 33.f, 38.5f, 44.f };
            for (int i = 0; i < S; ++i) { sp[i].d.init ((int) (0.09f * sr) + 64); sp[i].len = sp[i].tgt = ms[i] * 0.001f * sr; sp[i].w.seed ((uint32_t) (5 + i), 0.31f + 0.1f * (float) i, sr); }
            reset();
        }
        void reset() { for (auto& s : sp) { s.d.clear(); s.lp = 0.f; s.last = 0.f; for (auto& v : s.z) v = 0.f; } fresh = true; }
        void setParams (const Derived& d)
        {
            static const float ms[S] = { 33.f, 38.5f, 44.f };
            const float t60 = clampf (d.rvbRt60 * 0.6f, 0.25f, 4.5f);
            const float scale = 0.7f + 0.6f * d.rvbSize;
            for (int i = 0; i < S; ++i)
            {
                sp[i].tgt = ms[i] * 0.001f * sr * scale;
                if (fresh) sp[i].len = sp[i].tgt;
                sp[i].fb = std::pow (10.f, -3.f * (sp[i].tgt + K * 4.f) / (t60 * sr));
            }
            fresh = false;
            lpC = std::exp (-kTwoPi * (900.f + 5200.f * (1.f - d.rvbDamp)) / sr);
            mod = d.rvbChor * 5.f * sr / 48000.f;
            apA = 0.55f + 0.2f * (1.f - d.rvbDamp);
        }
        void process (float x, float& l, float& r)
        {
            float out[S];
            for (int i = 0; i < S; ++i)
            {
                Spring& s = sp[i];
                s.len += (s.tgt - s.len) * 0.0005f;
                float y = s.d.read (s.len + s.w.next() * mod);
                y = std::tanh (y * 1.3f) * 0.77f;                                              // the boing: a little drive in the loop
                s.lp = (1.f - lpC) * y + lpC * s.lp; y = s.lp;
                // dispersion chain: first-order allpasses, low frequencies travel slower (the chirp)
                float v = x + s.fb * y;
                for (int k = 0; k < K; ++k)
                {
                    const float o = apA * v + s.z[k];
                    s.z[k] = v - apA * o;
                    v = o;
                }
                s.d.push (std::max (-4.f, std::min (4.f, v)));
                out[i] = y;
            }
            l = (out[0] * 1.f + out[2] * 0.6f - out[1] * 0.3f) * 0.36f;
            r = (out[1] * 1.f + out[2] * 0.6f - out[0] * 0.3f) * 0.36f;
        }
    };

    struct GatedEngine                      // Gated: 64-tap feed-forward delay with a hard gate; "decay" = gate length
    {
        static constexpr int T = 64;
        Line tdl; Allpass a1, a2; float sr = 48000.f, gateS = 0.3f, lpL = 0.f, lpR = 0.f, lpC = 0.5f;
        float posL[T] = {}, posR[T] = {}, sgL[T] = {}, sgR[T] = {};
        void prepare (float s)
        {
            sr = s; tdl.init ((int) (1.4f * sr) + 16); a1.init ((int) (0.02f * sr)); a2.init ((int) (0.02f * sr));
            Rng r (4242);
            for (int i = 0; i < T; ++i)
            {
                posL[i] = 0.004f + 0.996f * ((float) i + r.next()) / (float) T;               // jittered grid: even, but not a comb
                posR[i] = 0.004f + 0.996f * ((float) i + r.next()) / (float) T;
                sgL[i] = r.next() < 0.5f ? -1.f : 1.f; sgR[i] = r.next() < 0.5f ? -1.f : 1.f;
            }
            reset();
        }
        void reset() { tdl.clear(); a1.line.clear(); a2.line.clear(); lpL = lpR = 0.f; }
        void setParams (const Derived& d)
        {
            gateS = clampf (0.08f + 0.12f * d.rvbRt60 * (0.6f + 0.8f * d.rvbSize), 0.08f, 1.25f);
            lpC = std::exp (-kTwoPi * (1500.f + 9000.f * (1.f - d.rvbDamp)) / sr);
        }
        void process (float x, float& l, float& r)
        {
            x = a1.process (x, 0.6f, 331.f * sr / 44100.f);
            x = a2.process (x, 0.55f, 577.f * sr / 44100.f);
            tdl.push (x);
            float wl = 0.f, wr = 0.f;
            const float fade = 0.012f;                                                         // short ramp at the gate edge, not a click
            for (int i = 0; i < T; ++i)
            {
                float gl = 1.f, gr = 1.f;
                if (posL[i] * gateS > gateS - fade) gl = (gateS - posL[i] * gateS) / fade;
                if (posR[i] * gateS > gateS - fade) gr = (gateS - posR[i] * gateS) / fade;
                wl += sgL[i] * gl * tdl.at (std::max (1, (int) (posL[i] * gateS * sr)));
                wr += sgR[i] * gr * tdl.at (std::max (1, (int) (posR[i] * gateS * sr)));
            }
            lpL = (1.f - lpC) * wl + lpC * lpL; lpR = (1.f - lpC) * wr + lpC * lpR;
            l = lpL * 0.055f; r = lpR * 0.055f;
        }
    };

    //==========================================================================
    // The Seedling reverb: pre-delay + swell + type switch (with a short crossfade) + dry/wet.
    struct Reverb
    {
        static constexpr int kNumTypes = 12;
        Plate plate; ChamberEngine chamber; TiledRoomEngine tiled; HallEngine hall; DenseHallEngine dense; ParallelHallEngine par;
        SpringEngine spring; ShimmerEngine shimmer; ReverseEngine rev; GatedEngine gated;
        Line pre;
        float sr = 48000.f, preSamples = 0.f, riseCo = 1.f, env = 0.f, gate = 1.f, mixCos = 1.f, mixSin = 0.f;
        int cur = 0, prev = -1;
        float xf = 1.f, xfStep = 0.001f;
        Derived last {};

        static int clampType (float v) { return std::max (0, std::min (kNumTypes - 1, (int) std::lround (v))); }

        void prepare (float sampleRate)
        {
            sr = sampleRate;
            plate.prepare (sr); chamber.prepare (sr); tiled.prepare (sr); hall.prepare (sr); dense.prepare (sr); par.prepare (sr);
            spring.prepare (sr); shimmer.prepare (sr); rev.prepare (sr); gated.prepare (sr);
            pre.init ((int) (0.08f * sr) + 16);
            xfStep = 1.f / (0.25f * sr);
            env = 0.f; gate = 1.f; cur = 0; prev = -1; xf = 1.f;
            applyPlateConfig (0);
        }

        void reset()
        {
            plate.reset(); chamber.reset(); tiled.reset(); hall.reset(); dense.reset(); par.reset();
            spring.reset(); shimmer.reset(); rev.reset(); gated.reset();
            pre.clear(); env = 0.f; gate = 1.f; prev = -1; xf = 1.f;
        }

        // the three plates share one tank; only the settings differ
        void applyPlateConfig (int type)
        {
            Plate::Config c;
            if (type == 1) { c.density = 0.50f; c.diff1 = 0.68f; c.diff2 = 0.45f; c.modScale = 0.55f; c.lpHz = 5600.f; c.hpHz = 110.f; c.erGain = 0.f; c.outGain = 3.0f; }
            else if (type == 2) { c.density = 0.68f; c.diff1 = 0.74f; c.diff2 = 0.60f; c.inDiffAdd = 0.08f; c.modScale = 0.5f; c.erGain = 0.25f; c.outGain = 1.85f; }
            plate.cfg = c;
        }

        static bool isPlate (int t) { return t >= 0 && t <= 2; }

        void setParams (const Derived& d)
        {
            last = d;
            const int t = clampType (d.rvbType);
            if (t != cur)
            {
                const bool samePlate = isPlate (t) && isPlate (cur);
                if (! samePlate) { prev = cur; xf = 0.f; engineReset (t); }                    // fade the old tail out while the new engine starts clean
                cur = t;
            }
            if (isPlate (cur)) { applyPlateConfig (cur); plate.setParams (d); }
            else engineSet (cur, d);
            if (prev >= 0 && ! isPlate (prev)) engineSet (prev, d);

            preSamples = d.rvbAtk * 0.06f * sr;
            const float tau = d.rvbAtk * 0.5f * sr;
            riseCo = tau < 1.f ? 1.f : 1.f - std::exp (-1.f / tau);
            mixCos = std::cos (d.rvbMix * kPi * 0.5f);
            mixSin = std::sin (d.rvbMix * kPi * 0.5f);
        }

        void engineReset (int t)
        {
            switch (t)
            {
                case 0: case 1: case 2: plate.reset(); plate.fresh = true; break;
                case 3: chamber.reset(); break;   case 4: tiled.reset(); break;  case 5: hall.reset(); break;
                case 6: dense.reset(); break;     case 7: par.reset(); break;    case 8: spring.reset(); break;
                case 9: shimmer.reset(); break;   case 10: rev.reset(); break;  default: gated.reset(); break;
            }
        }

        void engineSet (int t, const Derived& d)
        {
            switch (t)
            {
                case 3: chamber.setParams (d); break;  case 4: tiled.setParams (d); break;  case 5: hall.setParams (d); break;
                case 6: dense.setParams (d); break;    case 7: par.setParams (d); break;    case 8: spring.setParams (d); break;
                case 9: shimmer.setParams (d); break;  case 10: rev.setParams (d); break;  case 11: gated.setParams (d); break;
                default: break;
            }
        }

        void run (int t, float x, float& l, float& r)
        {
            switch (t)
            {
                case 0: case 1: case 2: plate.process (x, l, r); break;
                case 3: chamber.process (x, l, r); break;  case 4: tiled.process (x, l, r); break;  case 5: hall.process (x, l, r); break;
                case 6: dense.process (x, l, r); break;    case 7: par.process (x, l, r); break;    case 8: spring.process (x, l, r); break;
                case 9: shimmer.process (x, l, r); break;  case 10: rev.process (x, l, r); break;  default: gated.process (x, l, r); break;
            }
        }

        void process (float in, float& outL, float& outR)
        {
            pre.push (in);
            float x = pre.at (std::max (1, (int) preSamples + 1));
            env = std::max (std::fabs (in), env * 0.9995f);                                     // swell: the wet input fades in after silence
            const float target = env > 0.002f ? 1.f : 0.f;
            gate += (target - gate) * (target > gate ? riseCo : 0.002f);
            x *= gate;

            float l, r;
            run (cur, x, l, r);
            if (prev >= 0)
            {
                float pl, pr;
                run (prev, 0.f, pl, pr);                                                        // the old engine just rings out
                const float a = xf * xf * (3.f - 2.f * xf);
                l = l * a + pl * (1.f - a); r = r * a + pr * (1.f - a);
                xf += xfStep;
                if (xf >= 1.f) { xf = 1.f; prev = -1; }
            }
            if (! std::isfinite (l) || ! std::isfinite (r)) { reset(); l = r = 0.f; }
            outL = l; outR = r;
        }
    };
}
}
