#pragma once
// POLOOYX — space & stereo: modulated FDN reverb, tempo-synced delay, micro-pitch doubler,
// auto-pan movement, shadow layer.

#include "DspCommon.h"
#include <array>

namespace plx
{
// ============================================================ allpass diffuser
struct Allpass
{
    DelayLine d; int len = 100; float g = 0.6f;
    void prepare (int maxLen) { d.prepare (maxLen + 2); }
    float process (float x) noexcept
    {
        const float z = d.readInt (len - 1);
        const float v = x + g * z;
        d.push (v);
        return z - g * v;
    }
};

// ============================================================ 8-line modulated FDN reverb
class FdnReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (int i = 0; i < N; ++i) lines[(size_t) i].prepare ((int) (kLens[i] * 0.001f * 1.8f * sr) + 64);
        const float apl[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
        for (int i = 0; i < 4; ++i) { diff[(size_t) i].prepare ((int) (apl[i] * 0.001f * sr * 1.6f) + 4); diff[(size_t) i].len = (int) (apl[i] * 0.001f * sr); diff[(size_t) i].g = 0.62f; }
        pre.prepare ((int) (0.3f * sr) + 8);
        reset();
    }
    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& a : diff) a.d.clear();
        for (auto& f : damp) f.reset();
        pre.clear();
        for (int i = 0; i < N; ++i) lfoPh[(size_t) i] = (float) i / (float) N;
    }

    struct Settings { float size = 0.6f, decay = 2.5f, damping = 0.5f, preDelayMs = 20.0f, mod = 0.3f; };

    void set (const Settings& s) noexcept
    {
        const float scale = 0.45f + 1.2f * clamp01 (s.size);
        for (int i = 0; i < N; ++i)
        {
            len[(size_t) i] = kLens[i] * 0.001f * sr * scale;
            gain[(size_t) i] = std::pow (10.0f, -3.0f * (len[(size_t) i] / sr) / std::max (0.2f, s.decay));
            damp[(size_t) i].set (lerp (16000.0f, 1800.0f, clamp01 (s.damping)), sr);
        }
        preSamples = std::max (1.0f, s.preDelayMs * 0.001f * sr);
        modDepth = (0.2f + 3.0f * clamp01 (s.mod)) * sr / 44100.0f;
    }

    void process (float in, float& outL, float& outR) noexcept
    {
        pre.push (in);
        float x = pre.read (preSamples);
        for (auto& a : diff) x = a.process (x);

        float o[N];
        for (int i = 0; i < N; ++i)
        {
            float d = len[(size_t) i];
            if (i < 4)
            {
                lfoPh[(size_t) i] += (0.11f + 0.07f * (float) i) / sr;
                if (lfoPh[(size_t) i] >= 1.0f) lfoPh[(size_t) i] -= 1.0f;
                d += modDepth * std::sin (kTwoPi * lfoPh[(size_t) i]);
            }
            o[i] = damp[(size_t) i].process (lines[(size_t) i].read (d)) * gain[(size_t) i];
        }
        // Householder feedback
        float sum = 0.0f; for (int i = 0; i < N; ++i) sum += o[i];
        const float hh = sum * (2.0f / (float) N);
        for (int i = 0; i < N; ++i) lines[(size_t) i].push (o[i] - hh + x * 0.35f);

        outL = (o[0] - o[2] + o[4] + o[6] - o[7] * 0.5f) * 0.45f;
        outR = (o[1] + o[3] - o[5] + o[7] - o[6] * 0.5f) * 0.45f;
    }

private:
    static constexpr int N = 8;
    static constexpr float kLens[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
    float sr = 44100, preSamples = 1, modDepth = 1;
    std::array<DelayLine, N> lines;
    std::array<Allpass, 4> diff;
    std::array<OnePoleLP, N> damp;
    std::array<float, N> len {}, gain {}, lfoPh {};
    DelayLine pre;
};

// ============================================================ tempo-synced stereo delay
class SyncDelay
{
public:
    static constexpr int kNumDivs = 10;
    static const char* divName (int i)
    {
        static const char* n[kNumDivs] = { "1/32", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2" };
        return n[std::clamp (i, 0, kNumDivs - 1)];
    }
    static double divQuarters (int i)
    {
        static const double q[kNumDivs] = { 0.125, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };
        return q[std::clamp (i, 0, kNumDivs - 1)];
    }

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& l : line) l.prepare ((int) (3.2f * sr));
        reset();
    }
    void reset()
    {
        for (auto& l : line) l.clear();
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        timeSm.reset (-1.0f);
        fb[0] = fb[1] = 0.0f;
    }

    struct Settings { int div = 5; double bpm = 120; float feedback = 0.35f, hpHz = 300, lpHz = 5000, wow = 0.0f; bool pingPong = true; };

    void set (const Settings& s) noexcept
    {
        const float target = std::min (3.0f * sr, (float) (divQuarters (s.div) * 60.0 / std::max (20.0, s.bpm) * sr));
        if (timeSm.z < 0.0f) timeSm.reset (target);
        timeTarget = target;
        timeSm.setTime (0.08f, sr);
        for (auto& f : hp) f.highpass (s.hpHz, 0.6f, sr);
        for (auto& f : lp) f.lowpass (s.lpHz, 0.6f, sr);
        feedback = std::min (0.92f, s.feedback);
        pingPong = s.pingPong;
        wow = s.wow;
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float tm = timeSm.process (timeTarget);
        const float w = wow > 0.0f ? wowLfo.next (0.7f + 2.0f * wow, sr) * wow * 0.004f * sr : 0.0f;
        const float yl = line[0].read (std::max (1.0f, tm + w));
        const float yr = line[1].read (std::max (1.0f, tm - w));
        const float fl = lp[0].process (hp[0].process (yl));
        const float fr = lp[1].process (hp[1].process (yr));
        if (pingPong)
        {
            const float mono = 0.5f * (inL + inR);
            line[0].push (fastTanh (mono + feedback * fr));
            line[1].push (fastTanh (feedback * fl));
        }
        else
        {
            line[0].push (fastTanh (inL + feedback * fl));
            line[1].push (fastTanh (inR + feedback * fr));
        }
        outL = fl; outR = fr;
    }

private:
    float sr = 44100, timeTarget = 1000, feedback = 0.3f, wow = 0.0f;
    bool pingPong = true;
    std::array<DelayLine, 2> line;
    std::array<Biquad, 2> hp, lp;
    OnePole timeSm;
    DriftLfo wowLfo;
    float fb[2] {};
};

// ============================================================ micro-pitch doubler (keeps the lead centred)
class Doubler
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& d : dl) d.prepare ((int) (0.05f * sr));
        reset();
    }
    void reset() { for (auto& d : dl) d.clear(); ph[0] = 0.0f; ph[1] = 0.37f; }

    // depth 0..1 (pitch spread), rate 0..1, returns an L/R pair of detuned copies
    void process (float in, float depth, float rate, float& l, float& r) noexcept
    {
        dl[0].push (in); dl[1].push (in);
        const float hz = 0.18f + 1.4f * rate;
        ph[0] += hz / sr; if (ph[0] >= 1) ph[0] -= 1;
        ph[1] += hz * 1.13f / sr; if (ph[1] >= 1) ph[1] -= 1;
        const float dep = (0.25f + 2.2f * depth) * 0.001f * sr;
        l = dl[0].read (0.011f * sr + dep * (0.5f + 0.5f * std::sin (kTwoPi * ph[0])));
        r = dl[1].read (0.017f * sr + dep * (0.5f + 0.5f * std::sin (kTwoPi * ph[1] + 1.7f)));
    }

private:
    float sr = 44100;
    std::array<DelayLine, 2> dl;
    float ph[2] {};
};

// ============================================================ shadow layer
// Heavily filtered, driven, levelled, slightly delayed & split copy that thickens the lead
// without reading as a second vocal.
class ShadowLayer
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        hp.highpass (260.0f, 0.7f, sr);
        lp.lowpass (2100.0f, 0.8f, sr);
        env.set (8.0f, 180.0f, sr);
        for (auto& d : dl) d.prepare ((int) (0.05f * sr));
        reset();
    }
    void reset() { hp.reset(); lp.reset(); env.reset(); for (auto& d : dl) d.clear(); }

    void process (float in, float drive, float& l, float& r) noexcept
    {
        float x = lp.process (hp.process (in));
        x = fastTanh (x * (3.0f + 12.0f * drive));
        const float e = env.process (x);
        x *= 0.25f / std::max (0.05f, e);        // levelling (heavy compression)
        dl[0].push (x); dl[1].push (-x);         // polarity split = wide, mono-safe-ish
        l = dl[0].read (0.017f * sr);
        r = dl[1].read (0.029f * sr);
    }

private:
    float sr = 44100;
    Biquad hp, lp;
    EnvFollower env;
    std::array<DelayLine, 2> dl;
};
} // namespace plx
