#pragma once
// POLOOYX — dynamics: vocal compressor, split-band de-esser, safety soft clip, lookahead limiter.

#include "DspCommon.h"
#include <array>

namespace plx
{
// ============================================================ compressor
class Compressor
{
public:
    void prepare (double sampleRate) { sr = (float) sampleRate; envDb = -120.0f; }
    void reset() { envDb = -120.0f; }

    struct Settings { float threshDb = -18, ratio = 3, attackMs = 5, releaseMs = 120, makeupDb = 0, kneeDb = 6, mix = 1; };

    void process (float* const* ch, int numCh, int n, const Settings& s) noexcept
    {
        const float aC = std::exp (-1.0f / (std::max (0.05f, s.attackMs) * 0.001f * sr));
        const float rC = std::exp (-1.0f / (std::max (1.0f, s.releaseMs) * 0.001f * sr));
        const float slope = 1.0f - 1.0f / std::max (1.0f, s.ratio);
        const float makeup = dbToGain (s.makeupDb);
        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float pk = std::abs (ch[0][i]);
            if (numCh > 1) pk = std::max (pk, std::abs (ch[1][i]));
            const float inDb = gainToDb (pk + 1.0e-7f);
            // log-domain ballistic
            envDb = inDb > envDb ? inDb + aC * (envDb - inDb) : inDb + rC * (envDb - inDb);

            // soft-knee gain computer
            const float over = envDb - s.threshDb;
            float gr;
            if (2.0f * over < -s.kneeDb)                 gr = 0.0f;
            else if (2.0f * std::abs (over) <= s.kneeDb) { const float t = over + s.kneeDb * 0.5f; gr = slope * t * t / (2.0f * s.kneeDb); }
            else                                         gr = slope * over;

            maxGr = std::max (maxGr, gr);
            const float g = dbToGain (-gr) * makeup;
            const float m = mixPrev + (s.mix - mixPrev) * ((float) (i + 1) / (float) n);
            const float gg = 1.0f + m * (g - 1.0f);
            for (int c = 0; c < numCh; ++c) ch[c][i] *= gg;
        }
        mixPrev = s.mix;
        grDb = maxGr * s.mix;
    }

    float grDb = 0.0f;   // telemetry (last block)

private:
    float sr = 44100, envDb = -120.0f, mixPrev = 0.0f;
};

// ============================================================ de-esser (subtractive, phase-safe at unity)
class DeEsser
{
public:
    void prepare (double sampleRate) { sr = (float) sampleRate; reset(); }
    void reset() { for (auto& f : hp) f.reset(); side.reset(); env.reset(); }

    struct Settings { float freq = 6500, threshDb = -28, rangeDb = 10, amount = 1; };

    void process (float* const* ch, int numCh, int n, const Settings& s) noexcept
    {
        side.highpass (s.freq, 0.9f, sr);
        for (auto& f : hp) f.highpass (s.freq * 0.9f, 0.707f, sr);
        env.set (0.3f, 60.0f, sr);
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float m = ch[0][i];
            if (numCh > 1) m = 0.5f * (m + ch[1][i]);
            const float lvl = gainToDb (env.process (side.process (m)) + 1.0e-7f);
            const float gr  = std::min (s.rangeDb, std::max (0.0f, (lvl - s.threshDb) * 0.75f));
            const float amt = amtPrev + (s.amount - amtPrev) * ((float) (i + 1) / (float) n);
            maxGr = std::max (maxGr, gr * amt);
            const float cut = (1.0f - dbToGain (-gr)) * amt;
            for (int c = 0; c < numCh; ++c)
            {
                const float hi = hp[(size_t) c].process (ch[c][i]);
                ch[c][i] -= cut * hi;
            }
        }
        amtPrev = s.amount;
        grDb = maxGr;
    }

    float grDb = 0.0f;

private:
    float sr = 44100, amtPrev = 0.0f;
    std::array<Biquad, 2> hp;
    Biquad side;
    EnvFollower env;
};

// ============================================================ lookahead brickwall limiter
class Limiter
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = (float) sampleRate;
        la = std::max (8, (int) std::lround (0.0015 * sampleRate));
        int n = 1; while (n < la * 2 + 8) n <<= 1;
        rmask = n - 1;
        for (auto& d : delay) d.assign ((size_t) n, 0.0f);
        req.assign ((size_t) n, 1.0f);
        dqIdx.assign ((size_t) n, 0);
        box.assign ((size_t) n, 1.0f);
        reset();
    }

    void reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (req.begin(), req.end(), 1.0f);
        std::fill (box.begin(), box.end(), 1.0f);
        head = tail = 0; t = 0; env = 1.0f; boxSum = (double) la;
    }

    int getLatency() const noexcept { return la; }

    void process (float* const* ch, int numCh, int n, float ceilingDb, float releaseMs) noexcept
    {
        const float ceil = dbToGain (ceilingDb);
        const float rC = std::exp (-1.0f / (std::max (5.0f, releaseMs) * 0.001f * sr));
        float minG = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float pk = std::abs (ch[0][i]);
            if (numCh > 1) pk = std::max (pk, std::abs (ch[1][i]));
            const float g = pk > ceil ? ceil / pk : 1.0f;

            // sliding-window minimum over the lookahead (monotonic deque)
            const int idx = (int) (t & rmask);
            req[(size_t) idx] = g;
            while (head != tail && req[(size_t) (dqIdx[(size_t) ((tail - 1) & rmask)] & rmask)] >= g) tail = (tail - 1) & rmask;
            dqIdx[(size_t) tail] = t; tail = (tail + 1) & rmask;
            while (dqIdx[(size_t) head] <= t - la) head = (head + 1) & rmask;
            const float wmin = req[(size_t) (dqIdx[(size_t) head] & rmask)];

            env = wmin < env ? wmin : wmin + rC * (env - wmin);

            // boxcar smoothing so gain is fully down when the peak leaves the delay
            boxSum += (double) env - (double) box[(size_t) idx];
            box[(size_t) idx] = env;
            const float gg = (float) (boxSum / (double) la);
            minG = std::min (minG, gg);

            const int rd = (int) ((t - la) & rmask);
            for (int c = 0; c < numCh; ++c)
            {
                delay[(size_t) c][(size_t) idx] = ch[c][i];
                float y = delay[(size_t) c][(size_t) rd] * gg;
                y = std::max (-ceil, std::min (ceil, y));   // final safety
                ch[c][i] = y;
            }
            ++t;
            if ((t & 0xFFFF) == 0)   // periodically re-sum to avoid drift
            {
                double s = 0; for (int k = 0; k < la; ++k) s += box[(size_t) ((t - 1 - k) & (int64_t) rmask)];
                boxSum = s;
            }
        }
        grDb = -gainToDb (minG);
    }

    float grDb = 0.0f;

private:
    float sr = 44100;
    int la = 64, rmask = 0, head = 0, tail = 0;
    int64_t t = 0;
    std::array<std::vector<float>, 2> delay;
    std::vector<float> req, box;
    std::vector<int64_t> dqIdx;
    float env = 1.0f;
    double boxSum = 64;
};

// ============================================================ PUNCH: 3-band up/down compressor ("loud & dense")
// Splits into low / mid / high (bands sum back to the input exactly), then per band:
// downward compression above a threshold + upward compression of quiet detail (gated so noise
// isn't pulled up) + makeup. The processor then drives the result into the limiter.
// At amount 0 the output is the exact input.
class Punch
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& f : lo) f.lowpass (220.0f, 0.707f, sr);
        for (auto& f : hi) f.highpass (3200.0f, 0.707f, sr);
        aC = std::exp (-1.0f / (0.004f * sr));
        rC = std::exp (-1.0f / (0.080f * sr));
        reset();
    }
    void reset()
    {
        for (auto& f : lo) f.reset();
        for (auto& f : hi) f.reset();
        env.fill (-120.0f);
        amtPrev = 0.0f;
    }

    void process (float* const* ch, int numCh, int n, float amount) noexcept
    {
        static constexpr float downThr[3] = { -24.0f, -26.0f, -30.0f };
        static constexpr float makeup[3]  = {   3.5f,   4.5f,   5.0f };
        const bool active = amount > 0.0f || amtPrev > 0.0f;
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float band[3][2] = {};
            float x[2];
            for (int c = 0; c < 2; ++c)
            {
                x[c] = ch[std::min (c, numCh - 1)][i];
                band[0][c] = lo[(size_t) c].process (x[c]);
                band[2][c] = hi[(size_t) c].process (x[c]);
                band[1][c] = x[c] - band[0][c] - band[2][c];
            }
            if (! active) continue;   // filters/envelopes stay current; output untouched

            const float amt = amtPrev + (amount - amtPrev) * ((float) (i + 1) / (float) n);
            const float slope = 1.0f - 1.0f / (1.0f + 5.0f * amt);
            float g[3];
            for (int b = 0; b < 3; ++b)
            {
                const float pk = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
                const float inDb = gainToDb (pk + 1.0e-7f);
                env[(size_t) b] = inDb > env[(size_t) b] ? inDb + aC * (env[(size_t) b] - inDb) : inDb + rC * (env[(size_t) b] - inDb);
                const float e = env[(size_t) b];
                const float down = e > downThr[b] ? (e - downThr[b]) * slope : 0.0f;
                float up = 0.0f;
                if (e < -40.0f && e > -66.0f)
                    up = std::min (10.0f, (-40.0f - e) * 0.6f) * std::min (1.0f, (e + 66.0f) / 10.0f) * amt;
                maxGr = std::max (maxGr, down);
                g[b] = dbToGain (makeup[b] * amt + up - down);
            }
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = g[0] * band[0][c] + g[1] * band[1][c] + g[2] * band[2][c];
        }
        amtPrev = amount;
        grDb = maxGr;
    }

    float grDb = 0.0f;

private:
    float sr = 44100, aC = 0, rC = 0, amtPrev = 0;
    std::array<Biquad, 2> lo, hi;
    std::array<float, 3> env {};
};

// ============================================================ safety soft clip
inline float softClip (float x) noexcept
{
    const float t = 0.85f;
    const float a = std::abs (x);
    if (a <= t) return x;
    const float y = t + (1.0f - t) * fastTanh ((a - t) / (1.0f - t));
    return x < 0 ? -y : y;
}
} // namespace plx
