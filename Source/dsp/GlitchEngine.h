#pragma once
// POLOOYX — tempo-synced glitch engine working on a real recorded audio buffer.
//
// Every grid step (1/2 … 1/32, locked to the DAW's PPQ position when the transport runs)
// the engine decides whether to fire an event. Events manipulate recorded audio:
//   STUTTER  — first slice of the step plays live, then repeats (optionally pitch-stepped)
//   REPEAT   — replays the previous step (buffer repeat)
//   REVERSE  — plays the previous step backwards
//   GATE     — rhythmic chopping of the live signal
//   TAPE     — tape-stop: read speed falls to zero across the step
// SMART mode weights the probability and event choice with an event detector
// (phrase endings, sustained voiced notes, accents, pauses) instead of firing blindly.

#include "DspCommon.h"
#include <array>
#include <atomic>

namespace plx
{
class GlitchEngine
{
public:
    enum Event { None = 0, Stutter, Repeat, Reverse, Gate, Tape };

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = (float) sampleRate;
        int n = 1; while (n < (int) (sampleRate * 6.0)) n <<= 1;
        mask = n - 1;
        for (auto& b : rec) b.assign ((size_t) n, 0.0f);
        fastEnv.set (3.0f, 40.0f, sr);
        slowEnv.set (60.0f, 350.0f, sr);
        reset();
    }

    void reset()
    {
        for (auto& b : rec) std::fill (b.begin(), b.end(), 0.0f);
        t = 0; stepIdx = -1; stepStart = 0; prevStepStart = 0; stepLen = (int64_t) sr / 2; prevStepLen = stepLen;
        event = None; evGain = 0.0f; cooldown = 0; freePpq = 0.0; activeTime = 0; quietTime = 0; voicedTime = 0;
        fastEnv.reset(); slowEnv.reset(); tapePos = 0.0;
    }

    struct Settings
    {
        float probability = 0.0f;   // 0..1 chance per step
        int   rate = 3;             // 0:1/2 1:1/4 2:1/8 3:1/16 4:1/32
        float stutter = 1.0f, reverse = 0.5f, gate = 0.5f, tape = 0.3f;   // event weights 0..1
        float pitchVar = 0.0f;      // 0..1 → up to ±12 st per stutter repeat
        float chaos = 0.0f;         // 0..1 off-grid variations & wilder choices
        float mix = 1.0f;
        bool  smart = true;
    };

    struct Transport { bool playing = false; double ppq = 0.0; double bpm = 120.0; bool hasPpq = false; };

    void process (float* const* ch, int numCh, int n, const Settings& s, const Transport& tr, bool voiced) noexcept
    {
        const double bpm = tr.bpm > 20.0 && tr.bpm < 999.0 ? tr.bpm : 120.0;
        const double ppqPerSample = bpm / 60.0 / (double) sr;
        double ppq = (tr.playing && tr.hasPpq) ? tr.ppq : freePpq;
        const double stepQ = 4.0 / (double) (2 << std::clamp (s.rate, 0, 4));   // quarters per step

        if (voiced) voicedTime += n; else voicedTime = 0;
        bool anyActive = false;

        for (int i = 0; i < n; ++i)
        {
            // --- record
            float mono = 0.0f;
            for (int c = 0; c < numCh; ++c) { rec[(size_t) c][(size_t) (t & mask)] = ch[c][i]; mono += ch[c][i]; }
            if (numCh == 1) rec[1][(size_t) (t & mask)] = ch[0][i];
            mono /= (float) numCh;
            const float fe = fastEnv.process (mono), se = slowEnv.process (mono);
            if (se > 0.01f) { activeTime++; quietTime = 0; } else { quietTime++; if (quietTime > (int64_t) (sr * 0.6f)) activeTime = 0; }

            // --- grid
            const int64_t idx = (int64_t) std::floor ((ppq + 1.0e-9) / stepQ);
            if (idx != stepIdx)
            {
                prevStepStart = stepStart; prevStepLen = std::max<int64_t> (1, t - stepStart);
                stepStart = t;
                stepLen = std::max<int64_t> (32, (int64_t) (stepQ / ppqPerSample));
                if (stepIdx >= 0) decide (s, fe, se, voiced);
                stepIdx = idx;
            }
            ppq += ppqPerSample;

            // --- render
            const int64_t e = t - stepStart;
            float target = event == None ? 0.0f : 1.0f;
            // event fades out at the end of its step so the next step starts clean
            const float edge = std::max (16.0f, sr * 0.0025f);
            if (event != None && (float) (evLen - e) < edge) target = 0.0f;
            evGain += (target - evGain) * (1.0f / (edge * 0.35f));
            if (evGain < 1.0e-4f && event != None && e >= evLen) event = None;

            if (evGain > 1.0e-4f)
            {
                anyActive = true;
                for (int c = 0; c < numCh; ++c)
                {
                    const float live = ch[c][i];
                    const float g = render (c, e, live);
                    const float wet = live + evGain * (g - live);
                    const float m = mixPrev + (s.mix - mixPrev) * ((float) (i + 1) / (float) n);
                    ch[c][i] = live + m * (wet - live);
                }
            }
            ++t;
        }
        mixPrev = s.mix;
        if (! (tr.playing && tr.hasPpq)) freePpq = ppq;
        active.store (anyActive, std::memory_order_relaxed);
    }

    std::atomic<bool> active { false };
    std::atomic<int>  lastEvent { 0 };
    std::atomic<int>  eventCount { 0 };   // total events fired (telemetry / tests)

private:
    float readAbs (int c, double pos) const noexcept
    {
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl;
        const float fr = (float) (pos - fl);
        const auto& b = rec[(size_t) c];
        const float a = b[(size_t) (i & mask)], bb = b[(size_t) ((i + 1) & mask)];
        return a + fr * (bb - a);
    }

    // short fade window used inside repeating slices so every repeat is click-free
    float sliceWin (int64_t o, int64_t len) const noexcept
    {
        const float f = std::max (8.0f, sr * 0.0015f);
        const float a = std::min (1.0f, (float) o / f);
        const float b = std::min (1.0f, (float) (len - o) / f);
        return std::max (0.0f, std::min (a, b));
    }

    float render (int c, int64_t e, float live) noexcept
    {
        switch (event)
        {
            case Stutter:
            {
                if (e < sliceLen) return live;
                const int64_t k = e / sliceLen, o = e % sliceLen;
                const float r = std::clamp (std::pow (2.0f, pitchStep * (float) k / 12.0f), 0.5f, 2.0f);
                const double pos = (double) stepStart + (double) o * r;
                return readAbs (c, pos) * sliceWin (o, sliceLen);
            }
            case Repeat:
            {
                const int64_t o = e % prevStepLen;
                return readAbs (c, (double) (prevStepStart + o)) * sliceWin (o, prevStepLen);
            }
            case Reverse:
            {
                const int64_t o = e % prevStepLen;
                return readAbs (c, (double) (stepStart - 1 - o)) * sliceWin (o, prevStepLen);
            }
            case Gate:
            {
                const int64_t o = e % gateLen;
                const float on = (float) o < (float) gateLen * gateDuty ? 1.0f : 0.0f;
                const float w = sliceWin (o, (int64_t) ((float) gateLen * gateDuty));
                return live * on * w;
            }
            case Tape:
            {
                if (c == 0)
                {
                    const double prog = std::min (1.0, (double) e / (double) evLen);
                    tapeRate = std::max (0.0, 1.0 - std::pow (prog, tapeCurve));
                    tapePos += tapeRate;
                }
                return readAbs (c, (double) stepStart + tapePos);
            }
            default: return live;
        }
    }

    void decide (const Settings& s, float fe, float se, bool voiced) noexcept
    {
        event = None;
        if (s.probability <= 0.0f) { lastEvent.store (0); return; }

        // ---- context (SMART)
        float w = 1.0f;
        float wStut = s.stutter, wRep = 0.35f * s.stutter, wRev = s.reverse, wGate = s.gate, wTape = s.tape;
        if (s.smart)
        {
            const bool present = se > 0.01f;
            const bool ending = present && fe < 0.55f * se && activeTime > (int64_t) (sr * 0.35f);
            const bool accent = fe > 1.8f * se && fe > 0.03f;
            const bool sustained = voiced && voicedTime > (int64_t) (sr * 0.22f);
            const bool pauseAfterPhrase = ! present && quietTime < (int64_t) (sr * 0.5f) && fe < 0.004f;

            if (ending)                { w = 1.7f; wTape *= 2.0f; wStut *= 1.5f; wRev *= 1.5f; }
            else if (pauseAfterPhrase) { w = 1.3f; wRep = 1.2f * (s.stutter + 0.3f); wRev *= 2.0f; wStut *= 0.2f; wGate = 0.0f; wTape = 0.0f; }
            else if (sustained)        { w = 1.3f; wGate *= 2.0f; wStut *= 1.2f; }
            else if (accent)           { w = 0.9f; wStut *= 1.6f; wTape *= 0.3f; }
            else if (! present)        { w = 0.0f; }          // long silence: nothing to glitch
            else                       { w = 0.3f; }          // ordinary mid-phrase syllables: leave the lead alone

            if (cooldown > 0) { w *= 0.25f; --cooldown; }
        }

        if (rng.uni() >= std::min (1.0f, s.probability * w)) { lastEvent.store (0); return; }

        const float total = wStut + wRep + wRev + wGate + wTape;
        if (total <= 0.0f) { lastEvent.store (0); return; }
        float pick = rng.uni() * total;
        if ((pick -= wStut) < 0)      event = Stutter;
        else if ((pick -= wRep) < 0)  event = Repeat;
        else if ((pick -= wRev) < 0)  event = Reverse;
        else if ((pick -= wGate) < 0) event = Gate;
        else                          event = Tape;

        evLen = stepLen;
        // stutter slices: finer with chaos, occasionally off-grid
        int div = 2 << (int) std::floor (rng.uni() * (1.0f + 2.0f * s.chaos + 0.5f));   // 2,4,8(,16)
        div = std::min (div, 16);
        sliceLen = std::max<int64_t> (64, stepLen / div);
        if (s.chaos > 0.5f && rng.uni() < (s.chaos - 0.5f)) sliceLen = std::max<int64_t> (64, (int64_t) ((float) sliceLen * (0.66f + 0.5f * rng.uni())));
        const float maxSemi = 12.0f * s.pitchVar;
        pitchStep = maxSemi > 0.0f ? (rng.uni() < 0.5f ? -1.0f : 1.0f) * (rng.uni() < 0.5f ? maxSemi : maxSemi * 0.5f) : 0.0f;
        pitchStep = std::round (pitchStep);
        gateLen = std::max<int64_t> (48, stepLen / (s.chaos > 0.4f && rng.uni() < 0.5f ? 8 : 4));
        gateDuty = 0.45f + 0.25f * rng.uni();
        tapeCurve = 0.6f + 1.2f * rng.uni() * (0.5f + s.chaos);
        tapePos = 0.0; tapeRate = 1.0;
        if (s.smart) cooldown = 2;
        lastEvent.store ((int) event);
        eventCount.fetch_add (1, std::memory_order_relaxed);
    }

    float sr = 44100;
    int64_t mask = 0;
    std::array<std::vector<float>, 2> rec;
    int64_t t = 0, stepIdx = -1, stepStart = 0, prevStepStart = 0, stepLen = 22050, prevStepLen = 22050;
    int64_t evLen = 0, sliceLen = 1024, gateLen = 1024;
    int64_t activeTime = 0, quietTime = 0, voicedTime = 0;
    float gateDuty = 0.5f, pitchStep = 0.0f, evGain = 0.0f, tapeCurve = 1.0f;
    double tapePos = 0.0, tapeRate = 1.0, freePpq = 0.0;
    Event event = None;
    int cooldown = 0;
    float mixPrev = 0.0f;
    EnvFollower fastEnv, slowEnv;
    Rng rng;
};
} // namespace plx
