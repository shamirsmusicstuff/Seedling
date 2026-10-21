#include "Engine.h"
#include <array>
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
        }

        void release() { keyDown = false; sustained = false; if (vstage != 0) vstage = 3; }

        void render (float* out, int n, const Derived& d, float sr, float bend)
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

                // --- oscillator A, phase-modulated by B ----------------------------
                const float fmDepth = clampf (d.fmAmt + d.fmMod * m, 0.f, 1.f);
                phA += dtA; if (phA >= 1.f) phA -= 1.f;
                float pm = phA + fmDepth * fmDepth * 1.5f * b;
                pm -= std::floor (pm);
                const float pw = d.aColor > 0.f ? colorWarp (pm, d.aColor) : pm;
                nA += 0.35f * (rng.next() - nA);
                const float a = morphShape (false, d.aForm + d.aMod * m, pw, dtA) + d.aNoise * 1.6f * nA;

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

    //==========================================================================
    // 8-line feedback delay network. rvb_atk = pre-delay plus a slow "swell" of the wet signal.
    struct Reverb
    {
        static constexpr int N = 8;
        std::vector<float> buf[N];
        int mask[N] = {};
        int wr = 0;
        float delayCur[N] = {}, delayTgt[N] = {}, lp[N] = {}, gain[N] = {}, lfoPh[N] = {}, lfoInc[N] = {};
        std::vector<float> pre; int preMask = 0, preWr = 0;
        float env = 0.f, gate = 1.f;
        float sr = 48000.f;
        float dampC = 0.5f, modDepth = 0.f, preSamples = 0.f, riseCo = 1.f, mixCos = 1.f, mixSin = 0.f;

        static constexpr int base[N] = { 1031, 1223, 1427, 1597, 1801, 2039, 2243, 2467 };

        static int pow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

        void prepare (float sampleRate)
        {
            sr = sampleRate;
            const float k = sr / 48000.f;
            for (int i = 0; i < N; ++i)
            {
                const int sz = pow2 ((int) (base[i] * k * 2.0f) + 64);
                buf[i].assign ((size_t) sz, 0.f);
                mask[i] = sz - 1;
                lfoInc[i] = (0.13f + 0.071f * (float) i) / sr;
                lfoPh[i] = (float) i / (float) N;
                delayCur[i] = delayTgt[i] = (float) base[i] * k;
                lp[i] = 0.f;
            }
            const int psz = pow2 ((int) (0.08f * sr) + 8);
            pre.assign ((size_t) psz, 0.f); preMask = psz - 1; preWr = 0;
            wr = 0; env = 0.f; gate = 1.f;
        }

        void reset()
        {
            for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f);
            std::fill (pre.begin(), pre.end(), 0.f);
            for (int i = 0; i < N; ++i) lp[i] = 0.f;
            env = 0.f; gate = 1.f;
        }

        void setParams (const Derived& d)
        {
            const float k = sr / 48000.f;
            const float sizeScale = 0.4f + 1.4f * d.rvbSize;
            for (int i = 0; i < N; ++i)
            {
                delayTgt[i] = (float) base[i] * k * sizeScale;
                gain[i] = std::pow (10.f, -3.f * delayTgt[i] / (d.rvbRt60 * sr));
            }
            dampC = 0.02f + 0.93f * d.rvbDamp;
            modDepth = d.rvbChor * 14.f * k;
            preSamples = d.rvbAtk * 0.06f * sr;
            const float tau = d.rvbAtk * 0.5f * sr;
            riseCo = tau < 1.f ? 1.f : 1.f - std::exp (-1.f / tau);
            mixCos = std::cos (d.rvbMix * kPi * 0.5f);
            mixSin = std::sin (d.rvbMix * kPi * 0.5f);
        }

        void process (float in, float& outL, float& outR)
        {
            // pre-delay
            pre[(size_t) preWr] = in;
            const int pd = (int) preSamples;
            float x = pre[(size_t) ((preWr - pd) & preMask)];
            preWr = (preWr + 1) & preMask;

            // swell: the wet input fades in after silence
            env = std::max (std::fabs (in), env * 0.9995f);
            const float target = env > 0.002f ? 1.f : 0.f;
            gate += (target - gate) * (target > gate ? riseCo : 0.002f);
            x *= gate;

            float d[N];
            for (int i = 0; i < N; ++i)
            {
                delayCur[i] += (delayTgt[i] - delayCur[i]) * 0.001f;
                lfoPh[i] += lfoInc[i]; if (lfoPh[i] >= 1.f) lfoPh[i] -= 1.f;
                float dl = delayCur[i] + modDepth * std::sin (kTwoPi * lfoPh[i]);
                dl = std::max (2.f, std::min (dl, (float) mask[i] - 2.f));
                const float rp = (float) wr - dl;
                const float fl = std::floor (rp);
                const float fr = rp - fl;
                const int i0 = (int) fl;
                const float y = buf[i][(size_t) (i0 & mask[i])] * (1.f - fr)
                              + buf[i][(size_t) ((i0 + 1) & mask[i])] * fr;
                lp[i] = (1.f - dampC) * y + dampC * lp[i];
                d[i] = lp[i] * gain[i];
            }

            outL = 0.45f * (d[0] + d[2] - d[4] + d[6] + 0.5f * (d[1] - d[3]));
            outR = 0.45f * (d[1] + d[3] - d[5] + d[7] + 0.5f * (d[0] - d[2]));

            // Hadamard feedback matrix (orthogonal => lossless before the per-line gains)
            for (int h = 1; h < N; h <<= 1)
                for (int i = 0; i < N; i += h << 1)
                    for (int j = i; j < i + h; ++j)
                    {
                        const float a = d[j], b = d[j + h];
                        d[j] = a + b; d[j + h] = a - b;
                    }
            static constexpr float sgn[N] = { 1, -1, 1, 1, -1, 1, -1, -1 };
            for (int i = 0; i < N; ++i)
            {
                const float v = d[i] * 0.35355339f + x * sgn[i] * 0.35f;
                buf[i][(size_t) (wr & mask[i])] = std::isfinite (v) ? v : 0.f;
            }
            ++wr;
        }
    };
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
            if (snap) { cur[i] = tgt[i]; continue; }
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
            for (auto& v : voices) if (v.active) v.render (mixBuf, c, d, sr, bend);
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
void Engine::setPitchBend (float semitones) { impl->bend = semitones; }
void Engine::setSustain (bool down) { impl->setSustain (down); }
void Engine::render (float* L, float* R, int n) { impl->render (L, R, n); }

} // namespace seed
