#include "Engine.h"
#include "DuskVerbs.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace seed
{

//==============================================================================
Derived makeDerived (const float* p)
{
    Derived d {};

    d.envScale = 0.1f * std::pow (100.f, p[env_time]);          // 0.1x .. 10x, 1x at the middle
    d.envLoop  = p[env_loop];
    d.envKf    = p[env_kf];
    d.atkMul   = std::pow (4.f, (p[env_tilt] - 0.5f) * 2.f);    // tilt: long attacks <-> long decays
    d.dcyMul   = 1.f / d.atkMul;

    d.volAtk = map::volAtk (p[vol_atk]);
    d.volDcy = map::volDcy (p[vol_dcy]);
    d.volSus = p[vol_sus];
    d.volRel = clampf (0.12f + 0.45f * d.volDcy, 0.12f, 2.0f); // no release knob in the design: follows decay
    d.fadeTau = p[vol_fade] < 0.01f ? 0.f : 0.3f * std::pow (100.f, 1.f - p[vol_fade]);

    d.modAtk   = map::modAtk (p[mod_atk]);
    d.modDcy   = map::modDcy (p[mod_dcy]);
    d.modShape = p[mod_sh];
    d.modVel   = p[mod_vel];

    d.lfoHz     = map::lfoHz (p[lfo_rate]);
    d.vibSemis  = p[lfo_amt] * std::min (1.f, 2.f * (1.f - p[lfo_bal]));   // up to +-1 semitone
    d.tremDepth = p[lfo_amt] * std::min (1.f, 2.f * p[lfo_bal]);
    d.lfoDelay  = p[lfo_dly] * 2.f;

    d.aForm  = p[a_form];  d.aNoise = p[a_noise]; d.aMod = p[a_mod];
    d.aColor = p[a_color]; d.aSemi  = p[a_freq];
    d.fmMod  = p[fm_mod];  d.fmAmt  = p[fm_amt];
    d.mixMod = p[mix_mod]; d.mix    = p[osc_mix];
    d.bForm  = p[b_form];  d.bNoise = p[b_noise]; d.bMod = p[b_mod];
    d.sub    = p[sub_am];  d.bSemi  = p[b_freq];
    d.bHoldHz = p[b_sh] < 0.01f ? 0.f : 18000.f * std::pow (0.01f, std::pow (p[b_sh], 0.7f));

    d.fltType = p[flt_type];
    d.fltK    = 1.f / map::fltQ (p[flt_q]);
    d.fltHz   = map::fltHz (p[flt_freq]);
    d.fltKf   = p[flt_kf];
    d.fltMod  = p[flt_mod];
    d.fltSep  = p[flt_sep];

    d.sat     = p[saturate];
    d.rvbMix  = p[rvb_mix];
    d.rvbAtk  = p[rvb_atk];
    d.rvbRt60 = map::rvbRt60 (p[rvb_len]);
    d.rvbDamp = p[rvb_damp];
    d.rvbChor = p[rvb_chor];
    d.rvbSize = p[rvb_size];
    d.rvbType = p[rvb_type];
    d.srcType = std::round (clampf (p[src_type], 0.f, (float) (kNumSources - 1)));

    d.glideSec = p[glide] < 0.01f ? 0.f : 0.004f * std::pow (750.f, p[glide]);   // 4 ms .. 3 s
    d.mono = p[mono];
    d.bass = p[adj_bass]; d.treb = p[adj_treb]; d.pan = p[adj_pan]; d.clip = p[adj_clip];
    return d;
}

//==============================================================================
void Biquad::setLowShelf (float sr, float f, float gainDb)
{
    const float A = std::pow (10.f, gainDb / 40.f);
    const float w0 = kTwoPi * f / sr, cw = std::cos (w0), sw = std::sin (w0);
    const float alpha = sw * 0.5f * 1.41421356f;
    const float tsa = 2.f * std::sqrt (A) * alpha;
    const float a0 = (A + 1) + (A - 1) * cw + tsa;
    b0 = A * ((A + 1) - (A - 1) * cw + tsa) / a0;
    b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
    b2 = A * ((A + 1) - (A - 1) * cw - tsa) / a0;
    a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
    a2 = ((A + 1) + (A - 1) * cw - tsa) / a0;
}

void Biquad::setHighShelf (float sr, float f, float gainDb)
{
    const float A = std::pow (10.f, gainDb / 40.f);
    const float w0 = kTwoPi * f / sr, cw = std::cos (w0), sw = std::sin (w0);
    const float alpha = sw * 0.5f * 1.41421356f;
    const float tsa = 2.f * std::sqrt (A) * alpha;
    const float a0 = (A + 1) - (A - 1) * cw + tsa;
    b0 = A * ((A + 1) + (A - 1) * cw + tsa) / a0;
    b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
    b2 = A * ((A + 1) + (A - 1) * cw - tsa) / a0;
    a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
    a2 = ((A + 1) - (A - 1) * cw - tsa) / a0;
}

float Biquad::magnitude (float freq, float sr) const
{
    const float w = kTwoPi * freq / sr;
    const float c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    const float nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
    const float dr = 1.f + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
    return std::sqrt ((nr * nr + ni * ni) / std::max (1e-12f, dr * dr + di * di));
}

//==============================================================================
namespace
{
    constexpr int kChunk = 32;
    constexpr int kVoices = 8;

    struct Rng
    {
        uint32_t s = 2463534242u;
        float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.f / 2147483648.f); }
    };

    // Zavalishin TPT state-variable filter.
    struct Svf
    {
        float ic1 = 0.f, ic2 = 0.f;
        void reset() { ic1 = ic2 = 0.f; }
        // type 0..1 : low-pass -> band-pass -> high-pass
        float process (float x, float g, float k, float type)
        {
            const float a1 = 1.f / (1.f + g * (g + k));
            const float a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.f * v1 - ic1;
            ic2 = 2.f * v2 - ic2;
            const float lp = v2, bp = k * v1, hp = x - k * v1 - v2;
            const float w = type * 2.f;
            return w < 1.f ? lp + (bp - lp) * w : bp + (hp - bp) * (w - 1.f);
        }
    };

    //==========================================================================
    struct Voice
    {
        bool active = false, keyDown = false, sustained = false;
        int note = 60;
        float vel = 1.f;
        int64_t startOrder = 0;

        int vstage = 0;               // 0 idle, 1 attack, 2 decay/sustain, 3 release
        float vlvl = 0.f, fadeGain = 1.f;
        float glideNote = 60.f;       // the pitch actually sounding (in notes); glides towards `note`
        float modT = 0.f, ageS = 0.f;
        float phA = 0.f, phB = 0.f, phSub = 0.f, phLfo = 0.f, holdPh = 0.f, held = 0.f;
        float nA = 0.f, nB = 0.f;
        Rng rng;
        Svf f1, f2;

        // ---- extra sound sources (the A slot) -----------------------------------------------------------------------
        bool primed = false;
        static constexpr int kKsMask = 16383;
        float ks[kKsMask + 1];                       // pluck: the string
        int ksW = 0; float ksLp = 0.f, ksC = 0.5f, ksG = 0.99f;
        float phU[7] = {};                           // unison: seven saw phases
        Svf fv[3];                                   // vowel: three formant filters
        float my1[6] = {}, my2[6] = {};              // struck: six resonators
        float mC[6] = {}, mR2[6] = {}, mIn[6] = {};

        void start (int n, float v, int64_t order, uint32_t seedBits)
        {
            active = true; keyDown = true; sustained = false;
            note = n; vel = v; startOrder = order; glideNote = (float) n;
            vstage = 1; vlvl = 0.f; fadeGain = 1.f;
            modT = 0.f; ageS = 0.f;
            phA = phB = phSub = holdPh = held = 0.f; phLfo = 0.f;
            nA = nB = 0.f;
            rng.s = 2463534242u ^ (seedBits * 2654435761u);
            f1.reset(); f2.reset();
            primed = false;
            for (auto& f : fv) f.reset();
            ksLp = 0.f;
            for (int k = 0; k < 6; ++k) my1[k] = my2[k] = 0.f;
        }

        void release() { keyDown = false; sustained = false; if (vstage != 0) vstage = 3; }

        // Set up the A-slot source the first time a note renders (the knob values are known then).
        void prime (const Derived& d, float sr, float baseHz)
        {
            const int src = (int) d.srcType;
            if (src == 5)
                for (int k = 0; k < 7; ++k) phU[k] = 0.5f * (rng.next() + 1.f);
            if (src == 2)                                       // pluck: fill the string with a (softened) noise burst
            {
                const float f = std::max (20.f, baseHz);
                const int len = std::min (kKsMask - 8, (int) std::ceil (sr / f) + 4);
                const float c = 0.9f * (1.f - d.aNoise);        // soft pick -> dark burst, hard pick -> bright burst
                float y = 0.f, mean = 0.f;
                for (int i = 0; i < len; ++i) { y = (1.f - c) * rng.next() + c * y; ks[i] = y; mean += y; }
                mean /= (float) len;
                float e2 = 0.f;
                for (int i = 0; i < len; ++i) { ks[i] -= mean; e2 += ks[i] * ks[i]; }
                const float norm = 0.4f / std::sqrt (e2 / (float) len + 1e-9f);          // the same strike level on every note
                for (int i = 0; i < len; ++i) ks[i] *= norm;
                for (int i = len; i <= kKsMask; ++i) ks[i] = 0.f;
                ksW = len; ksLp = 0.f;
            }
            if (src == 4)                                       // struck: the strike sets the resonators ringing
            {
                const float k = 0.6f + 0.4f * vel;
                for (int i = 0; i < 6; ++i) { my1[i] = 0.f; my2[i] = -k / (1.f + 0.6f * (float) i); }   // scaled by sin(w) when the coefficients are set
                strikePending = true;
            }
        }
        bool strikePending = false;

        void render (float* out, int n, const Derived& d, float sr, float bend, const Wavetable* wt)
        {
            const float noteRel = (float) (note - 60);
            const float vs = d.envScale * std::exp2 (-d.envKf * noteRel / 12.f);

            const float atkS   = std::max (24.f, d.volAtk * d.atkMul * vs * sr);
            const float dcyS   = std::max (24.f, d.volDcy * d.dcyMul * vs * sr);
            const float dcyCo  = 1.f - std::exp (-5.f / dcyS);
            const float relS   = std::max (64.f, d.volRel * vs * sr);
            const float relCo  = std::exp (-5.f / relS);
            const float fadeMul = d.fadeTau > 0.f ? std::exp (-1.f / (d.fadeTau * vs * sr)) : 1.f;

            const float mAtkS  = std::max (8.f, d.modAtk * d.atkMul * vs * sr);
            const float mDcyS  = std::max (16.f, d.modDcy * d.dcyMul * vs * sr);
            const float mExp   = 1.f + 5.f * d.modShape;
            const float loopS  = d.envLoop > 0.02f ? std::max (0.015f * sr, (mAtkS + mDcyS) * (1.f + 3.f * (1.f - d.envLoop))) : 0.f;
            const float velMod = 1.f - d.modVel * (1.f - vel);
            const float velAmp = 0.35f + 0.65f * vel;

            float pitchRel = glideNote - 60.f;
            auto baseOf = [&] (float semi) { return 440.f * std::exp2 ((pitchRel - 9.f + semi + bend) / 12.f); };
            float baseA = baseOf (d.aSemi), baseB = baseOf (d.bSemi);
            const float glideCo = d.glideSec > 0.f ? 1.f - std::exp (-3.f / (d.glideSec * sr)) : 1.f;   // ~95% of the way in glideSec
            const float lfoInc = d.lfoHz / sr;
            const float lfoDelayS = d.lfoDelay * sr;
            const float holdInc = d.bHoldHz > 0.f ? d.bHoldHz / sr : 0.f;
            const int src = (int) d.srcType;
            if (! primed) { prime (d, sr, baseA); primed = true; }
            const float gSep = std::exp2 (d.fltSep * 3.f);
            float fcBase = d.fltHz * std::exp2 (d.fltKf * pitchRel / 12.f);
            const float fcMax = 0.45f * sr;

            for (int i = 0; i < n; ++i)
            {
                // --- glide: slide the sounding pitch towards the key --------------------
                if (glideNote != (float) note)
                {
                    glideNote += ((float) note - glideNote) * glideCo;
                    if (std::fabs ((float) note - glideNote) < 1e-3f) glideNote = (float) note;
                    pitchRel = glideNote - 60.f;
                    baseA = baseOf (d.aSemi); baseB = baseOf (d.bSemi);
                    fcBase = d.fltHz * std::exp2 (d.fltKf * pitchRel / 12.f);
                }

                // --- LFO ---------------------------------------------------
                phLfo += lfoInc; if (phLfo >= 1.f) phLfo -= 1.f;
                const float ramp = lfoDelayS > 1.f ? std::min (1.f, ageS / lfoDelayS) : 1.f;
                ageS += 1.f;
                const float lfo = std::sin (kTwoPi * phLfo) * ramp;

                // --- modulation envelope ---------------------------------------
                float m;
                if (modT < mAtkS) m = modT / mAtkS;
                else { const float u = (modT - mAtkS) / mDcyS; m = u >= 1.f ? 0.f : std::pow (1.f - u, mExp); }
                modT += 1.f;
                if (loopS > 0.f && modT >= loopS) modT -= loopS;
                m *= velMod;

                // --- volume envelope -----------------------------------------
                switch (vstage)
                {
                    case 1: vlvl += 1.f / atkS; if (vlvl >= 1.f) { vlvl = 1.f; vstage = 2; } break;
                    case 2: vlvl += (d.volSus - vlvl) * dcyCo; fadeGain *= fadeMul; break;
                    case 3: vlvl *= relCo; break;
                    default: break;
                }
                const float env = vlvl * fadeGain;
                if ((vstage == 3 && env < 1e-4f) || (vstage == 2 && env < 1e-4f))
                {
                    active = false; vstage = 0;
                    return;
                }

                // --- pitch ----------------------------------------------------
                const float vibMul = d.vibSemis > 1e-4f ? std::exp2 (d.vibSemis * lfo / 12.f) : 1.f;
                const float dtA = std::min (0.49f, baseA * vibMul / sr);
                const float dtB = std::min (0.49f, baseB * vibMul / sr);

                // --- oscillator B (also the FM source) ----------------------------
                phB += dtB; if (phB >= 1.f) phB -= 1.f;
                float b = morphShape (true, d.bForm + d.bMod * m, phB, dtB);
                if (holdInc > 0.f)
                {
                    holdPh += holdInc;
                    if (holdPh >= 1.f) { holdPh -= std::floor (holdPh); held = b; }
                    b = held;
                }
                nB += 0.35f * (rng.next() - nB);
                b += d.bNoise * 1.6f * nB;
                phSub += dtB * 0.5f; if (phSub >= 1.f) phSub -= 1.f;
                b += d.sub * std::sin (kTwoPi * phSub);

                // --- the A slot: oscillator, wavetable, pluck, vowel, struck or unison ----------------------------
                const float fmDepth = clampf (d.fmAmt + d.fmMod * m, 0.f, 1.f);
                const float fmPh = fmDepth * fmDepth * 1.5f * b;
                nA += 0.35f * (rng.next() - nA);
                float a = 0.f;
                switch (src)
                {
                    default:
                    {   // 0: two-oscillator morph, phase-modulated by B
                        phA += dtA; if (phA >= 1.f) phA -= 1.f;
                        float pm = phA + fmPh; pm -= std::floor (pm);
                        const float pw = d.aColor > 0.f ? colorWarp (pm, d.aColor) : pm;
                        a = morphShape (false, d.aForm + d.aMod * m, pw, dtA) + d.aNoise * 1.6f * nA;
                        break;
                    }
                    case 1:
                    {   // wavetable: A Form scans the frames, colour warps the phase, FM from B works as on the oscillator
                        phA += dtA; if (phA >= 1.f) phA -= 1.f;
                        float pm = phA + fmPh; pm -= std::floor (pm);
                        const float pw = d.aColor > 0.f ? colorWarp (pm, d.aColor) : pm;
                        a = wt->read (d.aForm + d.aMod * m, pw, Wavetable::levelFor (dtA)) + d.aNoise * 1.6f * nA;
                        break;
                    }
                    case 2:
                    {   // pluck (Karplus-Strong): A Form = brightness, A Color = how long it rings, A Noise = pick hardness
                        const float f = std::max (20.f, dtA * sr);
                        if ((i & 7) == 0)
                        {
                            // the loop filter is the classic two-tap average (1-b, b): it loses almost nothing at the fundamental and
                            // more and more towards the top, so A Form sets the brightness and A Color alone sets how long it rings
                            const float bright = clampf (d.aForm + d.aMod * m, 0.f, 1.f);
                            ksC = 0.5f * (1.f - bright) + 0.004f;
                            const float w0 = kTwoPi * f / sr;
                            const float mag = std::sqrt (std::max (0.25f, 1.f - 2.f * ksC * (1.f - ksC) * (1.f - std::cos (w0))));
                            const float T = 0.1f * std::pow (120.f, d.aColor);
                            ksG = std::min (0.99995f, std::exp (-6.9f / (T * f)) / mag);
                        }
                        const float c = ksC, g = ksG;
                        const float delay = std::max (2.f, sr / f - c);
                        const float rp = (float) ksW - delay;
                        const int i0 = (int) std::floor (rp);
                        const float fr = rp - (float) i0;
                        auto at = [this] (int k) { return ks[k & kKsMask]; };
                        const float xm1 = at (i0 - 1), x0 = at (i0), x1 = at (i0 + 1), x2 = at (i0 + 2);
                        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                        const float y = ((c3 * fr + c2) * fr + c1) * fr + x0;
                        ks[ksW & kKsMask] = ((1.f - c) * y + c * ksLp) * g;
                        ksLp = y;
                        ++ksW;
                        a = y * 1.4f;
                        break;
                    }
                    case 3:
                    {   // vowel: a glottal pulse through three formant filters. A Form = vowel (a e i o u), Color = pulse shape, Noise = breath
                        static const float F[5][3] = { { 800.f, 1150.f, 2800.f }, { 400.f, 1600.f, 2700.f }, { 350.f, 1700.f, 2700.f },
                                                       { 450.f, 800.f, 2830.f }, { 325.f, 700.f, 2530.f } };
                        phA += dtA; if (phA >= 1.f) phA -= 1.f;
                        const float vp = clampf (d.aForm + d.aMod * m, 0.f, 1.f) * 4.f;
                        const int vi = std::min (3, (int) vp);
                        const float vf = vp - (float) vi;
                        const float src0 = sawShape (phA, dtA) * (1.f - d.aColor) + 0.9f * pulseShape (phA, 0.14f, dtA) * d.aColor + d.aNoise * 1.6f * nA;
                        static const float G[3] = { 1.f, 0.7f, 0.45f };
                        float v = 0.f;
                        for (int k = 0; k < 3; ++k)
                        {
                            const float fc = clampf (F[vi][k] + (F[vi + 1][k] - F[vi][k]) * vf, 80.f, 0.45f * sr);
                            v += G[k] * fv[k].process (src0, std::tan (kPi * fc / sr), 0.14f, 0.5f);
                        }
                        a = v * 1.5f;
                        break;
                    }
                    case 4:
                    {   // struck: six ringing modes. A Form = what was hit (harmonic, marimba, bar, bell, membrane),
                        //         A Color = how long it rings, A Noise = keeps it excited (bowed / blown)
                        static const float R[5][6] = { { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f }, { 1.f, 3.9f, 9.9f, 17.6f, 27.6f, 39.8f },
                                                       { 1.f, 2.76f, 5.4f, 8.93f, 13.34f, 18.64f }, { 0.56f, 0.92f, 1.19f, 1.71f, 2.f, 2.74f },
                                                       { 1.f, 1.59f, 2.14f, 2.3f, 2.65f, 2.92f } };
                        if ((i & 7) == 0)
                        {
                            const float sp = clampf (d.aForm + d.aMod * m, 0.f, 1.f) * 4.f;
                            const int si = std::min (3, (int) sp);
                            const float sf = sp - (float) si;
                            const float T0 = 0.05f * std::pow (160.f, d.aColor);
                            const float f0 = std::max (20.f, baseA * vibMul);
                            for (int k = 0; k < 6; ++k)
                            {
                                const float fk = f0 * (R[si][k] + (R[si + 1][k] - R[si][k]) * sf);
                                const float Tk = T0 / (1.f + 0.7f * (float) k);
                                const float r = fk < 0.45f * sr ? std::exp (-6.9f / (Tk * sr)) : 0.f;
                                const float w = kTwoPi * fk / sr;
                                mC[k] = 2.f * r * std::cos (w);
                                mR2[k] = r * r;
                                mIn[k] = (1.f - r) * 3.f / (1.f + 0.6f * (float) k);
                                if (strikePending) my2[k] *= std::sin (w) * (r > 0.f ? 1.f : 0.f);       // first call: turn the strike into the starting state
                            }
                            strikePending = false;
                        }
                        const float x = d.aNoise * 0.5f * nA * 6.f;
                        float v = 0.f;
                        for (int k = 0; k < 6; ++k)
                        {
                            const float y = mC[k] * my1[k] - mR2[k] * my2[k] + x * mIn[k];
                            my2[k] = my1[k]; my1[k] = y;
                            v += y;
                        }
                        a = v * 0.9f;
                        break;
                    }
                    case 5:
                    {   // unison: seven detuned saws. A Form = detune, A Color = saw -> square, FM from B works too
                        static const float OFF[7] = { -1.f, -0.62f, -0.27f, 0.f, 0.27f, 0.62f, 1.f };
                        const float spread = 0.04f + 0.55f * std::pow (clampf (d.aForm + d.aMod * m, 0.f, 1.f), 1.5f);
                        const float shapeForm = 0.667f + 0.333f * d.aColor;
                        float v = 0.f;
                        for (int k = 0; k < 7; ++k)
                        {
                            const float dtk = std::min (0.49f, dtA * std::exp2 (OFF[k] * spread / 12.f));
                            phU[k] += dtk; if (phU[k] >= 1.f) phU[k] -= 1.f;
                            float pm = phU[k] + fmPh; pm -= std::floor (pm);
                            v += morphShape (false, shapeForm, pm, dtk);
                        }
                        a = v * 0.42f + d.aNoise * 1.6f * nA;
                        break;
                    }
                }

                // --- mix --------------------------------------------------------
                const float mx = clampf (d.mix + d.mixMod * m, 0.f, 1.f);
                float sig = a * std::sqrt (1.f - mx) + b * std::sqrt (mx);

                // --- filter (two parallel SVFs, "separation" splits their cutoffs) -------
                const float fc1 = clampf (fcBase * std::exp2 (d.fltMod * m * 4.f), 20.f, fcMax);
                const float fc2 = clampf (fc1 * gSep, 20.f, fcMax);
                const float g1 = std::tan (kPi * fc1 / sr), g2 = std::tan (kPi * fc2 / sr);
                sig = 0.5f * (f1.process (sig, g1, d.fltK, d.fltType) + f2.process (sig, g2, d.fltK, d.fltType));

                // --- amp ---------------------------------------------------------
                const float trem = 1.f - d.tremDepth * 0.5f * (1.f - lfo);
                out[i] += sig * env * velAmp * trem * 0.25f;
            }
        }
    };

    // The reverb is Source/DuskVerbs.h: twelve reverbs after DuskVerb's engines, chosen by rvb_type.
    using Reverb = dusk::Reverb;
}

//==============================================================================
struct Engine::Impl
{
    float sr = 48000.f;
    float tgt[kNumParams] = {}, cur[kNumParams] = {};
    bool snap = true;
    Derived d {};

    std::array<Voice, kVoices> voices;
    int64_t order = 0;
    float bend = 0.f;
    bool sustain = false;

    std::vector<int> held;        // keys currently down, in order (mono mode: last-note priority)
    float lastPitch = -1.f;       // where the mono voice last was, so the next note can glide from it
    float mixBuf[kChunk] = {};
    float dcX = 0.f, dcY = 0.f;
    std::shared_ptr<Wavetable> builtinWt = makeBuiltinWavetable();
    std::atomic<const Wavetable*> userWt { nullptr };
    Reverb rvb;
    Biquad lowL, lowR, highL, highR;
    float lastBass = 1e9f, lastTreb = 1e9f;

    Impl()
    {
        for (int i = 0; i < kNumParams; ++i) tgt[i] = cur[i] = kParams[i].def;
    }

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        rvb.prepare (sr);
        reset();
    }

    void reset()
    {
        for (auto& v : voices) { v.active = false; v.vstage = 0; }
        held.clear(); lastPitch = -1.f;
        dcX = dcY = 0.f;
        rvb.reset();
        for (auto* b : { &lowL, &lowR, &highL, &highR }) b->reset();
        lastBass = lastTreb = 1e9f;
        snap = true;
    }

    bool monoMode() const { return tgt[P::mono] >= 0.5f; }

    // Monophonic: one voice. Playing a key while another is held slides to the new pitch (legato, envelopes keep
    // running); otherwise a new note starts, sliding from wherever the last one was.
    void monoNoteOn (int note, float v)
    {
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        auto& vo = voices[0];
        const bool legato = ! held.empty() && vo.active && vo.keyDown;
        held.push_back (note);
        for (size_t i = 1; i < voices.size(); ++i) if (voices[i].active) voices[i].release();

        if (legato) { vo.note = note; vo.vel = clampf (v, 0.f, 1.f); return; }

        const Voice old = vo;
        ++order;
        vo.start (note, clampf (v, 0.f, 1.f), order, (uint32_t) order);
        vo.glideNote = lastPitch >= 0.f ? lastPitch : (float) note;
        if (old.active)                       // taking over a note that is still ringing: carry on from where it was (no click)
        {
            vo.vlvl = old.vlvl * old.fadeGain;
            vo.phA = old.phA; vo.phB = old.phB; vo.phSub = old.phSub;
            vo.f1 = old.f1; vo.f2 = old.f2;
        }
    }

    void monoNoteOff (int note)
    {
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        auto& vo = voices[0];
        if (! vo.active) return;
        if (! held.empty()) { vo.note = held.back(); return; }       // fall back to the key still held, gliding to it
        if (vo.keyDown)
        {
            if (sustain) { vo.keyDown = false; vo.sustained = true; }
            else vo.release();
        }
    }

    void noteOn (int note, float v)
    {
        if (monoMode()) { monoNoteOn (note, v); return; }
        held.clear();
        Voice* pick = nullptr;
        for (auto& vo : voices) if (vo.active && vo.note == note) { pick = &vo; break; }   // retrigger
        if (! pick) for (auto& vo : voices) if (! vo.active) { pick = &vo; break; }
        if (! pick)
        {
            // steal: oldest releasing voice, else oldest voice
            for (auto& vo : voices) if (vo.vstage == 3 && (! pick || vo.startOrder < pick->startOrder)) pick = &vo;
            if (! pick) { pick = &voices[0]; for (auto& vo : voices) if (vo.startOrder < pick->startOrder) pick = &vo; }
        }
        ++order;
        pick->start (note, clampf (v, 0.f, 1.f), order, (uint32_t) order);
    }

    void noteOff (int note)
    {
        if (monoMode()) { monoNoteOff (note); return; }
        for (auto& vo : voices)
            if (vo.active && vo.keyDown && vo.note == note)
            {
                if (sustain) { vo.keyDown = false; vo.sustained = true; }
                else vo.release();
            }
    }

    void setSustain (bool down)
    {
        sustain = down;
        if (! down) for (auto& vo : voices) if (vo.active && vo.sustained) vo.release();
    }

    void updateParams (int chunk)
    {
        const float co = 1.f - std::exp (-(float) chunk / (sr * 0.015f));
        for (int i = 0; i < kNumParams; ++i)
        {
            if (snap || (kParams[i].flags & fStep)) { cur[i] = tgt[i]; continue; }     // choices switch at once, they do not glide
            const float diff = tgt[i] - cur[i];
            const float range = kParams[i].hi - kParams[i].lo;
            cur[i] = std::fabs (diff) < 1e-4f * range ? tgt[i] : cur[i] + diff * co;
        }
        snap = false;
        d = makeDerived (cur);
        rvb.setParams (d);
        if (d.bass != lastBass)
        {
            lowL.setLowShelf (sr, 150.f, d.bass * 12.f);
            lowR.b0 = lowL.b0; lowR.b1 = lowL.b1; lowR.b2 = lowL.b2; lowR.a1 = lowL.a1; lowR.a2 = lowL.a2;
            lastBass = d.bass;
        }
        if (d.treb != lastTreb)
        {
            highL.setHighShelf (sr, 6000.f, d.treb * 12.f);
            highR.b0 = highL.b0; highR.b1 = highL.b1; highR.b2 = highL.b2; highR.a1 = highL.a1; highR.a2 = highL.a2;
            lastTreb = d.treb;
        }
    }

    void render (float* L, float* R, int n)
    {
        int pos = 0;
        while (pos < n)
        {
            const int c = std::min (kChunk, n - pos);
            updateParams (c);
            std::memset (mixBuf, 0, sizeof (mixBuf));
            const Wavetable* wt = userWt.load (std::memory_order_acquire); if (! wt) wt = builtinWt.get();
            for (auto& v : voices) if (v.active) v.render (mixBuf, c, d, sr, bend, wt);
            if (monoMode() && voices[0].active) lastPitch = voices[0].glideNote;

            const float panA = (d.pan + 1.f) * kPi * 0.25f;
            const float gL = std::cos (panA) * 1.41421356f, gR = std::sin (panA) * 1.41421356f;
            const float pre = 1.f + 4.f * d.clip;
            const float post = 1.f / std::sqrt (pre);

            for (int i = 0; i < c; ++i)
            {
                // DC blocker
                const float x0 = mixBuf[i];
                dcY = x0 - dcX + 0.995f * dcY; dcX = x0;
                float x = saturateCurve (dcY, d.sat);

                float wl, wr;
                rvb.process (x, wl, wr);
                float l = x * rvb.mixCos + wl * rvb.mixSin;
                float r = x * rvb.mixCos + wr * rvb.mixSin;

                l = highL.process (lowL.process (l));
                r = highR.process (lowR.process (r));
                l *= gL; r *= gR;

                l = softClip (l * pre) * post;
                r = softClip (r * pre) * post;
                if (! std::isfinite (l)) l = 0.f;
                if (! std::isfinite (r)) r = 0.f;
                L[pos + i] = l;
                R[pos + i] = r;
            }
            pos += c;
        }
    }
};

//==============================================================================
Engine::Engine() : impl (new Impl()) {}
Engine::~Engine() = default;
void Engine::prepare (double sampleRate) { impl->prepare (sampleRate); }
void Engine::reset() { impl->reset(); }
void Engine::setParams (const float* v) { std::memcpy (impl->tgt, v, sizeof (float) * kNumParams); }
void Engine::noteOn (int note, float vel) { impl->noteOn (note, vel); }
void Engine::noteOff (int note) { impl->noteOff (note); }
void Engine::allNotesOff() { for (auto& v : impl->voices) if (v.active) v.release(); impl->sustain = false; impl->held.clear(); }
void Engine::setUserWavetable (const Wavetable* table) { impl->userWt.store (table, std::memory_order_release); }
void Engine::setPitchBend (float semitones) { impl->bend = semitones; }
void Engine::setSustain (bool down) { impl->setSustain (down); }
void Engine::render (float* L, float* R, int n) { impl->render (L, R, n); }

} // namespace seed
