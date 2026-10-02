#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace plx;

namespace
{
constexpr int kChunk = 32;

const char* const kFloatIds[] = {
    id::inGain, id::outGain, id::mix, id::ceiling, id::punch,
    id::aura, id::glitch, id::space, id::dark, id::chaos, id::body,
    id::tuneAmount, id::retune, id::formant,
    id::hpf, id::lpf, id::lowFreq, id::lowGain, id::lmFreq, id::lmGain, id::hmFreq, id::hmGain, id::highFreq, id::highGain,
    id::deessFreq, id::deessThresh, id::deessRange,
    id::compThresh, id::compRatio, id::compAttack, id::compRelease, id::compMakeup,
    id::satDrive, id::satMix, id::satTone, id::satOut,
    id::glitchAmount, id::glitchStutter, id::glitchReverse, id::glitchGate, id::glitchTape, id::glitchPitch, id::glitchMix,
    id::revSize, id::revDecay, id::revPredelay, id::revDamp, id::revMix, id::dlyFeedback, id::dlyMix, id::dlyHp, id::dlyLp, id::duck,
    id::width, id::movement, id::microPitch, id::shadow,
    id::cleanRoom, id::cleanClarity, id::cleanSmooth, id::cleanNoise, id::cleanOn,
    // switches are smoothed too, so turning a module (or POLOOYX mode) on/off is click-free
    id::polooyx, id::eqOn, id::deessOn, id::compOn, id::satOn, id::glitchOn, id::spaceOn
};
}

PolooyxProcessor::PolooyxProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "POLOOYX", createLayout())
{
    for (auto* pid : kFloatIds)
    {
        auto* src = apvts.getRawParameterValue (pid);
        jassert (src != nullptr);
        fIndex[std::string_view (pid)] = (int) fparams.size();
        fparams.push_back ({ src, src->load() });
    }
    abSlots[0] = apvts.copyState();
    for (auto* pid : { id::tuneOn, id::key, id::scale, id::satMode, id::glitchRate, id::glitchSmart, id::dlyDiv, id::dlyPingPong, id::cleanMic })
        raw (pid);   // warm the cache so the audio thread never inserts
}

std::atomic<float>* PolooyxProcessor::raw (const char* pid)
{
    auto it = rawPtr.find (std::string_view (pid));
    if (it != rawPtr.end()) return it->second;
    auto* p = apvts.getRawParameterValue (pid);   // first use happens in the constructor / prepare, not per block
    jassert (p != nullptr);
    rawPtr[std::string_view (pid)] = p;
    return p;
}

bool PolooyxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in  != juce::AudioChannelSet::mono() && in  != juce::AudioChannelSet::stereo()) return false;
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

void PolooyxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = std::max (samplesPerBlock, kChunk);

    clean.prepare (sr);
    pitch.prepare (sr, kChunk);
    deess.prepare (sr);
    comp.prepare (sr);

    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    os->initProcessing ((size_t) kChunk);
    osLatency = (int) std::lround (os->getLatencyInSamples());
    sat.prepare (sr * 2.0);

    shadowL.prepare (sr);
    glitchEng.prepare (sr, kChunk);
    reverb.prepare (sr);
    delay.prepare (sr);
    doubler.prepare (sr);
    limiter.prepare (sr, kChunk);
    punchProc.prepare (sr);
    duckEnv.set (5.0f, 220.0f, (float) sr);

    const int dryLat = pitch.getLatency() + osLatency;
    for (auto& d : dryDelay) d.prepare (dryLat + kChunk + 8);

    totalLatency = dryLat + limiter.getLatency() + kChunk;   // + FIFO
    setLatencySamples (totalLatency);

    for (auto& d : satBypass) d.prepare (osLatency + kChunk + 8);
    for (auto& f : fifoIn)  f.fill (0.0f);
    for (auto& f : fifoOut) f.fill (0.0f);
    fifoPos = 0;

    for (auto& f : fparams) f.sm = f.src->load();
    eqPrev = fp (id::eqOn);
    shadowPrev = dlyPrev = revPrev = dblPrev = 0.0f;
    inGainRamp.reset (dbToGain (fp (id::inGain)));
    { const auto t0 = computeTargets (readParams (false, 0)); outGainRamp.reset (dbToGain (t0.outGainDb + t0.punchDriveDb)); }
    mixRamp.reset (fp (id::mix) * 0.01f);
    reset();
}

void PolooyxProcessor::reset()
{
    clean.reset(); pitch.reset(); deess.reset(); comp.reset(); sat.reset();
    if (os) os->reset();
    shadowL.reset(); glitchEng.reset(); reverb.reset(); delay.reset(); doubler.reset(); limiter.reset(); punchProc.reset();
    for (auto& d : dryDelay) d.clear();
    for (auto& d : satBypass) d.clear();
    for (auto& ch : eq) for (auto& b : ch) b.reset();
    duckEnv.reset();
}

RawParams PolooyxProcessor::readParams (bool smooth, int samples)
{
    const float a = smooth ? std::exp (-(float) samples / (0.025f * (float) sr)) : 0.0f;
    for (auto& f : fparams)
    {
        const float t = f.src->load();
        f.sm = t + a * (f.sm - t);
        if (std::abs (f.sm - t) < 1.0e-4f * (1.0f + std::abs (t))) f.sm = t;
    }

    RawParams p {};
    p.inGain = fp (id::inGain); p.outGain = fp (id::outGain); p.mix = fp (id::mix) * 0.01f; p.ceiling = fp (id::ceiling);
    p.punch = fp (id::punch) * 0.01f;
    p.polooyx = fp (id::polooyx);
    p.aura = fp (id::aura) * 0.01f; p.glitch = fp (id::glitch) * 0.01f; p.space = fp (id::space) * 0.01f;
    p.dark = fp (id::dark) * 0.01f; p.chaos = fp (id::chaos) * 0.01f; p.body = fp (id::body) * 0.01f;
    p.tuneOn = bp (id::tuneOn); p.key = cp (id::key); p.scale = cp (id::scale);
    p.tuneAmount = fp (id::tuneAmount) * 0.01f; p.retune = fp (id::retune) * 0.01f; p.formant = fp (id::formant);
    p.eqOn = fp (id::eqOn);
    p.hpf = fp (id::hpf); p.lpf = fp (id::lpf); p.lowFreq = fp (id::lowFreq); p.lowGain = fp (id::lowGain);
    p.lmFreq = fp (id::lmFreq); p.lmGain = fp (id::lmGain); p.hmFreq = fp (id::hmFreq); p.hmGain = fp (id::hmGain);
    p.highFreq = fp (id::highFreq); p.highGain = fp (id::highGain);
    p.deessOn = fp (id::deessOn); p.deessFreq = fp (id::deessFreq); p.deessThresh = fp (id::deessThresh); p.deessRange = fp (id::deessRange);
    p.compOn = fp (id::compOn); p.compThresh = fp (id::compThresh); p.compRatio = fp (id::compRatio);
    p.compAttack = fp (id::compAttack); p.compRelease = fp (id::compRelease); p.compMakeup = fp (id::compMakeup);
    p.satOn = fp (id::satOn); p.satMode = cp (id::satMode); p.satDrive = fp (id::satDrive) * 0.01f; p.satMix = fp (id::satMix) * 0.01f;
    p.satTone = fp (id::satTone) * 0.01f; p.satOut = fp (id::satOut);
    p.glitchOn = fp (id::glitchOn); p.glitchAmount = fp (id::glitchAmount) * 0.01f; p.glitchRate = cp (id::glitchRate); p.glitchSmart = bp (id::glitchSmart);
    p.glitchStutter = fp (id::glitchStutter) * 0.01f; p.glitchReverse = fp (id::glitchReverse) * 0.01f; p.glitchGate = fp (id::glitchGate) * 0.01f;
    p.glitchTape = fp (id::glitchTape) * 0.01f; p.glitchPitch = fp (id::glitchPitch) * 0.01f; p.glitchMix = fp (id::glitchMix) * 0.01f;
    p.spaceOn = fp (id::spaceOn); p.revSize = fp (id::revSize) * 0.01f; p.revDecay = fp (id::revDecay); p.revPredelay = fp (id::revPredelay);
    p.revDamp = fp (id::revDamp) * 0.01f; p.revMix = fp (id::revMix) * 0.01f; p.dlyDiv = cp (id::dlyDiv);
    p.dlyFeedback = fp (id::dlyFeedback) * 0.01f; p.dlyMix = fp (id::dlyMix) * 0.01f; p.dlyPingPong = bp (id::dlyPingPong);
    p.dlyHp = fp (id::dlyHp); p.dlyLp = fp (id::dlyLp); p.duck = fp (id::duck) * 0.01f;
    p.width = fp (id::width) * 0.01f; p.movement = fp (id::movement) * 0.01f; p.microPitch = fp (id::microPitch) * 0.01f; p.shadow = fp (id::shadow) * 0.01f;
    return p;
}

void PolooyxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    // transport
    GlitchEngine::Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            tr.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) tr.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { tr.ppq = *ppq; tr.hasPpq = true; }
        }
    hostBpm.store ((float) tr.bpm, std::memory_order_relaxed);

    // Everything runs in fixed 32-sample chunks fed through a small FIFO (+32 samples latency).
    // Result: the audio is bit-identical whatever buffer size FL Studio uses.
    const float* inL = numIn > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;
    float* outL = buffer.getWritePointer (0);
    float* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    const int gridOffset = pitch.getLatency() + osLatency;   // audio reaching the glitch stage is this much "older"

    for (int i = 0; i < n; ++i)
    {
        const float l = inL != nullptr ? inL[i] : 0.0f;
        const float r = inR != nullptr ? inR[i] : 0.0f;
        fifoIn[0][(size_t) fifoPos] = l;
        fifoIn[1][(size_t) fifoPos] = r;
        const float ol = fifoOut[0][(size_t) fifoPos], orr = fifoOut[1][(size_t) fifoPos];
        if (outR != nullptr) { outL[i] = ol; outR[i] = orr; }
        else                 outL[i] = 0.5f * (ol + orr);

        if (++fifoPos == kChunk)
        {
            fifoPos = 0;
            const auto raw = readParams (true, kChunk);
            const auto t = computeTargets (raw);
            GlitchEngine::Transport ctr = tr;
            ctr.ppq = tr.ppq + (double) (i - (kChunk - 1) - gridOffset) * tr.bpm / 60.0 / sr;
            float* ptr[2] = { fifoOut[0].data(), fifoOut[1].data() };
            std::copy (fifoIn[0].begin(), fifoIn[0].end(), fifoOut[0].begin());
            std::copy (fifoIn[1].begin(), fifoIn[1].end(), fifoOut[1].begin());
            processChunk (ptr, 2, kChunk, t, ctr);
        }
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
}

void PolooyxProcessor::processChunk (float* const* io, int numCh, int n, const Targets& t, const GlitchEngine::Transport& tr)
{
    const float fsr = (float) sr;
    float* L = io[0]; float* R = io[1];
    float dry[2][kChunk];

    // ---------------- 1. input gain + dry capture
    inGainRamp.set (dbToGain (t.inGainDb), n);
    float pkIn = 0.0f;
    const int dryLat = pitch.getLatency() + osLatency;
    for (int i = 0; i < n; ++i)
    {
        const float g = inGainRamp.next();
        L[i] *= g; R[i] *= g;
        pkIn = std::max (pkIn, std::max (std::abs (L[i]), std::abs (R[i])));
        dryDelay[0].push (L[i]); dryDelay[1].push (R[i]);
        dry[0][i] = dryDelay[0].readInt (dryLat);
        dry[1][i] = dryDelay[1].readInt (dryLat);
    }
    inPeak.store (std::max (pkIn, inPeak.load (std::memory_order_relaxed) * 0.97f), std::memory_order_relaxed);

    // ---------------- 1b. CLEAN: fix the recording (rumble, hiss, room, boxiness, harshness) before anything else
    {
        Clean::Settings cs;
        cs.amount = fp (id::cleanOn); cs.mic = cp (id::cleanMic);
        cs.room = fp (id::cleanRoom) * 0.01f; cs.clarity = fp (id::cleanClarity) * 0.01f;
        cs.smooth = fp (id::cleanSmooth) * 0.01f; cs.noise = fp (id::cleanNoise) * 0.01f;
        clean.process (io, numCh, n, cs);
        cleanGr.store (clean.grDb, std::memory_order_relaxed);
    }

    // ---------------- 2. pitch / formant
    {
        vibPhase += t.vibratoHz * (float) n / fsr; if (vibPhase >= 1.0f) vibPhase -= 1.0f;
        const float drift = pitchDrift.next (t.driftHz * (float) n, fsr);   // advanced per chunk
        PitchEngine::Settings ps;
        ps.tuneOn = t.tuneOn; ps.key = t.key; ps.scale = t.scale;
        ps.amount = t.tuneAmount; ps.retune = t.retune; ps.formantSemis = t.formant;
        ps.extraCents = t.vibratoCents * std::sin (kTwoPi * vibPhase) + t.driftCents * drift;
        pitch.process (io, numCh, n, ps);
    }

    // ---------------- 3. EQ (crossfaded bypass)
    const float eq0 = eqPrev, eq1 = t.eqAmt;
    eqPrev = eq1;
    if (eq0 > 0.0f || eq1 > 0.0f)
    {
        for (int c = 0; c < 2; ++c)
        {
            auto& b = eq[(size_t) c];
            b[0].highpass (t.hpf, 0.5412f, fsr);
            b[1].highpass (t.hpf, 1.3066f, fsr);
            b[2].lowShelf (t.lowFreq, t.lowGain, fsr);
            b[3].peak (t.lmFreq, 0.9f, t.lmGain, fsr);
            b[4].peak (t.hmFreq, 0.8f, t.hmGain, fsr);
            b[5].highShelf (t.highFreq, t.highGain, fsr);
            b[6].lowpass (std::min (t.lpf, 0.45f * fsr), 0.707f, fsr);
        }
        for (int i = 0; i < n; ++i)
        {
            const float mixv = eq0 + (eq1 - eq0) * ((float) (i + 1) / (float) n);
            for (int c = 0; c < 2; ++c)
            {
                const float x = io[c][i];
                float y = x;
                for (auto& bq : eq[(size_t) c]) y = bq.process (y);
                io[c][i] = x + mixv * (y - x);
            }
        }
    }

    // ---------------- 4. de-esser → compressor (always running; enable is a smoothed amount)
    {
        DeEsser::Settings ds { t.deessFreq, t.deessThresh, t.deessRange, t.deessAmt };
        deess.process (io, numCh, n, ds);
        deessGr.store (deess.grDb, std::memory_order_relaxed);

        Compressor::Settings cs;
        cs.threshDb = t.compThresh; cs.ratio = t.compRatio; cs.attackMs = t.compAttack;
        cs.releaseMs = t.compRelease; cs.makeupDb = t.compMakeup; cs.mix = t.compAmt;
        comp.process (io, numCh, n, cs);
        compGr.store (comp.grDb, std::memory_order_relaxed);
    }

    // ---------------- 5. saturation (2x oversampled; always runs so latency stays constant)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) satBypass[(size_t) c].push (io[c][i]);
        juce::dsp::AudioBlock<float> blk (io, 2, (size_t) n);
        auto up = os->processSamplesUp (blk);
        float* upPtr[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
        Saturator::Settings ss;
        ss.mode = t.satMode; ss.drive = t.satDrive; ss.mix = t.satMix * t.satAmt; ss.tone = t.satTone;
        ss.outDb = t.satOut * t.satAmt;
        const bool satActive = ss.mix > 0.0f || sat.mixPrev > 0.0f || ss.outDb != 0.0f;
        if (satActive) sat.process (upPtr, 2, (int) up.getNumSamples(), ss);
        os->processSamplesDown (blk);   // filters keep running so re-engaging is seamless
        if (! satActive)                // …but when saturation is off the output is the exact, latency-matched input
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) io[c][i] = satBypass[(size_t) c].readInt (osLatency + n - 1 - i);
    }

    // ---------------- 6. shadow layer (thickens the lead from underneath)
    {
        const float l0 = shadowPrev, l1 = t.shadow * 0.32f;
        shadowPrev = l1;
        for (int i = 0; i < n; ++i)
        {
            float sl, sr_;
            shadowL.process (0.5f * (L[i] + R[i]), t.shadowDrive, sl, sr_);
            const float lvl = l0 + (l1 - l0) * ((float) (i + 1) / (float) n);
            L[i] += lvl * sl; R[i] += lvl * sr_;
        }
    }

    // ---------------- 7. glitch
    {
        GlitchEngine::Settings gs;
        gs.probability = t.glitchAmt > 0.0f ? t.glitchProb : 0.0f;
        gs.rate = t.glitchRate; gs.smart = t.glitchSmart;
        gs.stutter = t.gStutter; gs.reverse = t.gReverse; gs.gate = t.gGate; gs.tape = t.gTape;
        gs.pitchVar = t.gPitch; gs.chaos = t.gChaos; gs.mix = t.gMix * t.glitchAmt;
        glitchEng.process (io, numCh, n, gs, tr, pitch.isVoiced());
    }
    const bool glitching = glitchEng.active.load (std::memory_order_relaxed);

    // ---------------- 8. space + stereo layers
    {
        FdnReverb::Settings rs { t.revSize, t.revDecay, t.revDamp, t.revPredelay, t.revMod };
        reverb.set (rs);
        SyncDelay::Settings dls; dls.div = t.dlyDiv; dls.bpm = tr.bpm; dls.feedback = t.dlyFeedback;
        dls.hpHz = t.dlyHp; dls.lpHz = t.dlyLp; dls.wow = t.dlyWow; dls.pingPong = t.dlyPingPong;
        delay.set (dls);

        const float dly1 = t.dlyMix * t.spaceAmt + (glitching ? t.glitchSend * t.spaceAmt : 0.0f);
        const float rev1 = t.revMix * t.spaceAmt;
        const float dbl1 = t.doubler * 0.42f;
        const float dly0 = dlyPrev, rev0 = revPrev, dbl0 = dblPrev;
        dlyPrev = dly1; revPrev = rev1; dblPrev = dbl1;
        const float panInc  = t.panRate / fsr;

        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);

            // ducking: space breathes around the lead
            const float env  = duckEnv.process (m);
            const float duck = 1.0f - t.duck * 0.78f * std::min (1.0f, env * 5.0f);

            const float fr = (float) (i + 1) / (float) n;
            const float dlySend = dly0 + (dly1 - dly0) * fr, revSend = rev0 + (rev1 - rev0) * fr, dblLvl = dbl0 + (dbl1 - dbl0) * fr;
            float dl = 0.0f, dr = 0.0f, rl = 0.0f, rr = 0.0f;
            delay.process (m * dlySend, m * dlySend, dl, dr);          // tails ring out naturally when SPACE is switched off
            reverb.process (m * revSend * 1.6f + 0.25f * (dl + dr), rl, rr);

            // auto-pan movement on the space bus
            panPhase += panInc; if (panPhase >= 1.0f) panPhase -= 1.0f;
            const float pan = t.panDepth * 0.8f * std::sin (kTwoPi * panPhase);
            const float ang = (pan + 1.0f) * 0.25f * kPi;
            const float gl = std::cos (ang) * 1.41421356f, gr = std::sin (ang) * 1.41421356f;
            float bl = (dl + rl) * duck * gl;
            float br = (dr + rr) * duck * gr;

            // micro-pitch doubler (L/R only, lead stays centred)
            {   // always running: its delay lines must stay current or re-enabling replays stale audio
                float xl, xr;
                doubler.process (m, t.doublerDepth, t.doublerRate, xl, xr);
                bl += dblLvl * xl; br += dblLvl * xr;
            }

            // width on everything that isn't the lead
            const float mid = 0.5f * (bl + br), side = 0.5f * (bl - br) * t.width;
            L[i] += mid + side;
            R[i] += mid - side;
        }
    }

    // ---------------- 8b. PUNCH: multiband up/down compression (density), then drive into the limiter (loudness)
    punchProc.process (io, numCh, n, t.punch);

    // ---------------- 9. output gain (+ punch drive), dry/wet, safety
    outGainRamp.set (dbToGain (t.outGainDb + t.punchDriveDb), n);
    mixRamp.set (t.mix, n);
    for (int i = 0; i < n; ++i)
    {
        const float og = outGainRamp.next(), mx = mixRamp.next();
        for (int c = 0; c < 2; ++c)
        {
            const float w = io[c][i] * og;
            io[c][i] = softClip (dry[c][i] + mx * (w - dry[c][i]));
        }
    }
    limiter.process (io, numCh, n, t.ceiling, 60.0f);
    limGr.store (limiter.grDb, std::memory_order_relaxed);

    // ---------------- telemetry
    float pkOut = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        pkOut = std::max (pkOut, std::max (std::abs (L[i]), std::abs (R[i])));
        if (++scopeDecim >= 4)
        {
            scopeDecim = 0;
            const int w = scopeWrite.load (std::memory_order_relaxed);
            scopeIn[(size_t) w].store (dry[0][i], std::memory_order_relaxed);
            scopeOut[(size_t) w].store (0.5f * (L[i] + R[i]), std::memory_order_relaxed);
            scopeWrite.store ((w + 1) % kScopeSize, std::memory_order_relaxed);
        }
    }
    outPeak.store (std::max (pkOut, outPeak.load (std::memory_order_relaxed) * 0.97f), std::memory_order_relaxed);
}

// ============================================================ programs / presets
int PolooyxProcessor::getNumPrograms() { return (int) factoryPresets().size(); }
const juce::String PolooyxProcessor::getProgramName (int i)
{
    const auto& p = factoryPresets();
    return juce::isPositiveAndBelow (i, (int) p.size()) ? juce::String (p[(size_t) i].name) : juce::String();
}
void PolooyxProcessor::setCurrentProgram (int i)
{
    if (juce::isPositiveAndBelow (i, getNumPrograms())) loadFactoryPreset (i);
}

juce::StringArray PolooyxProcessor::getFactoryPresetNames() const
{
    juce::StringArray s;
    for (auto& p : factoryPresets()) s.add (p.name);
    return s;
}

void PolooyxProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
}

void PolooyxProcessor::loadFactoryPreset (int index)
{
    const auto& all = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) all.size())) return;
    undo.beginNewTransaction ("Preset");
    // session settings survive a preset / style change: the song's key and scale, and the gain staging
    // and so does CLEAN, which is about the mic and the room, not the style
    static const char* keep[] = { id::key, id::scale, id::inGain, id::outGain, id::ceiling, id::mix,
                                  id::cleanOn, id::cleanMic, id::cleanRoom, id::cleanClarity, id::cleanSmooth, id::cleanNoise };
    constexpr int nKeep = (int) (sizeof (keep) / sizeof (keep[0]));
    float kept[nKeep];
    for (int i = 0; i < nKeep; ++i) kept[i] = apvts.getParameter (keep[i])->getValue();
    resetToDefaults();
    for (int i = 0; i < nKeep; ++i) apvts.getParameter (keep[i])->setValueNotifyingHost (kept[i]);
    for (auto& [pid, val] : all[(size_t) index].values)
        if (auto* rp = apvts.getParameter (pid))
            rp->setValueNotifyingHost (rp->convertTo0to1 (val));
    currentProgram = index;
    currentPresetName = all[(size_t) index].name;
}

juce::File PolooyxProcessor::getUserPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("POLOOYX").getChildFile ("Presets");
}

juce::Array<juce::File> PolooyxProcessor::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.polooyx");
    files.sort();
    return files;
}

bool PolooyxProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory()) return false;
    auto state = apvts.copyState();
    state.setProperty ("presetName", name.trim(), nullptr);
    if (auto xml = state.createXml())
    {
        if (xml->writeTo (folder.getChildFile (clean + ".polooyx")))
        {
            currentPresetName = name.trim();
            return true;
        }
    }
    return false;
}

bool PolooyxProcessor::loadUserPreset (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName (apvts.state.getType()))
        {
            undo.beginNewTransaction ("Preset");
            auto tree = juce::ValueTree::fromXml (*xml);
            // apply param by param so it's undoable and hosts see the changes
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    auto child = tree.getChildWithProperty ("id", rp->getParameterID());
                    const float v = child.isValid() ? (float) child.getProperty ("value") : rp->convertFrom0to1 (rp->getDefaultValue());
                    rp->setValueNotifyingHost (rp->convertTo0to1 (v));
                }
            currentPresetName = tree.getProperty ("presetName", f.getFileNameWithoutExtension()).toString();
            return true;
        }
    return false;
}

// ============================================================ A/B
void PolooyxProcessor::storeAB (int slot) { abSlots[slot & 1] = apvts.copyState(); }

void PolooyxProcessor::switchAB()
{
    abSlots[activeAB] = apvts.copyState();
    activeAB ^= 1;
    if (abSlots[activeAB].isValid())
    {
        auto target = abSlots[activeAB];
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto child = target.getChildWithProperty ("id", rp->getParameterID());
                if (child.isValid()) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
            }
    }
    else abSlots[activeAB] = apvts.copyState();   // first switch: B starts as a copy of A
}

// ============================================================ state
void PolooyxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("presetName", currentPresetName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void PolooyxProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentPresetName = tree.getProperty ("presetName", "POLOOYX DEFAULT").toString();
            apvts.replaceState (tree);
            // replaceState only pushes values whose tree property changed; set every parameter explicitly
            // so the restore is exact even if a host left a switch at an in-between normalised value
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    auto child = tree.getChildWithProperty ("id", rp->getParameterID());
                    const float v = child.isValid() ? (float) child.getProperty ("value") : rp->convertFrom0to1 (rp->getDefaultValue());
                    rp->setValueNotifyingHost (rp->convertTo0to1 (v));
                }
        }
}

juce::AudioProcessorEditor* PolooyxProcessor::createEditor() { return new PolooyxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PolooyxProcessor(); }
