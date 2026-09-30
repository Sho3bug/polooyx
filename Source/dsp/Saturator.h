#pragma once
// POLOOYX — saturation / damage. Runs inside a 2x oversampled block (see processor).

#include "DspCommon.h"
#include <array>

namespace plx
{
class Saturator
{
public:
    enum Mode { Tube = 0, Tape, Digital, Crush, Alien, NumModes };

    void prepare (double sampleRate)   // sampleRate = the (oversampled) rate this runs at
    {
        sr = (float) sampleRate;
        reset();
    }
    void reset()
    {
        for (auto& f : tone) f.reset();
        for (auto& f : tapeLp) f.reset();
        for (auto& f : dc) f.reset();
        for (auto& h : hold) h = 0.0f;
        holdCount = 0; phase = 0.0f;
    }

    struct Settings { int mode = 0; float drive = 0.3f, mix = 1.0f, tone = 0.0f, outDb = 0.0f; };

    void process (float* const* ch, int numCh, int n, const Settings& s) noexcept
    {
        const float drive = clamp01 (s.drive);
        const float g     = dbToGain (drive * 30.0f);
        const float comp  = 1.0f / std::sqrt (g);
        const float out   = dbToGain (s.outDb);
        for (int c = 0; c < numCh; ++c)
        {
            tone[(size_t) c].highShelf (2500.0f, s.tone * 9.0f, sr);
            tapeLp[(size_t) c].lowpass (16000.0f - drive * 7000.0f, 0.6f, sr);
            dc[(size_t) c].highpass (18.0f, 0.707f, sr);
        }

        // CRUSH parameters
        const float bits  = 12.0f - drive * 8.5f;
        const float q     = std::pow (2.0f, bits - 1.0f);
        const int   holdN = 1 + (int) (drive * drive * (sr / 44100.0f) * 14.0f);
        // DIGITAL: quantiser that only bites near the top of the range
        const float dq    = std::pow (2.0f, 14.0f - drive * 7.0f);
        const float phInc = (35.0f + 80.0f * drive) / sr;

        for (int i = 0; i < n; ++i)
        {
            bool newHold = false;
            if (s.mode == Crush && ++holdCount >= holdN) { holdCount = 0; newHold = true; }
            phase += phInc; if (phase >= 1.0f) phase -= 1.0f;
            const float ring = std::sin (kTwoPi * phase);

            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                const float d = x * g;
                float y = d;
                switch (s.mode)
                {
                    case Tube:
                    {
                        const float bias = 0.25f;
                        y = fastTanh (d + bias) - fastTanh (bias);
                        break;
                    }
                    case Tape:
                    {
                        y = fastTanh (d * 0.85f);
                        y = y - 0.08f * y * y * y;
                        y = tapeLp[(size_t) c].process (y);
                        break;
                    }
                    case Digital:
                    {
                        float v = std::max (-1.0f, std::min (1.0f, d * 0.8f));
                        // soft fold above the clip point adds hard, glassy upper harmonics
                        if (std::abs (d) > 1.0f) v = (d > 0 ? 1.0f : -1.0f) * (1.0f - 0.35f * (std::abs (d) - 1.0f) / (1.0f + std::abs (d)));
                        y = std::round (v * dq) / dq;
                        break;
                    }
                    case Crush:
                    {
                        if (newHold) hold[(size_t) c] = std::round (fastTanh (d) * q) / q;
                        y = hold[(size_t) c];
                        break;
                    }
                    case Alien:
                    default:
                    {
                        const float fold = std::sin (d * 1.3f);                 // wavefolder
                        const float rm   = fold * (0.55f + 0.45f * ring);        // slow ring-mod shimmer
                        y = fastTanh (rm * (1.0f + drive));
                        break;
                    }
                }
                y = dc[(size_t) c].process (y);
                y = tone[(size_t) c].process (y);
                const float wet = y * (s.mode == Crush ? 0.9f : comp * 1.4f);
                const float m = mixPrev + (s.mix - mixPrev) * ((float) (i + 1) / (float) n);
                ch[c][i] = (x + m * (wet - x)) * out;
            }
        }
        mixPrev = s.mix;
    }
    float mixPrev = 0.0f;

private:
    float sr = 88200;
    std::array<Biquad, 2> tone, tapeLp, dc;
    std::array<float, 2> hold {};
    int holdCount = 0;
    float phase = 0.0f;
};
} // namespace plx
