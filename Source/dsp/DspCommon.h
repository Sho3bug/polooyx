#pragma once
// POLOOYX — shared real-time DSP primitives.
// Everything here is allocation-free after prepare(); safe to call on the audio thread.

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace plx
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float clamp01 (float x) noexcept   { return std::min (1.0f, std::max (0.0f, x)); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Smooth, bounded saturation used all over the chain.
inline float fastTanh (float x) noexcept
{
    if (x > 4.97f)  return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    const float a  = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float b  = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return a / b;
}

// ---------------------------------------------------------------- random
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) noexcept { s = v ? v : 0x9E3779B9u; }
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float bi() noexcept  { return uni() * 2.0f - 1.0f; }                           // [-1,1)
};

// ---------------------------------------------------------------- smoothing
struct OnePole
{
    float z = 0.0f, a = 0.0f;
    void setTime (float seconds, float sr) noexcept
    {
        a = seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (seconds * sr));
    }
    void reset (float v = 0.0f) noexcept { z = v; }
    float process (float x) noexcept { z = x + a * (z - x); return z; }
};

// Linear per-sample ramp for control values updated every sub-block.
struct Ramp
{
    float cur = 0.0f, target = 0.0f, step = 0.0f;
    int   left = 0;
    void reset (float v) noexcept { cur = target = v; step = 0.0f; left = 0; }
    void set (float t, int samples) noexcept
    {
        target = t;
        if (samples <= 0) { cur = t; left = 0; step = 0.0f; return; }
        step = (t - cur) / (float) samples;
        left = samples;
    }
    float next() noexcept
    {
        if (left > 0) { cur += step; if (--left == 0) cur = target; }
        return cur;
    }
};

// Linear fader with a fixed slew time — used for click-free module enable/bypass.
struct Fader
{
    float cur = 0.0f, target = 0.0f, step = 0.001f;
    void prepare (float sr, float seconds, float v) noexcept { step = 1.0f / std::max (1.0f, seconds * sr); cur = target = v; }
    void setTarget (float t) noexcept { target = t; }
    bool idle() const noexcept { return cur == target; }
    float next() noexcept
    {
        if (cur < target) cur = std::min (target, cur + step);
        else if (cur > target) cur = std::max (target, cur - step);
        return cur;
    }
};

// ---------------------------------------------------------------- biquad (RBJ)
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setNorm (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        const double inv = 1.0 / A0;
        b0 = (float) (B0 * inv); b1 = (float) (B1 * inv); b2 = (float) (B2 * inv);
        a1 = (float) (A1 * inv); a2 = (float) (A2 * inv);
    }

    static double w0 (float f, float sr) { return 2.0 * 3.14159265358979 * std::min ((double) f, 0.49 * sr) / sr; }

    void lowpass (float f, float q, float sr) noexcept
    {
        const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNorm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (float f, float q, float sr) noexcept
    {
        const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNorm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandpass (float f, float q, float sr) noexcept
    {
        const double w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNorm (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak (float f, float q, float gainDb, float sr) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = w0 (f, sr), c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNorm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (float f, float gainDb, float sr) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = w0 (f, sr), c = std::cos (w);
        const double al = std::sin (w) / 2.0 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        setNorm (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                 (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    void highShelf (float f, float gainDb, float sr) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = w0 (f, sr), c = std::cos (w);
        const double al = std::sin (w) / 2.0 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        setNorm (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                 (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
};

// ---------------------------------------------------------------- one-pole LP/HP (cheap tone shaping)
struct OnePoleLP
{
    float z = 0, a = 0;
    void set (float f, float sr) noexcept { a = std::exp (-kTwoPi * std::min (f, 0.49f * sr) / sr); }
    float process (float x) noexcept { z = x + a * (z - x); return z; }
    void reset() noexcept { z = 0; }
};

// ---------------------------------------------------------------- delay line (power-of-two ring, fractional read)
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // delay in samples measured from the most recently pushed sample (0 = newest)
    float read (float d) const noexcept
    {
        const float pos = (float) w - 1.0f - d;
        const int   i   = (int) std::floor (pos);
        const float fr  = pos - (float) i;
        // cubic (Catmull-Rom) interpolation
        const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)];
        const float y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fr + c2) * fr + c1) * fr + y1;
    }
    float readInt (int d) const noexcept { return buf[(size_t) ((w - 1 - d) & mask)]; }
};

// ---------------------------------------------------------------- envelope follower
struct EnvFollower
{
    float env = 0, att = 0, rel = 0;
    void set (float attackMs, float releaseMs, float sr) noexcept
    {
        att = std::exp (-1.0f / (std::max (0.01f, attackMs) * 0.001f * sr));
        rel = std::exp (-1.0f / (std::max (0.1f, releaseMs) * 0.001f * sr));
    }
    float process (float x) noexcept
    {
        const float a = std::abs (x);
        env = a > env ? a + att * (env - a) : a + rel * (env - a);
        return env;
    }
    void reset() noexcept { env = 0; }
};

// ---------------------------------------------------------------- smooth random LFO (for "alive" movement)
struct DriftLfo
{
    float phase = 0, a = 0, b = 0;
    Rng rng;
    float next (float rateHz, float sr) noexcept
    {
        phase += rateHz / sr;
        if (phase >= 1.0f) { phase -= 1.0f; a = b; b = rng.bi(); }
        const float t = phase * phase * (3.0f - 2.0f * phase);   // smoothstep between random points
        return a + (b - a) * t;
    }
};
} // namespace plx
