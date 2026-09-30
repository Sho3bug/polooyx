#pragma once
// POLOOYX — pitch tracking, scale correction and PSOLA pitch/formant shifting.
//
// Detection : YIN (de Cheveigné & Kawahara 2002) on a decimated mono copy of the input.
// Shifting  : real-time TD-PSOLA-style overlap-add. Output grains are laid down at the
//             *corrected* period, so pitch changes while the spectral envelope (formants)
//             stays put. Formant shifting resamples the inside of each grain, which moves
//             the envelope without moving the pitch — the two are independent.
// Latency   : fixed kLatencyMs, reported to the host (FL Studio compensates on playback).

#include "DspCommon.h"
#include <array>
#include <atomic>

namespace plx
{
// ============================================================ YIN detector
class PitchDetector
{
public:
    void prepare (double sampleRate)
    {
        sr     = (float) sampleRate;
        decim  = std::max (1, (int) std::floor (sampleRate / 16000.0));
        decRate = sr / (float) decim;
        minLag = std::max (2, (int) std::floor (decRate / kMaxHz));
        maxLag = (int) std::ceil (decRate / kMinHz);
        window = maxLag;
        hop    = std::max (16, (int) (decRate * 0.004f));
        aa.lowpass (decRate * 0.42f, 0.707f, sr);
        aa2.lowpass (decRate * 0.42f, 0.707f, sr);
        ring.assign (kRing, 0.0f);
        diff.assign ((size_t) maxLag + 2, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        aa.reset(); aa2.reset();
        wpos = 0; decCount = 0; hopCount = 0;
        f0 = 0.0f; voiced = false; clarity = 0.0f;
        hist[0] = hist[1] = hist[2] = 0.0f;
    }

    // Feed one mono sample. Returns true when a new estimate was produced.
    bool push (float x) noexcept
    {
        const float y = aa2.process (aa.process (x));
        if (++decCount < decim) return false;
        decCount = 0;
        ring[(size_t) wpos] = y;
        wpos = (wpos + 1) & (kRing - 1);
        if (++hopCount < hop) return false;
        hopCount = 0;
        analyse();
        return true;
    }

    float getF0() const noexcept      { return voiced ? f0 : 0.0f; }
    bool  isVoiced() const noexcept   { return voiced; }
    float getClarity() const noexcept { return clarity; }

private:
    static constexpr float kMinHz = 70.0f, kMaxHz = 1000.0f;
    static constexpr int   kRing  = 2048;

    float at (int back) const noexcept { return ring[(size_t) ((wpos - 1 - back) & (kRing - 1))]; }

    void analyse() noexcept
    {
        const int span = window + maxLag;
        // energy gate
        float e = 0.0f;
        for (int i = 0; i < window; ++i) { const float v = at (i); e += v * v; }
        const float rms = std::sqrt (e / (float) window);
        if (rms < 0.0035f) { setUnvoiced(); return; }   // ~ -49 dBFS

        // difference function d(tau), oldest-first window
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            float s = 0.0f;
            for (int j = 0; j < window; ++j)
            {
                const float d = at (span - 1 - j) - at (span - 1 - j - tau);
                s += d * d;
            }
            diff[(size_t) tau] = s;
        }
        // cumulative mean normalised difference
        diff[0] = 1.0f;
        float run = 0.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            run += diff[(size_t) tau];
            diff[(size_t) tau] = run > 0.0f ? diff[(size_t) tau] * (float) tau / run : 1.0f;
        }
        // absolute threshold search
        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (diff[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            // fall back to global minimum if it's reasonably periodic
            float m = 1.0f;
            for (int tau = minLag; tau < maxLag; ++tau)
                if (diff[(size_t) tau] < m) { m = diff[(size_t) tau]; best = tau; }
            if (m > 0.3f) { setUnvoiced(); return; }
        }
        // parabolic interpolation
        float t = (float) best;
        if (best > 1 && best < maxLag)
        {
            const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1.0e-9f) t += 0.5f * (a - c) / den;
        }
        clarity = 1.0f - std::min (1.0f, diff[(size_t) best]);
        const float est = decRate / t;

        // median of three to kill octave blips
        hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = est;
        float med = est;
        if (hist[0] > 0 && hist[1] > 0)
            med = std::max (std::min (hist[0], hist[1]), std::min (std::max (hist[0], hist[1]), hist[2]));
        f0 = med;
        voiced = true;
    }

    void setUnvoiced() noexcept { voiced = false; clarity = 0.0f; hist[2] = 0.0f; }

    float sr = 44100, decRate = 22050;
    int decim = 2, minLag = 20, maxLag = 300, window = 300, hop = 88;
    int wpos = 0, decCount = 0, hopCount = 0;
    Biquad aa, aa2;
    std::vector<float> ring, diff;
    float f0 = 0, clarity = 0;
    float hist[3] {};
    bool voiced = false;
};

// ============================================================ scale logic + retune
class ScaleTuner
{
public:
    enum Scale { Chromatic = 0, Major, Minor, HarmonicMinor, Pentatonic, NumScales };

    void setScale (int key, int scale) noexcept
    {
        static const int masks[NumScales][12] = {
            { 1,1,1,1,1,1,1,1,1,1,1,1 },
            { 1,0,1,0,1,1,0,1,0,1,0,1 },
            { 1,0,1,1,0,1,0,1,1,0,1,0 },
            { 1,0,1,1,0,1,0,1,1,0,0,1 },
            { 1,0,0,1,0,1,0,1,0,0,1,0 },   // minor pentatonic
        };
        for (int i = 0; i < 12; ++i) allowed[(size_t) i] = masks[std::clamp (scale, 0, NumScales - 1)][((i - key) % 12 + 12) % 12] != 0;
    }

    // Returns the target MIDI note for a detected (fractional) MIDI pitch, with hysteresis.
    float target (float midi) noexcept
    {
        const int base = (int) std::lround (midi);
        int bestNote = base; float bestDist = 99.0f;
        for (int d = -6; d <= 6; ++d)
        {
            const int n = base + d;
            if (! allowed[(size_t) (((n % 12) + 12) % 12)]) continue;
            const float dist = std::abs (midi - (float) n);
            if (dist < bestDist) { bestDist = dist; bestNote = n; }
        }
        if (current >= 0 && allowed[(size_t) (((current % 12) + 12) % 12)])
        {
            const float curDist = std::abs (midi - (float) current);
            if (bestNote != current && curDist < bestDist + 0.12f) bestNote = current;   // hysteresis
        }
        current = bestNote;
        return (float) bestNote;
    }
    void resetNote() noexcept { current = -1; }

private:
    std::array<bool, 12> allowed { { true,true,true,true,true,true,true,true,true,true,true,true } };
    int current = -1;
};

// ============================================================ PSOLA shifter (stereo, shared marks)
class PsolaShifter
{
public:
    void prepare (double sampleRate, int latencySamples)
    {
        sr = (float) sampleRate;
        L  = latencySamples;
        int n = 1;
        while (n < L * 8 + 4096) n <<= 1;
        size = n; mask = n - 1;
        for (auto& b : in)  b.assign ((size_t) n, 0.0f);
        for (auto& b : acc) b.assign ((size_t) n, 0.0f);
        win.assign ((size_t) n, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : in)  std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : acc) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (win.begin(), win.end(), 0.0f);
        now = 0; nextSynth = (double) L; anaMark = 0.0;
    }

    // period = analysis period in samples (0 → unvoiced), ratio = pitch ratio, fRatio = formant ratio
    // lock = true → grains are taken exactly at the time-mapped position, so with ratio 1 / formant 1
    // the output is a sample-exact delayed copy of the input (lets us fade to the dry path invisibly).
    void process (float* const* ch, int numCh, int numSamples, float period, float ratio, float fRatio, bool lock = false) noexcept
    {
        locked = lock;
        const bool  voicedNow = period > 1.0f;
        const float T    = voicedNow ? period : sr * 0.0055f;
        const float r    = voicedNow ? std::clamp (ratio, 0.25f, 4.0f) : 1.0f;
        const float Tout = T / r;
        const float f    = std::clamp (fRatio, 0.5f, 2.0f);
        // grain half-length: one analysis period, limited by the latency budget
        const float hMax = ((float) L - 2.0f) / (1.0f + f);
        const float h    = std::max (24.0f, std::min (std::max (T, Tout), hMax));

        for (int i = 0; i < numSamples; ++i)
        {
            const int wi = (int) (now & mask);
            for (int c = 0; c < numCh; ++c) in[(size_t) c][(size_t) wi] = ch[c][i];

            while ((double) now >= nextSynth - h)
            {
                emit (numCh, nextSynth, h, T, f);
                nextSynth += Tout;
            }

            const float wsum = win[(size_t) wi];
            const float norm = 1.0f / std::max (wsum, 0.5f);
            for (int c = 0; c < numCh; ++c)
            {
                ch[c][i] = acc[(size_t) c][(size_t) wi] * norm;
                acc[(size_t) c][(size_t) wi] = 0.0f;
            }
            win[(size_t) wi] = 0.0f;
            ++now;
        }
    }

    int getLatency() const noexcept { return L; }

private:
    float readIn (int c, double t) const noexcept
    {
        const double fl = std::floor (t);
        const int64_t i = (int64_t) fl;
        const float fr = (float) (t - fl);
        const auto& b = in[(size_t) c];
        const float y0 = b[(size_t) ((i - 1) & mask)], y1 = b[(size_t) (i & mask)];
        const float y2 = b[(size_t) ((i + 1) & mask)], y3 = b[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fr + c2) * fr + c1) * fr + y1;
    }

    void emit (int numCh, double s, float h, float T, float f) noexcept
    {
        // pitch-synchronous analysis marks: walk forward in steps of one period
        const double targetIn = s - (double) L;
        if (locked || anaMark > targetIn || anaMark < targetIn - 4.0 * T) anaMark = targetIn;
        while (anaMark + T <= targetIn) anaMark += T;

        const int64_t first = std::max ((int64_t) std::ceil (s - h), now);
        const int64_t last  = (int64_t) std::floor (s + h);
        const float invH = 1.0f / h;
        for (int64_t p = first; p <= last; ++p)
        {
            const float k = (float) ((double) p - s);
            const float w = 0.5f + 0.5f * std::cos (kPi * k * invH);
            const int pi = (int) (p & mask);
            const double src = anaMark + (double) (k * f);
            for (int c = 0; c < numCh; ++c)
                acc[(size_t) c][(size_t) pi] += w * readIn (c, src);
            win[(size_t) pi] += w;
        }
    }

    float sr = 44100;
    int L = 800, size = 0, mask = 0;
    std::array<std::vector<float>, 2> in, acc;
    std::vector<float> win;
    int64_t now = 0;
    double nextSynth = 0, anaMark = 0;
    bool locked = false;
};

// ============================================================ complete pitch stage
class PitchEngine
{
public:
    static constexpr float kLatencyMs = 20.0f;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = (float) sampleRate;
        latency = (int) std::lround (kLatencyMs * 0.001 * sampleRate);
        detector.prepare (sampleRate);
        psola.prepare (sampleRate, latency);
        for (auto& d : dry) d.prepare (latency + maxBlock + 8);
        scratch[0].assign ((size_t) maxBlock, 0.0f);
        scratch[1].assign ((size_t) maxBlock, 0.0f);
        shiftSmooth = 0.0f; activeFade = 0.0f; formantSm = 0.0f;
        tuner.resetNote();
    }

    void reset()
    {
        detector.reset(); psola.reset();
        for (auto& d : dry) d.clear();
        shiftSmooth = 0.0f; activeFade = 0.0f; formantSm = 0.0f; tuner.resetNote();
    }

    int getLatency() const noexcept { return latency; }

    struct Settings
    {
        bool  tuneOn = true;
        int   key = 0, scale = 0;
        float amount = 1.0f;        // 0..1 correction depth
        float retune = 0.2f;        // 0..1 (0 = instant)
        float formantSemis = 0.0f;  // total formant shift
        float extraCents = 0.0f;    // modulation from CHAOS / AURA
    };

    // Processes up to maxBlock samples in place.
    void process (float* const* ch, int numCh, int n, const Settings& s) noexcept
    {
        tuner.setScale (s.key, s.scale);

        // detection runs on the newest input (lookahead relative to the delayed output)
        for (int i = 0; i < n; ++i)
        {
            float m = ch[0][i];
            if (numCh > 1) m = 0.5f * (m + ch[1][i]);
            detector.push (m);
        }

        const float f0 = detector.getF0();
        voicedFlag.store (f0 > 0.0f, std::memory_order_relaxed);
        float desiredCents = 0.0f;
        if (f0 > 0.0f && s.tuneOn)
        {
            const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
            const float tgt  = tuner.target (midi);
            desiredCents = (tgt - midi) * 100.0f * s.amount;
            detectedMidi.store (midi, std::memory_order_relaxed);
            targetMidi.store (tgt, std::memory_order_relaxed);
        }
        else if (f0 <= 0.0f)
        {
            detectedMidi.store (-1.0f, std::memory_order_relaxed);
        }

        const bool needed = (s.tuneOn && s.amount > 0.001f) || std::abs (s.formantSemis) > 0.01f || std::abs (s.extraCents) > 0.05f;

        // retune smoothing (in cents). 0 → instant, 1 → ~400 ms glide. When tuning is switched
        // off the correction glides out quickly instead of snapping.
        float tau = 0.4f * s.retune * s.retune;
        if (! s.tuneOn) tau = std::min (std::max (tau, 0.015f), 0.03f);
        const float blockSec = (float) n / sr;
        const float a = tau <= 0.0005f ? 0.0f : std::exp (-blockSec / tau);
        const float goal = (f0 > 0.0f && s.tuneOn) ? desiredCents : 0.0f;
        shiftSmooth = goal + a * (shiftSmooth - goal);
        if (std::abs (shiftSmooth) < 0.01f && goal == 0.0f) shiftSmooth = 0.0f;
        correctionCents.store (shiftSmooth, std::memory_order_relaxed);

        const float fa = std::exp (-blockSec / 0.02f);
        formantSm = s.formantSemis + fa * (formantSm - s.formantSemis);
        if (std::abs (formantSm - s.formantSemis) < 0.001f) formantSm = s.formantSemis;

        const float totalCents = shiftSmooth + s.extraCents;
        const bool  lock = std::abs (totalCents) < 0.35f && std::abs (formantSm) < 0.01f;
        const float ratio  = lock ? 1.0f : std::pow (2.0f, totalCents / 1200.0f);
        const float fRatio = lock ? 1.0f : std::pow (2.0f, formantSm / 12.0f);

        // keep an aligned dry copy (latency-matched) for the transparent path
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                dry[(size_t) c].push (ch[c][i]);
                scratch[(size_t) c][(size_t) i] = dry[(size_t) c].readInt (latency);
            }

        // the grain engine always runs (keeps its history warm); we only cross-fade to the dry path
        // while it is locked, i.e. while both paths carry the identical signal → switching is inaudible
        const float period = f0 > 0.0f ? sr / f0 : 0.0f;
        psola.process (ch, numCh, n, period, ratio, fRatio, lock);
        const float fadeStep = (float) n / (0.02f * sr);
        const float from = activeFade;
        if (needed && ! lock)  activeFade = 1.0f;                                  // already equal at the switch point
        else if (needed)       activeFade = std::min (1.0f, activeFade + fadeStep);
        else if (lock)         activeFade = std::max (0.0f, activeFade - fadeStep);
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float g = from + (activeFade - from) * ((float) (i + 1) / (float) n);
                ch[c][i] = scratch[(size_t) c][(size_t) i] + g * (ch[c][i] - scratch[(size_t) c][(size_t) i]);
            }
    }

    // UI telemetry
    std::atomic<float> detectedMidi { -1.0f }, targetMidi { -1.0f }, correctionCents { 0.0f };
    std::atomic<bool>  voicedFlag { false };
    float getClarity() const noexcept { return detector.getClarity(); }
    bool  isVoiced() const noexcept { return detector.isVoiced(); }

private:
    float sr = 44100;
    int latency = 882;
    PitchDetector detector;
    ScaleTuner tuner;
    PsolaShifter psola;
    std::array<DelayLine, 2> dry;
    std::array<std::vector<float>, 2> scratch;
    float shiftSmooth = 0.0f, activeFade = 0.0f, formantSm = 0.0f;
};
} // namespace plx
