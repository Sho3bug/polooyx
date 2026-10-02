#pragma once
// POLOOYX — CLEAN: recording cleanup that runs first in the chain.
//   NOISE   rumble high-pass + gentle downward expander for hiss/hum between lines
//   ROOM    cuts the boxy "bedroom" low-mids and shortens room-echo tails (envelope-based tail suppression)
//   CLARITY lifts the 3 kHz presence band and clears the 800 Hz honk
//   SMOOTH  tames harsh/hissy top end: static air shelf + dynamic 5-10 kHz harshness reducer
//   MIC     profile offsets for a specific microphone (AT2020: bright condenser that hears the room)
// With amount = 0 the output is the exact input.

#include "DspCommon.h"
#include <array>

namespace plx
{
class Clean
{
public:
    enum Mic { generic = 0, at2020 = 1 };

    struct Settings { float amount = 1, room = 0.35f, clarity = 0.35f, smooth = 0.35f, noise = 0.35f; int mic = generic; };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        fastEnv.set (0.5f, 25.0f, sr);
        slowEnv.set (0.5f, 1000.0f, sr);
        harshEnv.set (0.3f, 40.0f, sr);
        fullEnv.set (0.3f, 40.0f, sr);
        detBand.bandpass (7000.0f, 0.8f, sr);
        cutSm.setTime (0.006f, sr);
        for (auto& f : split) f.highpass (4500.0f, 0.707f, sr);
        reset();
    }

    void reset()
    {
        for (auto& ch : eq) for (auto& b : ch) b.reset();
        for (auto& f : split) f.reset();
        fastEnv.reset(); slowEnv.reset(); harshEnv.reset(); fullEnv.reset(); detBand.reset();
        cutSm.reset (0.0f); harshSm = 0.0f;
        amtPrev = 0.0f; grDb = 0.0f;
    }

    void process (float* const* ch, int numCh, int n, const Settings& s) noexcept
    {
        const bool at = s.mic == at2020;
        // ---- static voicing (recomputed per 32-sample chunk; parameters arrive smoothed)
        const float hpHz     = 30.0f + 90.0f * s.noise + (at ? 10.0f : 0.0f);
        const float boxDb    = -7.0f * s.room - (at ? 2.0f : 0.0f);
        const float honkDb   = -2.5f * s.clarity;
        const float presDb   = 5.5f * s.clarity + (at ? 1.5f : 0.0f);
        const float airDb    = -5.0f * s.smooth - (at ? 2.5f : 0.0f);
        for (auto& b : eq)
        {
            b[0].highpass (hpHz, 0.5412f, sr);
            b[1].highpass (hpHz, 1.3066f, sr);
            b[2].peak (350.0f, 1.0f, boxDb, sr);
            b[3].peak (800.0f, 1.2f, honkDb, sr);
            b[4].peak (3200.0f, 1.1f, presDb, sr);
            b[5].highShelf (8500.0f, airDb, sr);
        }

        const float maxTail  = 10.0f * s.room;
        const float thr      = -60.0f + 20.0f * s.noise;          // expander threshold (dBFS)
        const float maxExp   = 24.0f * s.noise;
        const float maxHarsh = 9.0f * s.smooth + (at ? 2.0f : 0.0f) * s.smooth;
        const float harshOff = at ? 14.0f : 12.0f;                 // how close 5-10 kHz may get to the full level

        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float x[2];
            for (int c = 0; c < 2; ++c) x[c] = ch[std::min (c, numCh - 1)][i];
            const float m = 0.5f * (x[0] + x[1]);

            // ---- detectors (on the incoming signal)
            const float fast = fastEnv.process (m), slow = slowEnv.process (m);
            const float fastDb = gainToDb (fast + 1.0e-9f), slowDb = gainToDb (slow + 1.0e-9f);
            const float tailCut = maxTail * std::clamp ((slowDb - fastDb - 3.0f) / 10.0f, 0.0f, 1.0f);
            const float expCut  = std::clamp (2.0f * (thr - fastDb), 0.0f, maxExp);
            const float cut     = cutSm.process (tailCut + expCut);
            const float g       = dbToGain (-cut);

            const float he = harshEnv.process (detBand.process (m)), fe = fullEnv.process (m);
            const float excess = gainToDb (he + 1.0e-9f) - gainToDb (fe + 1.0e-9f) + harshOff;
            const float harshTarget = std::clamp (excess, 0.0f, maxHarsh);
            harshSm += 0.02f * (harshTarget - harshSm);
            const float hcut = 1.0f - dbToGain (-harshSm);
            maxGr = std::max (maxGr, cut + harshSm);

            const float amt = amtPrev + (s.amount - amtPrev) * ((float) (i + 1) / (float) n);
            for (int c = 0; c < 2; ++c)
            {
                float y = x[c];
                for (auto& b : eq[(size_t) c]) y = b.process (y);
                y -= hcut * split[(size_t) c].process (y);
                y *= g;
                const float out = x[c] + amt * (y - x[c]);
                if (c < numCh) ch[c][i] = out;
            }
        }
        amtPrev = s.amount;
        grDb = maxGr * s.amount;
    }

    float grDb = 0.0f;   // telemetry: deepest cleanup reduction in the last chunk

private:
    float sr = 44100.0f, amtPrev = 0.0f, harshSm = 0.0f;
    std::array<std::array<Biquad, 6>, 2> eq;
    std::array<Biquad, 2> split;
    Biquad detBand;
    EnvFollower fastEnv, slowEnv, harshEnv, fullEnv;
    OnePole cutSm;
};
} // namespace plx
