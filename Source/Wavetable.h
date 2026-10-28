#pragma once
// Wavetables for the "Wavetable" sound source, plus the importer that turns a sound file into one.
// Plain C++ (no JUCE) so it can be unit-tested offline.
//
//   * A table is N frames of 2048 samples, one cycle each (like Serum). The A Form knob scans through the frames.
//   * Every frame is stored band-limited at 9 levels (512 ... 2 harmonics); the voice picks the level for the note
//     it plays, so high notes never alias.
//   * importWavetable() accepts
//       - a wavetable file (length a multiple of 2048/1024/512/256: used as it is), or
//       - any sampled sound: its pitch is detected (YIN), then it is cut into pitch-synchronous cycles - every cycle
//         is resampled to exactly one period - so the table plays IN TUNE on every key. That is the same idea as
//         Correct Tune, applied to a sample: whatever the recording's pitch was, it comes out on the note you play.
//
// A table also reports what it found (root note, how far from 12-TET it was, how many frames) for the UI.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace seed
{

constexpr int kWtSize   = 2048;
constexpr int kWtLevels = 9;
constexpr int kWtMaxFrames = 64;

inline int wtCap (int level) { return 512 >> level; }

struct Wavetable
{
    int frames = 0;
    std::string name = "Built-in";
    std::vector<float> base;                          // frames * kWtSize, normalised, DC-free
    std::vector<float> mip[kWtLevels];                // frames * (kWtSize + 1), band-limited, [kWtSize] repeats [0]

    // what the importer found (for the display and for presets)
    bool userMade = false, pitched = false, tableFile = false;
    float rootHz = 0.f, centsOff = 0.f;
    int rootNote = -1;                                // MIDI note of the detected pitch, -1 = none

    // ---- FFT (radix 2) -------------------------------------------------------------------------------------------
    static void fft (std::vector<std::complex<double>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * 3.14159265358979323846 / (double) len * (inverse ? 1 : -1);
            const std::complex<double> wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v; a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse) for (auto& x : a) x /= (double) n;
    }

    // Build the band-limited levels from `base`.
    void build()
    {
        for (auto& m : mip) m.assign ((size_t) frames * (kWtSize + 1), 0.f);
        std::vector<std::complex<double>> spec (kWtSize), tmp (kWtSize);
        for (int f = 0; f < frames; ++f)
        {
            for (int i = 0; i < kWtSize; ++i) spec[(size_t) i] = { (double) base[(size_t) f * kWtSize + (size_t) i], 0.0 };
            fft (spec, false);
            for (int l = 0; l < kWtLevels; ++l)
            {
                const int cap = wtCap (l);
                for (int k = 0; k < kWtSize; ++k)
                {
                    const int h = k <= kWtSize / 2 ? k : kWtSize - k;
                    tmp[(size_t) k] = (h == 0 || h > cap) ? std::complex<double> (0, 0) : spec[(size_t) k];
                }
                fft (tmp, true);
                float* out = &mip[l][(size_t) f * (kWtSize + 1)];
                for (int i = 0; i < kWtSize; ++i) out[i] = (float) tmp[(size_t) i].real();
                out[kWtSize] = out[0];
            }
        }
    }

    // pos 0..1 scans the frames; ph 0..1 is the phase; level from the note's phase increment.
    static int levelFor (float dt)
    {
        const float hmax = 0.5f / std::max (dt, 1e-5f);
        int l = 0;
        while (l < kWtLevels - 1 && (float) wtCap (l) > hmax) ++l;
        return l;
    }

    float read (float pos, float ph, int level) const
    {
        const float fp = std::min (std::max (pos, 0.f), 1.f) * (float) (frames - 1);
        const int f0 = (int) fp, f1 = std::min (f0 + 1, frames - 1);
        const float ff = fp - (float) f0;
        const float* a = &mip[level][(size_t) f0 * (kWtSize + 1)];
        const float* b = &mip[level][(size_t) f1 * (kWtSize + 1)];
        const float x = ph * (float) kWtSize;
        const int i = std::min (kWtSize - 1, (int) x);
        const float fr = x - (float) i;
        const float va = a[i] + (a[i + 1] - a[i]) * fr;
        if (f1 == f0 || ff <= 0.f) return va;
        const float vb = b[i] + (b[i + 1] - b[i]) * fr;
        return va + (vb - va) * ff;
    }

    // Normalise every frame (DC removed, equal loudness) and the table (peak 0.95). Call before build().
    void normalise (bool equalLoudness)
    {
        std::vector<float> rms ((size_t) frames, 0.f);
        for (int f = 0; f < frames; ++f)
        {
            float* x = &base[(size_t) f * kWtSize];
            double m = 0; for (int i = 0; i < kWtSize; ++i) m += x[i]; m /= kWtSize;
            double e = 0; for (int i = 0; i < kWtSize; ++i) { x[i] -= (float) m; e += (double) x[i] * x[i]; }
            rms[(size_t) f] = (float) std::sqrt (e / kWtSize);
        }
        if (equalLoudness)
        {
            std::vector<float> r = rms; std::sort (r.begin(), r.end());
            const float target = r[r.size() / 2];
            for (int f = 0; f < frames; ++f)
                if (rms[(size_t) f] > 1e-6f && target > 1e-6f)
                {
                    const float g = std::min (8.f, target / rms[(size_t) f]);
                    for (int i = 0; i < kWtSize; ++i) base[(size_t) f * kWtSize + (size_t) i] *= g;
                }
        }
        float pk = 0.f; for (float v : base) pk = std::max (pk, std::fabs (v));
        if (pk > 1e-9f) for (auto& v : base) v *= 0.95f / pk;
    }
};

//==============================================================================
// The eight built-in frames (the table the Wavetable source uses until you load your own).
inline std::shared_ptr<Wavetable> makeBuiltinWavetable()
{
    auto t = std::make_shared<Wavetable>();
    t->frames = 8;
    t->name = "Built-in";
    t->base.assign ((size_t) 8 * kWtSize, 0.f);
    static const char* kNames[8] = { "sine", "soft", "organ", "hollow", "bright", "vowel", "glass", "metal" };
    (void) kNames;
    std::vector<float> sinT (kWtSize);
    for (int i = 0; i < kWtSize; ++i) sinT[(size_t) i] = (float) std::sin (6.28318530717958647692 * (double) i / kWtSize);

    for (int f = 0; f < 8; ++f)
    {
        float* x = &t->base[(size_t) f * kWtSize];
        for (int h = 1; h <= 255; ++h)
        {
            float a = 0.f, ph = 0.f;
            switch (f)
            {
                case 0: a = h == 1 ? 1.f : 0.f; break;
                case 1: a = 1.f / (float) (h * h); break;
                case 2: { static const int dh[6] = { 1, 2, 3, 4, 6, 8 }; static const float da[6] = { 1.f, .8f, .5f, .4f, .3f, .2f };
                          for (int k = 0; k < 6; ++k) if (dh[k] == h) a = da[k]; break; }
                case 3: a = (h & 1) ? 1.f / (float) h : 0.f; break;
                case 4: a = 1.f / (float) h; break;
                case 5: { const float c1 = 3.f, c2 = 5.5f, c3 = 12.f, hh = (float) h;
                          a = (1.f / hh) * (0.25f + 1.8f * std::exp (-0.5f * (hh - c1) * (hh - c1) / 1.2f) + 1.2f * std::exp (-0.5f * (hh - c2) * (hh - c2) / 1.6f)
                                                  + 0.7f * std::exp (-0.5f * (hh - c3) * (hh - c3) / 5.f)); break; }
                case 6: a = (h % 3 != 0 && h <= 40) ? 1.f / std::pow ((float) h, 0.7f) : 0.f; ph = 0.37f * (float) (h * h); break;
                default: a = (h % 3 == 0 && h <= 36) ? 1.f / std::pow ((float) h, 0.45f) : 0.f; break;
            }
            if (a == 0.f) continue;
            const int off = (int) std::lround (ph / 6.28318530717958647692f * kWtSize);
            for (int i = 0; i < kWtSize; ++i) x[i] += a * sinT[(size_t) (((h * i + off) % kWtSize + kWtSize) % kWtSize)];
        }
    }
    t->normalise (false);
    t->build();
    return t;
}

//==============================================================================
// ---- pitch detection (YIN) -----------------------------------------------------------------------------------------
// Returns the fundamental in Hz (0 = none) and a confidence 0..1 (1 - the normalised difference at the chosen lag).
inline float detectPitchYin (const float* x, int n, double sr, float& confidence, float minHz = 35.f, float maxHz = 2000.f)
{
    confidence = 0.f;
    const int tauMax = std::min (n / 2 - 1, (int) (sr / minHz));
    const int tauMin = std::max (2, (int) (sr / maxHz));
    if (tauMax <= tauMin + 2) return 0.f;
    std::vector<float> d ((size_t) tauMax + 1, 0.f), cm ((size_t) tauMax + 1, 1.f);
    const int w = n - tauMax;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double s = 0;
        for (int i = 0; i < w; ++i) { const double diff = (double) x[i] - (double) x[i + tau]; s += diff * diff; }
        d[(size_t) tau] = (float) s;
    }
    double run = 0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        run += d[(size_t) tau];
        cm[(size_t) tau] = run > 0 ? (float) (d[(size_t) tau] * tau / run) : 1.f;
    }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (cm[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
            best = tau; break;
        }
    if (best < 0)                                   // no dip under the threshold: take the global minimum, low confidence
    {
        float m = 1e9f;
        for (int tau = tauMin; tau < tauMax; ++tau) if (cm[(size_t) tau] < m) { m = cm[(size_t) tau]; best = tau; }
        if (best < 0 || m > 0.4f) return 0.f;
    }
    // parabolic refinement
    float tauF = (float) best;
    if (best > 1 && best < tauMax)
    {
        const float a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
        const float den = a - 2.f * b + c;
        if (std::fabs (den) > 1e-9f) tauF += 0.5f * (a - c) / den;
    }
    confidence = std::max (0.f, 1.f - cm[(size_t) best]);
    return (float) (sr / tauF);
}

inline float hermiteAt (const std::vector<float>& x, double pos)
{
    const int n = (int) x.size();
    const int i = (int) std::floor (pos);
    const float f = (float) (pos - i);
    auto at = [&] (int k) { return x[(size_t) std::min (n - 1, std::max (0, k))]; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

struct ImportResult
{
    std::shared_ptr<Wavetable> table;
    std::string message;          // one line for the user
};

// mode: 0 = automatic, 1 = wavetable file (frames of 2048, 1024, 512 or 256), 2 = sampled sound (detect pitch, tune)
inline ImportResult importWavetable (const std::vector<float>& input, double sr, const std::string& name, int mode = 0, int frameHint = 0)
{
    ImportResult res;
    if (input.size() < 64 || sr < 8000.0) { res.message = "That file is too short to use."; return res; }

    auto t = std::make_shared<Wavetable>();
    t->name = name; t->userMade = true;

    // ------------------------------------------------------------------ a wavetable file: frames used as they are
    int frameLen = 0;
    if (mode != 2 && frameHint >= 64 && frameHint <= 8192 && (int) input.size() % frameHint == 0 && (int) input.size() / frameHint >= 1)
        frameLen = frameHint;             // the file says so (Serum's clm chunk)
    if (mode != 2 && frameLen == 0)
        for (int s : { 2048, 1024, 512, 256 })
            if ((int) input.size() % s == 0 && (int) input.size() / s >= 1 && (int) input.size() / s <= 512 && (mode == 1 || (int) input.size() / s >= 2 || s == 2048)) { frameLen = s; break; }
    if (mode == 1 && frameLen == 0) frameLen = 2048;

    if (frameLen > 0 && ((int) input.size() >= frameLen))
    {
        const int total = (int) input.size() / frameLen;
        const int frames = std::min (kWtMaxFrames, total);
        t->frames = frames;
        t->base.assign ((size_t) frames * kWtSize, 0.f);
        for (int f = 0; f < frames; ++f)
        {
            const int src = frames == total ? f : (int) std::lround ((double) f * (total - 1) / std::max (1, frames - 1));
            const float* in = &input[(size_t) src * (size_t) frameLen];
            std::vector<float> one (in, in + frameLen);
            for (int i = 0; i < kWtSize; ++i)
                t->base[(size_t) f * kWtSize + (size_t) i] = frameLen == kWtSize ? one[(size_t) i] : hermiteAt (one, (double) i * frameLen / kWtSize);
        }
        t->tableFile = true;
        t->normalise (false);
        t->build();
        res.table = t;
        res.message = std::to_string (frames) + " frames, used as they are (tuned: one cycle per frame)";
        return res;
    }

    // ------------------------------------------------------------------ a sampled sound: find the pitch, cut into cycles
    std::vector<float> x (input.begin(), input.begin() + (long) std::min<size_t> (input.size(), (size_t) (sr * 12.0)));
    double mean = 0; for (float v : x) mean += v; mean /= (double) x.size();
    float peak = 0.f; for (auto& v : x) { v -= (float) mean; peak = std::max (peak, std::fabs (v)); }
    if (peak < 1e-5f) { res.message = "That file is silent."; return res; }
    for (auto& v : x) v /= peak;

    // trim silence at both ends (threshold 3 %), keep the loud part
    int a = 0, b = (int) x.size() - 1;
    while (a < b && std::fabs (x[(size_t) a]) < 0.03f) ++a;
    while (b > a && std::fabs (x[(size_t) b]) < 0.03f) --b;
    const int len = b - a + 1;
    if (len < (int) (sr * 0.02)) { res.message = "That sound is too short to find a pitch."; return res; }

    // pitch: YIN on up to 9 windows spread over the sound; the median of the confident ones
    const int win = (int) std::max (4096.0, sr / 35.0 * 2.2);
    std::vector<float> f0s;
    const int nWin = len > win * 2 ? 9 : 1;
    for (int k = 0; k < nWin; ++k)
    {
        const int start = a + (nWin == 1 ? 0 : (int) ((double) (len - win) * k / (nWin - 1)));
        const int n = std::min (win, b - start + 1);
        if (n < 512) continue;
        float conf = 0.f;
        const float f = detectPitchYin (&x[(size_t) start], n, sr, conf);
        if (f > 0.f && conf > 0.8f) f0s.push_back (f);
    }
    std::sort (f0s.begin(), f0s.end());
    const bool pitched = f0s.size() >= (nWin == 1 ? 1u : 2u);
    double period = 0.0;
    if (pitched)
    {
        const float f0 = f0s[f0s.size() / 2];
        period = sr / f0;
        t->pitched = true; t->rootHz = f0;
        const double midi = 69.0 + 12.0 * std::log2 ((double) f0 / 440.0);
        t->rootNote = (int) std::lround (midi);
        t->centsOff = (float) ((midi - t->rootNote) * 100.0);
    }

    const int nFrames = std::max (1, std::min (kWtMaxFrames, pitched ? (int) (len / (period * 3.0)) : len / kWtSize));
    t->frames = nFrames;
    t->base.assign ((size_t) nFrames * kWtSize, 0.f);

    auto localPeriod = [&] (int centre, double p0) -> double
    {
        // refine the period around p0 (+-4 %) by the normalised autocorrelation of 3 periods
        const int span = (int) (p0 * 3.0);
        const int s0 = std::max (0, std::min ((int) x.size() - span - (int) (p0 * 1.1) - 4, centre - span / 2));
        if (s0 < 0) return p0;
        double best = p0, bestScore = -1e30;
        for (int step = -16; step <= 16; ++step)
        {
            const double p = p0 * (1.0 + 0.04 * step / 16.0);
            double num = 0, e1 = 0, e2 = 0;
            for (int i = 0; i < span; i += 2)
            {
                const double u = x[(size_t) (s0 + i)], v = hermiteAt (x, s0 + i + p);
                num += u * v; e1 += u * u; e2 += v * v;
            }
            const double score = num / std::sqrt (e1 * e2 + 1e-12);
            if (score > bestScore) { bestScore = score; best = p; }
        }
        return best;
    };

    for (int f = 0; f < nFrames; ++f)
    {
        float* out = &t->base[(size_t) f * kWtSize];
        if (pitched)
        {
            const double usable = (double) len - period * 4.0;
            const int centre = a + (int) (usable > 0 ? usable * (nFrames == 1 ? 0.5 : (double) f / (nFrames - 1)) : 0.0) + (int) (period * 1.5);
            const double p = localPeriod (centre, period);
            // start at the next rising zero crossing so neighbouring frames line up in phase
            int s = std::max (a, centre);
            const int limit = std::min ((int) x.size() - (int) (p * 1.2) - 4, s + (int) p);
            while (s < limit && ! (x[(size_t) s] <= 0.f && x[(size_t) s + 1] > 0.f)) ++s;
            for (int i = 0; i < kWtSize; ++i) out[i] = hermiteAt (x, (double) s + (double) i * p / kWtSize);
        }
        else
        {
            const int s = std::min (b - kWtSize, a + (nFrames == 1 ? 0 : (int) ((double) (len - kWtSize) * f / (nFrames - 1))));
            for (int i = 0; i < kWtSize; ++i)
            {
                const float fade = std::min (1.f, std::min ((float) i, (float) (kWtSize - 1 - i)) / 64.f);
                out[i] = x[(size_t) std::max (0, s + i)] * fade;
            }
        }
        // make the end meet the start (a perfect period already does; this removes any small step)
        const float step = out[kWtSize - 1] - out[0];
        for (int i = 0; i < kWtSize; ++i) out[i] -= step * (float) i / (float) kWtSize;
    }
    t->normalise (true);
    t->build();
    res.table = t;
    if (pitched)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = t->rootNote;
        char buf[160];
        std::snprintf (buf, sizeof buf, "Sampled sound: pitch %s%d %+.0f ct, tuned to the keys (%d frames)", names[((n % 12) + 12) % 12], n / 12 - 1, (double) t->centsOff, nFrames);
        res.message = buf;
    }
    else
        res.message = "Sampled sound without a clear pitch: loaded as " + std::to_string (nFrames) + " raw frames (not tunable)";
    return res;
}

//==============================================================================
// ---- WAV files ------------------------------------------------------------------------------------------------------
// PCM 8/16/24/32-bit and float 32/64-bit, any channel count (mixed to mono). Serum / Surge tables carry a "clm " chunk
// such as "<!>2048 10000000 wavetable": the number after <!> is the frame length.
struct WavData { std::vector<float> mono; double sampleRate = 0.0; int frameHint = 0; };

inline bool decodeWav (const uint8_t* d, size_t n, WavData& out, std::string& error)
{
    auto u16 = [&] (size_t o) { return (uint32_t) d[o] | ((uint32_t) d[o + 1] << 8); };
    auto u32 = [&] (size_t o) { return u16 (o) | (u16 (o + 2) << 16); };
    if (n < 44 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0) { error = "Not a WAV file."; return false; }

    int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0;
    size_t dataOff = 0, dataLen = 0;
    size_t pos = 12;
    while (pos + 8 <= n)
    {
        const size_t len = u32 (pos + 4), body = pos + 8;
        const size_t avail = std::min (len, n - body);
        if (std::memcmp (d + pos, "fmt ", 4) == 0 && avail >= 16)
        {
            fmt = (int) u16 (body); ch = (int) u16 (body + 2); rate = u32 (body + 4); bits = (int) u16 (body + 14);
            if (fmt == 0xFFFE && avail >= 26) fmt = (int) u16 (body + 24);        // WAVE_FORMAT_EXTENSIBLE: first two bytes of the sub-format GUID
        }
        else if (std::memcmp (d + pos, "data", 4) == 0) { dataOff = body; dataLen = avail; }
        else if (std::memcmp (d + pos, "clm ", 4) == 0)
        {
            std::string txt ((const char*) d + body, avail);
            const auto k = txt.find ("<!>");
            if (k != std::string::npos) out.frameHint = std::atoi (txt.c_str() + k + 3);
        }
        pos = body + len + (len & 1);
    }
    if (ch < 1 || ch > 16 || rate < 8000 || dataLen == 0) { error = "That WAV file has no usable audio."; return false; }
    const bool isFloat = fmt == 3;
    if (! ((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (isFloat && (bits == 32 || bits == 64)))) { error = "Unsupported WAV format (use PCM or float)."; return false; }

    const size_t bytes = (size_t) bits / 8, frames = dataLen / (bytes * (size_t) ch);
    out.mono.assign (frames, 0.f);
    out.sampleRate = (double) rate;
    for (size_t i = 0; i < frames; ++i)
    {
        double acc = 0;
        for (int c = 0; c < ch; ++c)
        {
            const uint8_t* p = d + dataOff + (i * (size_t) ch + (size_t) c) * bytes;
            double v;
            if (isFloat && bits == 32) { float f; std::memcpy (&f, p, 4); v = f; }
            else if (isFloat)          { double f; std::memcpy (&f, p, 8); v = f; }
            else if (bits == 8)        v = ((int) p[0] - 128) / 128.0;
            else if (bits == 16)       v = (int16_t) (p[0] | (p[1] << 8)) / 32768.0;
            else if (bits == 24)       { int32_t x = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24); v = (x >> 8) / 8388608.0; }
            else                       { int32_t x; std::memcpy (&x, p, 4); v = x / 2147483648.0; }
            acc += v;
        }
        out.mono[i] = (float) (acc / ch);
    }
    return true;
}

inline ImportResult importWavBytes (const uint8_t* d, size_t n, const std::string& name)
{
    WavData w; std::string err;
    if (! decodeWav (d, n, w, err)) { ImportResult r; r.message = err; return r; }
    return importWavetable (w.mono, w.sampleRate, name, 0, w.frameHint);
}

// ---- keeping a table inside presets and host sessions --------------------------------------------------------------
// "SDWT", version 1, frames, flags, root note, rootHz, centsOff, name, then the frames as 16-bit. Rebuilds the band-limited
// levels on load, so what is stored is small (64 frames = 256 kB).
inline std::vector<uint8_t> serializeWavetable (const Wavetable& t)
{
    std::vector<uint8_t> o;
    auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back ((uint8_t) (v >> (8 * i))); };
    auto putf = [&] (float f) { uint32_t v; std::memcpy (&v, &f, 4); put32 (v); };
    o.insert (o.end(), { 'S', 'D', 'W', 'T' });
    put32 (1); put32 ((uint32_t) t.frames);
    put32 ((t.pitched ? 1u : 0u) | (t.tableFile ? 2u : 0u));
    put32 ((uint32_t) (int32_t) t.rootNote); putf (t.rootHz); putf (t.centsOff);
    put32 ((uint32_t) t.name.size());
    o.insert (o.end(), t.name.begin(), t.name.end());
    for (float v : t.base)
    {
        const int q = (int) std::lround (std::max (-1.f, std::min (1.f, v)) * 32767.f);
        o.push_back ((uint8_t) (q & 255)); o.push_back ((uint8_t) ((q >> 8) & 255));
    }
    return o;
}

inline std::shared_ptr<Wavetable> deserializeWavetable (const uint8_t* d, size_t n)
{
    auto u32 = [&] (size_t o) { return (uint32_t) d[o] | ((uint32_t) d[o + 1] << 8) | ((uint32_t) d[o + 2] << 16) | ((uint32_t) d[o + 3] << 24); };
    auto f32 = [&] (size_t o) { const uint32_t v = u32 (o); float f; std::memcpy (&f, &v, 4); return f; };
    if (n < 32 || std::memcmp (d, "SDWT", 4) != 0 || u32 (4) != 1) return nullptr;
    const uint32_t frames = u32 (8), flags = u32 (12), nameLen = u32 (28);
    if (frames < 1 || frames > (uint32_t) kWtMaxFrames || nameLen > 400) return nullptr;
    const size_t dataOff = 32 + nameLen;
    if (n < dataOff + (size_t) frames * kWtSize * 2) return nullptr;
    auto t = std::make_shared<Wavetable>();
    t->frames = (int) frames; t->userMade = true;
    t->pitched = (flags & 1u) != 0; t->tableFile = (flags & 2u) != 0;
    t->rootNote = (int) (int32_t) u32 (16); t->rootHz = f32 (20); t->centsOff = f32 (24);
    t->name.assign ((const char*) d + 32, nameLen);
    t->base.resize ((size_t) frames * kWtSize);
    for (size_t i = 0; i < t->base.size(); ++i)
        t->base[i] = (float) (int16_t) (d[dataOff + 2 * i] | (d[dataOff + 2 * i + 1] << 8)) / 32767.f;
    t->build();
    return t;
}

} // namespace seed
