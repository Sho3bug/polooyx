#pragma once
// POLOOYX — parameter IDs, ranges and layout.

#include <juce_audio_processors/juce_audio_processors.h>

namespace plx::id
{
// global
inline constexpr auto inGain = "inGain", outGain = "outGain", mix = "mix", ceiling = "ceiling", polooyx = "polooyx";
// macros
inline constexpr auto aura = "aura", glitch = "glitch", space = "space", dark = "dark", chaos = "chaos", body = "body";
// tune
inline constexpr auto tuneOn = "tuneOn", key = "key", scale = "scale", tuneAmount = "tuneAmount", retune = "retune", formant = "formant";
// eq
inline constexpr auto eqOn = "eqOn", hpf = "hpf", lpf = "lpf", lowFreq = "lowFreq", lowGain = "lowGain", lmFreq = "lmFreq", lmGain = "lmGain",
                      hmFreq = "hmFreq", hmGain = "hmGain", highFreq = "highFreq", highGain = "highGain";
// de-esser
inline constexpr auto deessOn = "deessOn", deessFreq = "deessFreq", deessThresh = "deessThresh", deessRange = "deessRange";
// compressor
inline constexpr auto compOn = "compOn", compThresh = "compThresh", compRatio = "compRatio", compAttack = "compAttack",
                      compRelease = "compRelease", compMakeup = "compMakeup";
// saturation
inline constexpr auto satOn = "satOn", satMode = "satMode", satDrive = "satDrive", satMix = "satMix", satTone = "satTone", satOut = "satOut";
// glitch
inline constexpr auto glitchOn = "glitchOn", glitchAmount = "glitchAmount", glitchRate = "glitchRate", glitchSmart = "glitchSmart",
                      glitchStutter = "glitchStutter", glitchReverse = "glitchReverse", glitchGate = "glitchGate", glitchTape = "glitchTape",
                      glitchPitch = "glitchPitch", glitchMix = "glitchMix";
// space
inline constexpr auto spaceOn = "spaceOn", revSize = "revSize", revDecay = "revDecay", revPredelay = "revPredelay", revDamp = "revDamp",
                      revMix = "revMix", dlyDiv = "dlyDiv", dlyFeedback = "dlyFeedback", dlyMix = "dlyMix", dlyPingPong = "dlyPingPong",
                      dlyHp = "dlyHp", dlyLp = "dlyLp", duck = "duck";
// stereo / layers
inline constexpr auto width = "width", movement = "movement", microPitch = "microPitch", shadow = "shadow";
} // namespace plx::id

namespace plx
{
inline juce::StringArray keyNames()   { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
inline juce::StringArray scaleNames() { return { "Chromatic", "Major", "Minor", "Harmonic Minor", "Pentatonic" }; }
inline juce::StringArray satModeNames() { return { "TUBE", "TAPE", "DIGITAL", "CRUSH", "ALIEN" }; }
inline juce::StringArray glitchRateNames() { return { "1/2", "1/4", "1/8", "1/16", "1/32" }; }
inline juce::StringArray delayDivNames() { return { "1/32", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2" }; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using F = AudioParameterFloat; using B = AudioParameterBool; using C = AudioParameterChoice;
    AudioProcessorValueTreeState::ParameterLayout l;
    int v = 1;   // parameter version hint for VST3/AU

    auto pct = [] (float val, int) { return String (roundToInt (val)) + "%"; };
    auto db  = [] (float val, int) { return String (val, 1) + " dB"; };
    auto hz  = [] (float val, int) { return val >= 1000.0f ? String (val / 1000.0f, 2) + " kHz" : String (roundToInt (val)) + " Hz"; };
    auto ms  = [] (float val, int) { return String (val, val < 10 ? 1 : 0) + " ms"; };
    auto st  = [] (float val, int) { return (val > 0 ? "+" : "") + String (val, 1) + " st"; };

    auto fl = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> fmt, const char* unit = "")
    {
        l.add (std::make_unique<F> (ParameterID { id, v }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (fmt).withLabel (unit)));
    };
    auto freq = [] (float lo, float hi) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (std::sqrt (lo * hi)); return r; };
    auto lin  = [] (float lo, float hi, float step = 0.0f) { return NormalisableRange<float> (lo, hi, step); };

    // global
    fl (id::inGain,  "Input Gain",  lin (-24, 24, 0.1f), 0.0f, db);
    fl (id::outGain, "Output Gain", lin (-24, 12, 0.1f), 0.0f, db);
    fl (id::mix,     "Mix",         lin (0, 100, 0.1f), 100.0f, pct);
    fl (id::ceiling, "Ceiling",     lin (-12, 0, 0.1f), -0.3f, db);
    l.add (std::make_unique<B> (ParameterID { id::polooyx, v }, "POLOOYX Mode", true));

    // macros
    fl (id::aura,   "AURA",   lin (0, 100, 0.1f), 30.0f, pct);
    fl (id::glitch, "GLITCH", lin (0, 100, 0.1f), 15.0f, pct);
    fl (id::space,  "SPACE",  lin (0, 100, 0.1f), 35.0f, pct);
    fl (id::dark,   "DARK",   lin (0, 100, 0.1f), 40.0f, pct);
    fl (id::chaos,  "CHAOS",  lin (0, 100, 0.1f), 10.0f, pct);
    fl (id::body,   "BODY",   lin (0, 100, 0.1f), 50.0f, pct);

    // tune
    l.add (std::make_unique<B> (ParameterID { id::tuneOn, v }, "Tune On", true));
    l.add (std::make_unique<C> (ParameterID { id::key, v }, "Key", keyNames(), 0));
    l.add (std::make_unique<C> (ParameterID { id::scale, v }, "Scale", scaleNames(), 0));
    fl (id::tuneAmount, "Tune Amount", lin (0, 100, 0.1f), 80.0f, pct);
    fl (id::retune,     "Retune Speed", lin (0, 100, 0.1f), 20.0f, [] (float val, int) { return String (roundToInt (400.0f * (val / 100.0f) * (val / 100.0f))) + " ms"; });
    fl (id::formant,    "Formant", lin (-12, 12, 0.01f), 0.0f, st);

    // eq
    l.add (std::make_unique<B> (ParameterID { id::eqOn, v }, "EQ On", true));
    fl (id::hpf,      "HPF",           freq (20, 500), 90.0f, hz);
    fl (id::lpf,      "LPF",           freq (3000, 20000), 19000.0f, hz);
    fl (id::lowFreq,  "Low Freq",      freq (60, 400), 160.0f, hz);
    fl (id::lowGain,  "Low Gain",      lin (-12, 12, 0.1f), 0.0f, db);
    fl (id::lmFreq,   "Low-Mid Freq",  freq (150, 1500), 380.0f, hz);
    fl (id::lmGain,   "Low-Mid Gain",  lin (-12, 12, 0.1f), -1.5f, db);
    fl (id::hmFreq,   "High-Mid Freq", freq (1000, 8000), 3200.0f, hz);
    fl (id::hmGain,   "High-Mid Gain", lin (-12, 12, 0.1f), 2.0f, db);
    fl (id::highFreq, "High Freq",     freq (5000, 16000), 10000.0f, hz);
    fl (id::highGain, "High Gain",     lin (-12, 12, 0.1f), 1.5f, db);

    // de-esser
    l.add (std::make_unique<B> (ParameterID { id::deessOn, v }, "De-Esser On", true));
    fl (id::deessFreq,   "De-Ess Freq",   freq (3000, 12000), 6500.0f, hz);
    fl (id::deessThresh, "De-Ess Thresh", lin (-60, 0, 0.1f), -30.0f, db);
    fl (id::deessRange,  "De-Ess Range",  lin (0, 24, 0.1f), 8.0f, db);

    // compressor
    l.add (std::make_unique<B> (ParameterID { id::compOn, v }, "Comp On", true));
    fl (id::compThresh,  "Comp Threshold", lin (-48, 0, 0.1f), -20.0f, db);
    { NormalisableRange<float> r (1, 20, 0.01f); r.setSkewForCentre (4.0f);
      fl (id::compRatio, "Comp Ratio", r, 3.5f, [] (float val, int) { return String (val, 1) + ":1"; }); }
    { NormalisableRange<float> r (0.1f, 100, 0.01f); r.setSkewForCentre (8.0f); fl (id::compAttack, "Comp Attack", r, 6.0f, ms); }
    { NormalisableRange<float> r (10, 1000, 0.1f); r.setSkewForCentre (120.0f); fl (id::compRelease, "Comp Release", r, 120.0f, ms); }
    fl (id::compMakeup,  "Comp Makeup", lin (0, 24, 0.1f), 4.0f, db);

    // saturation
    l.add (std::make_unique<B> (ParameterID { id::satOn, v }, "Saturation On", true));
    l.add (std::make_unique<C> (ParameterID { id::satMode, v }, "Saturation Mode", satModeNames(), 0));
    fl (id::satDrive, "Drive",      lin (0, 100, 0.1f), 25.0f, pct);
    fl (id::satMix,   "Sat Mix",    lin (0, 100, 0.1f), 60.0f, pct);
    fl (id::satTone,  "Sat Tone",   lin (-100, 100, 0.1f), 0.0f, pct);
    fl (id::satOut,   "Sat Output", lin (-24, 12, 0.1f), 0.0f, db);

    // glitch
    l.add (std::make_unique<B> (ParameterID { id::glitchOn, v }, "Glitch On", true));
    fl (id::glitchAmount,  "Glitch Base Amount", lin (0, 100, 0.1f), 0.0f, pct);
    l.add (std::make_unique<C> (ParameterID { id::glitchRate, v }, "Glitch Rate", glitchRateNames(), 3));
    l.add (std::make_unique<B> (ParameterID { id::glitchSmart, v }, "Smart Glitch", true));
    fl (id::glitchStutter, "Stutter",        lin (0, 100, 0.1f), 70.0f, pct);
    fl (id::glitchReverse, "Reverse",        lin (0, 100, 0.1f), 40.0f, pct);
    fl (id::glitchGate,    "Gate",           lin (0, 100, 0.1f), 45.0f, pct);
    fl (id::glitchTape,    "Tape Stop",      lin (0, 100, 0.1f), 30.0f, pct);
    fl (id::glitchPitch,   "Pitch Variation", lin (0, 100, 0.1f), 20.0f, pct);
    fl (id::glitchMix,     "Glitch Mix",     lin (0, 100, 0.1f), 100.0f, pct);

    // space
    l.add (std::make_unique<B> (ParameterID { id::spaceOn, v }, "Space On", true));
    fl (id::revSize,     "Reverb Size",     lin (0, 100, 0.1f), 55.0f, pct);
    { NormalisableRange<float> r (0.3f, 12.0f, 0.01f); r.setSkewForCentre (2.5f);
      fl (id::revDecay, "Reverb Decay", r, 2.6f, [] (float val, int) { return String (val, 2) + " s"; }); }
    fl (id::revPredelay, "Reverb Pre-Delay", lin (0, 250, 0.1f), 22.0f, ms);
    fl (id::revDamp,     "Reverb Damping",  lin (0, 100, 0.1f), 55.0f, pct);
    fl (id::revMix,      "Reverb Level",    lin (0, 100, 0.1f), 10.0f, pct);
    l.add (std::make_unique<C> (ParameterID { id::dlyDiv, v }, "Delay Time", delayDivNames(), 5));
    fl (id::dlyFeedback, "Delay Feedback",  lin (0, 90, 0.1f), 28.0f, pct);
    fl (id::dlyMix,      "Delay Level",     lin (0, 100, 0.1f), 6.0f, pct);
    l.add (std::make_unique<B> (ParameterID { id::dlyPingPong, v }, "Ping Pong", true));
    fl (id::dlyHp,       "Delay HP",        freq (20, 2000), 320.0f, hz);
    fl (id::dlyLp,       "Delay LP",        freq (1000, 16000), 6000.0f, hz);
    fl (id::duck,        "Ducking",         lin (0, 100, 0.1f), 45.0f, pct);

    // stereo / layers
    fl (id::width,      "Width",       lin (0, 200, 0.1f), 110.0f, pct);
    fl (id::movement,   "Movement",    lin (0, 100, 0.1f), 10.0f, pct);
    fl (id::microPitch, "Micro Pitch", lin (0, 100, 0.1f), 15.0f, pct);
    fl (id::shadow,     "Shadow",      lin (0, 100, 0.1f), 30.0f, pct);
    return l;
}
} // namespace plx
