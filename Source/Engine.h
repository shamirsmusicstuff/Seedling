#pragma once
// Seedling DSP engine. Plain C++ (no JUCE) so it can be unit-tested offline.

#include "Params.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace seed
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

//==============================================================================
// Knob (0..1) -> real-world value mappings. Shared by the engine and the UI figures.
namespace map
{
    inline float volAtk (float x) { return 0.002f * std::pow (2000.f, x); }   // 2 ms .. 4 s
    inline float volDcy (float x) { return 0.010f * std::pow (500.f,  x); }   // 10 ms .. 5 s
    inline float modAtk (float x) { return 0.001f * std::pow (2000.f, x); }   // 1 ms .. 2 s
    inline float modDcy (float x) { return 0.010f * std::pow (400.f,  x); }   // 10 ms .. 4 s
    inline float lfoHz  (float x) { return 0.1f   * std::pow (200.f,  x); }   // 0.1 .. 20 Hz
    inline float fltHz  (float x) { return 20.f   * std::pow (900.f,  x); }   // 20 Hz .. 18 kHz
    inline float fltQ   (float x) { return 0.5f   * std::pow (24.f,   x); }   // 0.5 .. 12
    inline float rvbRt60(float x) { return 0.2f   * std::pow (60.f,   x); }   // 0.2 s .. 12 s
}

// Everything the audio code needs, derived from the raw knob values.
struct Derived
{
    float envScale, envKf, envLoop, atkMul, dcyMul;
    float volAtk, volDcy, volSus, volRel, fadeTau;        // seconds (fadeTau 0 = off)
    float modAtk, modDcy, modShape, modVel;
    float lfoHz, vibSemis, tremDepth, lfoDelay;
    float aForm, aNoise, aMod, aColor, aSemi;
    float fmMod, fmAmt, mixMod, mix;
    float bForm, bNoise, bMod, sub, bSemi, bHoldHz;       // bHoldHz 0 = sample&hold off
    float fltType, fltK, fltHz, fltKf, fltMod, fltSep;     // fltK = 1/Q
    float sat, rvbMix, rvbAtk, rvbRt60, rvbDamp, rvbChor, rvbSize;
    float bass, treb, pan, clip;
    float glideSec, mono;                                 // glideSec 0 = off; mono >= 0.5 = monophonic
};

Derived makeDerived (const float* p);

//==============================================================================
// Oscillator shapes. Aligned so that every shape starts at 0 and rises, so that
// morphing between them never cancels. dt = phase increment (for PolyBLEP).
inline float polyBlep (float t, float dt)
{
    if (t < dt)         { t /= dt;          return t + t - t * t - 1.f; }
    if (t > 1.f - dt)   { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

inline float sawShape (float ph, float dt)
{
    float q = ph + 0.5f; q -= std::floor (q);
    return (2.f * q - 1.f) - polyBlep (q, dt);
}

inline float triShape (float ph)
{
    return ph < 0.25f ? 4.f * ph : (ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f);
}

inline float pulseShape (float ph, float w, float dt)
{
    float v = ph < w ? 1.f : -1.f;
    v += polyBlep (ph, dt);
    float q = ph - w; q -= std::floor (q);
    v -= polyBlep (q, dt);
    v -= (2.f * w - 1.f);                         // remove DC
    return v / std::max (2.f - 2.f * w, 2.f * w); // normalise peak
}

// Osc A: sine -> triangle -> saw -> square
inline float shapeA (int i, float ph, float dt)
{
    switch (i)
    {
        case 0:  return std::sin (kTwoPi * ph);
        case 1:  return 1.1f * triShape (ph);
        case 2:  return sawShape (ph, dt);
        default: return 0.7f * pulseShape (ph, 0.5f, dt);
    }
}

// Osc B: narrow pulse -> square -> saw -> triangle
inline float shapeB (int i, float ph, float dt)
{
    switch (i)
    {
        case 0:  return 0.8f * pulseShape (ph, 0.06f, dt);
        case 1:  return 0.7f * pulseShape (ph, 0.5f, dt);
        case 2:  return sawShape (ph, dt);
        default: return 1.1f * triShape (ph);
    }
}

inline float morphShape (bool isB, float form, float ph, float dt)
{
    const float p = clampf (form, 0.f, 1.f) * 3.f;
    const int i = std::min (2, (int) p);
    const float f = p - (float) i;
    const float a = isB ? shapeB (i, ph, dt) : shapeA (i, ph, dt);
    if (f <= 0.f) return a;
    const float b = isB ? shapeB (i + 1, ph, dt) : shapeA (i + 1, ph, dt);
    return a + (b - a) * f;
}

// "color": phase warp. 0 = untouched.
inline float colorWarp (float ph, float color)
{
    const float c = 0.5f - 0.47f * color;
    return ph < c ? 0.5f * ph / c : 0.5f + 0.5f * (ph - c) / (1.f - c);
}

// Saturation transfer curve (used by the engine and the UI figure).
inline float saturateCurve (float x, float amount)
{
    if (amount <= 0.f) return x;
    const float g = 1.f + 11.f * amount;
    const float wet = std::tanh (g * x) / std::sqrt (g);
    const float blend = std::min (1.f, amount * 8.f);
    return x + blend * (wet - x);
}

inline float softClip (float x)
{
    const float ax = std::fabs (x);
    if (ax <= 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((ax - 0.8f) / 0.2f);
    return x < 0.f ? -y : y;
}

//==============================================================================
struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.f; }
    void setLowShelf  (float sr, float freq, float gainDb);
    void setHighShelf (float sr, float freq, float gainDb);
    float magnitude (float freq, float sr) const;
};

//==============================================================================
class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate);
    void reset();

    void setParams (const float* values);   // kNumParams real-world values
    void noteOn (int note, float velocity01);
    void noteOff (int note);
    void allNotesOff();
    void setPitchBend (float semitones);
    void setSustain (bool down);

    // Renders n samples (overwrites L and R).
    void render (float* L, float* R, int n);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace seed
