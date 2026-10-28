// Wavetable import: a table file is used as it is; a sampled sound is pitch-detected and cut into exact cycles,
// so it comes out in tune on every key. Plain C++, no JUCE.
#include "Wavetable.h"
#include <cstdio>
#include <random>

using namespace seed;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

// a decaying, slightly bright tone at f Hz, with a little noise and vibrato-free pitch
static std::vector<float> tone (double f, double sr, double seconds, float noise = 0.f)
{
    std::mt19937 g (3); std::normal_distribution<float> n (0.f, 1.f);
    std::vector<float> x ((size_t) (sr * seconds));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / sr;
        double v = 0;
        for (int h = 1; h <= 8; ++h) v += std::sin (2 * 3.14159265358979 * f * h * t + 0.3 * h) / h;
        x[i] = (float) (v * std::exp (-1.5 * t)) + noise * n (g);
    }
    return x;
}

// energy at integer multiples of the table's own fundamental versus everywhere else: a perfect one-cycle frame has
// all of its energy on the integer bins
static double harmonicPurity (const float* frame)
{
    std::vector<std::complex<double>> s (kWtSize);
    for (int i = 0; i < kWtSize; ++i) s[(size_t) i] = frame[i];
    Wavetable::fft (s, false);
    double tot = 0, on = 0;
    for (int k = 1; k < kWtSize / 2; ++k) { const double e = std::norm (s[(size_t) k]); tot += e; if (k <= 16) on += e; }
    return on / std::max (1e-12, tot);   // almost everything in the lowest 16 integer harmonics for these tones
}

int main()
{
    // built-in table
    {
        auto t = makeBuiltinWavetable();
        CHECK (t->frames == 8, "built-in frames %d", t->frames);
        float pk = 0.f; for (float v : t->base) pk = std::max (pk, std::fabs (v));
        CHECK (pk > 0.9f && pk <= 0.96f, "built-in peak %.3f", pk);
        // band-limiting: the lowest level of a bright frame has less high-frequency energy than the top level
        auto hf = [&] (int level) { double e = 0; for (int i = 1; i < kWtSize; ++i) { const float d = t->mip[level][(size_t) 4 * (kWtSize + 1) + (size_t) i] - t->mip[level][(size_t) 4 * (kWtSize + 1) + (size_t) i - 1]; e += d * d; } return e; };
        CHECK (hf (8) < hf (0) * 0.2, "levels not band-limited (%.3g vs %.3g)", hf (8), hf (0));
        CHECK (Wavetable::levelFor (0.0005f) == 0 && Wavetable::levelFor (0.2f) == kWtLevels - 1, "level choice");
    }

    // sampled sounds at several pitches, detuned on purpose, at two sample rates
    struct Case { double f, sr; };
    for (Case c : { Case { 110.0, 44100.0 }, Case { 220.7, 44100.0 }, Case { 261.0, 48000.0 }, Case { 440.0, 48000.0 }, Case { 733.3, 44100.0 }, Case { 55.0, 48000.0 } })
    {
        auto r = importWavetable (tone (c.f, c.sr, 1.0, 0.002f), c.sr, "test");
        CHECK (r.table != nullptr, "import failed at %.1f Hz", c.f);
        if (! r.table) continue;
        const double cents = 1200.0 * std::log2 ((double) r.table->rootHz / c.f);
        std::printf ("sample %.1f Hz @%.0f: found %.2f Hz (%+.1f ct), %d frames, \"%s\"\n", c.f, c.sr, (double) r.table->rootHz, cents, r.table->frames, r.message.c_str());
        CHECK (r.table->pitched && std::fabs (cents) < 5.0, "pitch %.1f Hz found as %.2f (%+.1f ct)", c.f, (double) r.table->rootHz, cents);
        CHECK (r.table->frames >= 2, "too few frames %d", r.table->frames);
        // every frame is exactly one cycle: the frame is made of integer harmonics only
        double worst = 1.0;
        for (int f = 0; f < r.table->frames; ++f) worst = std::min (worst, harmonicPurity (&r.table->base[(size_t) f * kWtSize]));
        CHECK (worst > 0.97, "a frame is not one clean cycle (purity %.3f)", worst);
        // the first harmonic is where the fundamental ends up: it dominates frame 0
        std::vector<std::complex<double>> s (kWtSize);
        for (int i = 0; i < kWtSize; ++i) s[(size_t) i] = r.table->base[(size_t) i];
        Wavetable::fft (s, false);
        int arg = 1; double m = 0; for (int k = 1; k < 20; ++k) if (std::abs (s[(size_t) k]) > m) { m = std::abs (s[(size_t) k]); arg = k; }
        CHECK (arg == 1, "fundamental ends up in harmonic %d", arg);
    }

    // a wavetable file (4 frames of 2048) is used as it is
    {
        std::vector<float> x;
        for (int f = 0; f < 4; ++f) for (int i = 0; i < kWtSize; ++i)
            x.push_back ((float) (std::sin (6.28318530717958 * i / kWtSize) + (f / 3.0) * std::sin (6.28318530717958 * 3 * i / kWtSize)));
        auto r = importWavetable (x, 44100.0, "tab");
        CHECK (r.table && r.table->tableFile && r.table->frames == 4, "wavetable file not recognised (%s)", r.message.c_str());
        // scanning from frame 0 to 3 raises the third harmonic
        auto h3 = [&] (float pos) { double a = 0, b = 0; for (int i = 0; i < kWtSize; ++i) { const double ph = (double) i / kWtSize; const double v = r.table->read (pos, (float) ph, 0); a += v * std::sin (6.28318530717958 * 3 * ph); b += v * std::cos (6.28318530717958 * 3 * ph); } return std::hypot (a, b); };
        CHECK (h3 (1.f) > h3 (0.f) * 3.0, "scanning the table does not change it (%.3f vs %.3f)", h3 (0.f), h3 (1.f));
    }

    // unpitched noise: loads as raw frames, says so
    {
        std::mt19937 g (5); std::normal_distribution<float> n (0.f, 0.3f);
        std::vector<float> x (44100 + 777); for (auto& v : x) v = n (g);
        auto r = importWavetable (x, 44100.0, "noise");
        CHECK (r.table && ! r.table->pitched && r.table->frames >= 2, "noise should load as raw frames");
    }

    // rubbish in: refused politely
    CHECK (importWavetable (std::vector<float> (10, 0.f), 44100.0, "x").table == nullptr, "tiny file accepted");
    CHECK (importWavetable (std::vector<float> (50000, 0.f), 44100.0, "x", 2).table == nullptr, "silent file accepted");


    // ---- WAV files: PCM16 / float / 24-bit stereo, the clm chunk, and keeping a table in a preset ---------------------
    {
        auto wav = [] (const std::vector<float>& x, int bits, int ch, double sr, const char* clm)
        {
            std::vector<uint8_t> o;
            auto p32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back ((uint8_t) (v >> (8 * i))); };
            auto p16 = [&] (uint32_t v) { o.push_back ((uint8_t) v); o.push_back ((uint8_t) (v >> 8)); };
            const int bytes = bits / 8, fmt = bits == 32 ? 3 : 1;
            const size_t dataLen = x.size() * (size_t) ch * (size_t) bytes;
            const std::string c = clm ? clm : "";
            o.insert (o.end(), { 'R', 'I', 'F', 'F' }); p32 (0); o.insert (o.end(), { 'W', 'A', 'V', 'E' });
            o.insert (o.end(), { 'f', 'm', 't', ' ' }); p32 (16); p16 (fmt); p16 ((uint32_t) ch); p32 ((uint32_t) sr); p32 ((uint32_t) (sr * ch * bytes)); p16 ((uint32_t) (ch * bytes)); p16 ((uint32_t) bits);
            if (! c.empty()) { o.insert (o.end(), { 'c', 'l', 'm', ' ' }); p32 ((uint32_t) c.size()); o.insert (o.end(), c.begin(), c.end()); if (c.size() & 1) o.push_back (0); }
            o.insert (o.end(), { 'd', 'a', 't', 'a' }); p32 ((uint32_t) dataLen);
            for (float v : x) for (int k = 0; k < ch; ++k)
            {
                if (bits == 16) p16 ((uint32_t) (int16_t) std::lround (v * 32767.f));
                else if (bits == 24) { const int32_t q = (int32_t) std::lround (v * 8388607.f); o.push_back ((uint8_t) q); o.push_back ((uint8_t) (q >> 8)); o.push_back ((uint8_t) (q >> 16)); }
                else { uint32_t u; std::memcpy (&u, &v, 4); p32 (u); }
            }
            return o;
        };

        // a serum-style file: 6 frames of 2048 samples, frame k = the k-th harmonic, with a clm chunk
        std::vector<float> frames;
        for (int f = 0; f < 6; ++f) for (int i = 0; i < 2048; ++i) frames.push_back (0.8f * std::sin (6.2831853f * (float) (f + 1) * (float) i / 2048.f));
        auto a = wav (frames, 16, 1, 44100.0, "<!>2048 10000000 wavetable (test)");
        auto r = importWavBytes (a.data(), a.size(), "six.wav");
        CHECK (r.table && r.table->frames == 6 && r.table->tableFile, "wav with clm chunk: %s", r.message.c_str());

        // frames of 1024 announced by the clm chunk (not a multiple the guesser would pick first)
        std::vector<float> small; for (int f = 0; f < 5; ++f) for (int i = 0; i < 1024; ++i) small.push_back (0.7f * std::sin (6.2831853f * 2.f * (float) i / 1024.f));
        auto b = wav (small, 24, 2, 48000.0, "<!>1024 10000000");
        auto rb = importWavBytes (b.data(), b.size(), "five.wav");
        CHECK (rb.table && rb.table->frames == 5, "1024-sample frames: %s", rb.message.c_str());

        // a sampled sound as a 32-bit float stereo file: pitch found, tuned
        auto smp = tone (233.0, 44100.0, 1.2);
        auto c = wav (smp, 32, 2, 44100.0, nullptr);
        auto rc = importWavBytes (c.data(), c.size(), "sample.wav");
        CHECK (rc.table && rc.table->pitched && std::fabs (rc.table->rootHz - 233.f) < 1.f, "float stereo sample: %s", rc.message.c_str());

        // broken input never crashes
        std::vector<uint8_t> junk (100, 7);
        CHECK (importWavBytes (junk.data(), junk.size(), "junk").table == nullptr, "junk accepted");
        auto trunc = a; trunc.resize (60);
        (void) importWavBytes (trunc.data(), trunc.size(), "cut");

        // stored in a preset and read back
        const auto bytes = serializeWavetable (*rc.table);
        auto back = deserializeWavetable (bytes.data(), bytes.size());
        CHECK (back && back->frames == rc.table->frames && back->pitched == rc.table->pitched && back->rootNote == rc.table->rootNote && back->name == "sample.wav", "serialize round trip");
        double err = 0; for (size_t i = 0; back && i < back->base.size(); ++i) err = std::max (err, (double) std::fabs (back->base[i] - rc.table->base[i]));
        CHECK (back && err < 1.0 / 16000.0, "round trip error %.5f", err);
        CHECK (deserializeWavetable (bytes.data(), 20) == nullptr, "short blob accepted");
        std::printf ("wav + preset storage: ok (%zu bytes for %d frames)\n", bytes.size(), rc.table->frames);
    }

    std::printf (failures == 0 ? "WAVETABLE OK\n" : "%d WAVETABLE CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
