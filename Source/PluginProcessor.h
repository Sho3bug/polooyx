#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <unordered_map>
#include <array>
#include <atomic>
#include "Params.h"
#include "MacroMap.h"
#include "dsp/PitchEngine.h"
#include "dsp/Dynamics.h"
#include "dsp/Saturator.h"
#include "dsp/GlitchEngine.h"
#include "dsp/SpaceStereo.h"

class PolooyxProcessor : public juce::AudioProcessor
{
public:
    PolooyxProcessor();
    ~PolooyxProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "POLOOYX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- public API for the editor
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;

    // presets
    juce::StringArray getFactoryPresetNames() const;
    void loadFactoryPreset (int index);
    juce::File getUserPresetFolder() const;
    juce::Array<juce::File> getUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File&);
    juce::String currentPresetName { "POLOOYX DEFAULT" };

    // A/B
    void storeAB (int slot);
    void switchAB();
    int  activeAB = 0;

    void resetToDefaults();

    // telemetry (audio → UI, lock-free)
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, compGr { 0 }, deessGr { 0 }, limGr { 0 };
    static constexpr int kScopeSize = 1024;
    std::array<std::atomic<float>, kScopeSize> scopeIn {}, scopeOut {};
    std::atomic<int> scopeWrite { 0 };
    plx::PitchEngine& getPitchEngine() { return pitch; }
    plx::GlitchEngine& getGlitchEngine() { return glitchEng; }
    std::atomic<float> hostBpm { 120.0f };

    // test hooks
    int getTotalLatency() const { return totalLatency; }
    int getGridOffset() const { return pitch.getLatency() + osLatency; }

private:
    plx::RawParams readParams (bool smooth, int samples);
    void processChunk (float* const* io, int numCh, int n, const plx::Targets& t, const plx::GlitchEngine::Transport& tr);

    double sr = 44100.0;
    int maxBlock = 512, totalLatency = 0, osLatency = 0;
    int currentProgram = 0;

    // parameter smoothing
    struct FloatParam { std::atomic<float>* src; float sm; };
    std::vector<FloatParam> fparams;
    std::unordered_map<const void*, int> fIndex;                      // keyed by the id-constant's address
    std::unordered_map<const void*, std::atomic<float>*> rawPtr;
    float fp (const char* id) const { return fparams[(size_t) fIndex.at (id)].sm; }
    std::atomic<float>* raw (const char* id);
    bool  bp (const char* id) { return raw (id)->load() > 0.5f; }
    int   cp (const char* id) { return (int) raw (id)->load(); }

    // DSP
    plx::PitchEngine pitch;
    std::array<std::array<plx::Biquad, 7>, 2> eq;
    plx::DeEsser deess;
    plx::Compressor comp;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    plx::Saturator sat;
    plx::ShadowLayer shadowL;
    plx::GlitchEngine glitchEng;
    plx::FdnReverb reverb;
    plx::SyncDelay delay;
    plx::Doubler doubler;
    plx::Limiter limiter;
    std::array<plx::DelayLine, 2> dryDelay;
    plx::EnvFollower duckEnv;
    plx::DriftLfo pitchDrift;
    float vibPhase = 0.0f, panPhase = 0.0f;
    plx::Ramp inGainRamp, outGainRamp, mixRamp;
    float eqPrev = 0.0f, shadowPrev = 0.0f, dlyPrev = 0.0f, revPrev = 0.0f, dblPrev = 0.0f;
    std::array<std::array<float, 32>, 2> fifoIn {}, fifoOut {};
    int fifoPos = 0;
    std::array<plx::DelayLine, 2> satBypass;
    int scopeDecim = 0;

    // A/B snapshots
    juce::ValueTree abSlots[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolooyxProcessor)
};
