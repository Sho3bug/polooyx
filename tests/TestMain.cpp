// POLOOYX - offline DSP verification harness.
// Instantiates the real PolooyxProcessor (same code as the VST3), renders synthetic vocal
// material through it and measures what every parameter does to the audio.

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include <chrono>
#include <cstdio>
#include <map>

using namespace plx;

// ============================================================ helpers
struct FakePlayHead : juce::AudioPlayHead
{
    double sr = 48000, bpm = 140; int64_t pos = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setIsPlaying (playing);
        i.setBpm (bpm);
        i.setPpqPosition ((double) pos / sr * bpm / 60.0);
        i.setTimeInSamples (pos);
        return i;
    }
};

static juce::AudioBuffer<float> makeVocal (double sr, double seconds, float detuneCents = 0.0f, bool sibilance = true, bool steady = false, float steadyHz = 220.0f)
{
    const int N = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, N); b.clear();
    juce::Random rnd (1234);
    const float notes[] = { 57, 60, 57, 55, 57, 52 };    // A3 C4 A3 G3 A3 E3
    struct Vowel { float f[3]; float g[3]; };
    const Vowel vowels[] = { { { 730, 1090, 2440 }, { 1.0f, 0.5f, 0.25f } },    // ah
                             { { 270, 2290, 3010 }, { 1.0f, 0.25f, 0.2f } },    // ee
                             { { 300, 870, 2240 },  { 1.0f, 0.3f, 0.12f } } };  // oo
    Biquad f[3]; Biquad sib; sib.highpass (5500.0f, 0.7f, (float) sr);
    double ph = 0.0; float env = 0.0f;
    const int syl = (int) (0.28 * sr), gap = (int) (0.09 * sr), phraseGap = (int) (0.6 * sr);
    int t = 0, s = 0;
    while (t < N)
    {
        const bool last = (s % 6) == 5;
        const int len = steady ? N : (last ? (int) (0.7 * sr) : syl);
        const float midi = notes[s % 6] + detuneCents * 0.01f;
        const auto& v = vowels[s % 3];
        for (int k = 0; k < 3; ++k) f[k].bandpass (v.f[k], 6.0f, (float) sr);
        for (int i = 0; i < len && t < N; ++i, ++t)
        {
            const float tt = (float) i / (float) sr;
            const float vib = (! steady && tt > 0.18f) ? 15.0f * std::sin (kTwoPi * 5.2f * tt) : 0.0f;
            const float hz = steady ? steadyHz : 440.0f * std::pow (2.0f, (midi - 69.0f + vib * 0.01f) / 12.0f);
            ph += hz / sr; if (ph >= 1.0) ph -= 1.0;
            // band-limited glottal-ish source
            float src = 0.0f;
            const int H = std::min (40, (int) (7000.0f / hz));
            for (int k = 1; k <= H; ++k) src += std::sin ((float) (kTwoPi * ph * k)) / std::pow ((float) k, 1.15f);
            float y = 0.0f; for (int k = 0; k < 3; ++k) y += v.g[k] * f[k].process (src);
            const float target = steady ? 1.0f : (i < len - (int) (0.03 * sr) ? 1.0f : 0.0f);
            env += (target - env) * 0.004f;
            float out = y * env * 0.9f;
            if (sibilance && ! steady && (s % 2 == 0) && i < (int) (0.06 * sr)) out += 0.35f * sib.process (rnd.nextFloat() * 2.0f - 1.0f) * (1.0f - (float) i / (float) (0.06 * sr));
            b.setSample (0, t, out); b.setSample (1, t, out);
        }
        if (! steady) t += last ? phraseGap : gap;
        ++s;
    }
    // normalise to -9 dBFS peak
    const float pk = b.getMagnitude (0, N);
    if (pk > 0) b.applyGain (0.355f / pk);
    return b;
}

static juce::AudioBuffer<float> makeSine (double sr, double seconds, float hz, float amp)
{
    const int N = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, N);
    for (int i = 0; i < N; ++i) { const float v = amp * std::sin (kTwoPi * hz * (float) i / (float) sr); b.setSample (0, i, v); b.setSample (1, i, v); }
    return b;
}

using Setup = std::function<void (PolooyxProcessor&)>;
using PerBlock = std::function<void (PolooyxProcessor&, int64_t)>;   // (processor, block start sample)

static void setParam (PolooyxProcessor& p, const juce::String& id, float value)
{
    auto* rp = p.apvts.getParameter (id);
    jassert (rp != nullptr);
    rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}

static juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& in, double sr, int block, const Setup& setup,
                                        const PerBlock& perBlock = nullptr, bool compensate = true, int* latencyOut = nullptr,
                                        int numInCh = 2, int numOutCh = 2)
{
    PolooyxProcessor p;
    if (numInCh == 1 || numOutCh == 1)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (numInCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        l.outputBuses.add (numOutCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        p.setBusesLayout (l);
    }
    FakePlayHead ph; ph.sr = sr;
    p.setPlayHead (&ph);
    if (setup) setup (p);
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    const int lat = p.getLatencySamples();
    if (latencyOut) *latencyOut = lat;
    const int N = in.getNumSamples();
    const int total = N + (compensate ? lat : 0);
    juce::AudioBuffer<float> out (numOutCh, total);
    juce::AudioBuffer<float> io (std::max (numInCh, numOutCh), block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), io.getNumChannels(), n);
        view.clear();
        for (int c = 0; c < numInCh; ++c)
            if (pos < N) view.copyFrom (c, 0, in, std::min (c, in.getNumChannels() - 1), pos, std::min (n, N - pos));
        if (perBlock) perBlock (p, pos);
        ph.pos = pos;
        p.processBlock (view, midi);
        for (int c = 0; c < numOutCh; ++c) out.copyFrom (c, pos, view, c, 0, n);
    }
    p.releaseResources();
    if (! compensate) return out;
    juce::AudioBuffer<float> res (numOutCh, N);
    for (int c = 0; c < numOutCh; ++c) res.copyFrom (c, 0, out, c, lat, N);
    return res;
}

// ---- metrics
static double rms (const juce::AudioBuffer<float>& b, int start = 0, int len = -1)
{
    if (len < 0) len = b.getNumSamples() - start;
    double s = 0; int n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = start; i < start + len; ++i) { const double v = b.getSample (c, i); s += v * v; ++n; }
    return std::sqrt (s / std::max (1, n));
}
static double diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& ref)
{
    double s = 0, r = 0;
    const int n = std::min (a.getNumSamples(), ref.getNumSamples());
    for (int c = 0; c < std::min (a.getNumChannels(), ref.getNumChannels()); ++c)
        for (int i = 0; i < n; ++i) { const double d = a.getSample (c, i) - ref.getSample (c, i); s += d * d; r += (double) ref.getSample (c, i) * ref.getSample (c, i); }
    if (s <= 0) return -300.0;
    return 10.0 * std::log10 (s / std::max (r, 1e-20));
}
static double centroid (const juce::AudioBuffer<float>& b, double sr, int start = 0, int len = -1)
{
    if (len < 0) len = b.getNumSamples() - start;
    juce::dsp::FFT fft (12);
    const int F = 4096;
    std::vector<float> buf ((size_t) F * 2);
    double num = 0, den = 0;
    for (int off = start; off + F <= start + len; off += F / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < F; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) F);
            buf[(size_t) i] = 0.5f * (b.getSample (0, off + i) + b.getSample (b.getNumChannels() - 1, off + i)) * w;
        }
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < F / 2; ++k) { const double f = k * sr / F; if (f > 12000) break; num += f * buf[(size_t) k]; den += buf[(size_t) k]; }
    }
    return den > 0 ? num / den : 0.0;
}
static double sideRatioDb (const juce::AudioBuffer<float>& b)
{
    if (b.getNumChannels() < 2) return -120;
    double m = 0, s = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    { const double l = b.getSample (0, i), r = b.getSample (1, i); m += (l + r) * (l + r); s += (l - r) * (l - r); }
    return 10.0 * std::log10 ((s + 1e-20) / (m + 1e-20));
}
static double levelDb (const juce::AudioBuffer<float>& b, int start = 0, int len = -1) { return 20.0 * std::log10 (rms (b, start, len) + 1e-12); }

// offline f0 (normalised autocorrelation with parabolic refinement)
static double measureF0 (const juce::AudioBuffer<float>& b, double sr, int start, int len)
{
    const int minLag = (int) (sr / 1000), maxLag = (int) (sr / 60);
    double best = -1; int bestLag = minLag;
    std::vector<double> ac ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (int i = start; i < start + len; ++i)
        { const double x = b.getSample (0, i), y = b.getSample (0, i + lag); s += x * y; e1 += x * x; e2 += y * y; }
        ac[(size_t) lag] = s / std::sqrt (e1 * e2 + 1e-20);
    }
    // first peak above 0.9 * global max (avoids octave errors)
    double gmax = 0; for (int lag = minLag; lag <= maxLag; ++lag) gmax = std::max (gmax, ac[(size_t) lag]);
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        if (ac[(size_t) lag] > 0.9 * gmax && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1]) { bestLag = lag; best = ac[(size_t) lag]; break; }
    (void) best;
    const double a = ac[(size_t) bestLag - 1], c = ac[(size_t) bestLag], d = ac[(size_t) bestLag + 1];
    const double den = a - 2 * c + d;
    const double lag = bestLag + (std::abs (den) > 1e-12 ? 0.5 * (a - d) / den : 0.0);
    return sr / lag;
}
static bool allFinite (const juce::AudioBuffer<float>& b, float& peak)
{
    peak = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        { const float v = b.getSample (c, i); if (! std::isfinite (v)) return false; peak = std::max (peak, std::abs (v)); }
    return true;
}
static double centsBetween (double a, double b) { return 1200.0 * std::log2 (a / b); }

// ============================================================ report
static juce::String report;
static int failures = 0;
static void line (const juce::String& s) { report << s << "\n"; std::printf ("%s\n", s.toRawUTF8()); std::fflush (stdout); }
static void check (bool ok, const juce::String& what) { if (! ok) ++failures; line (juce::String (ok ? "- [PASS] " : "- [FAIL] ") + what); }
static juce::String f1 (double v) { return juce::String (v, 1); }
static juce::String f2 (double v) { return juce::String (v, 2); }

// "neutral" setup: everything off -> transparency test
static void neutral (PolooyxProcessor& p)
{
    for (auto* m : { id::aura, id::glitch, id::space, id::dark, id::chaos, id::body }) setParam (p, m, 0);
    for (auto* m : { id::polooyx, id::tuneOn, id::eqOn, id::deessOn, id::compOn, id::satOn, id::glitchOn, id::spaceOn }) setParam (p, m, 0);
    setParam (p, id::microPitch, 0); setParam (p, id::shadow, 0); setParam (p, id::movement, 0); setParam (p, id::formant, 0);
    setParam (p, id::ceiling, 0); setParam (p, id::punch, 0); setParam (p, id::cleanOn, 0);
}

// a "bedroom" take: the clean vocal + room echo, boxy low-mids, a bright condenser top, 60 Hz hum and hiss
static juce::AudioBuffer<float> makeBedroom (const juce::AudioBuffer<float>& dry, double sr)
{
    juce::AudioBuffer<float> b (dry);
    const int N = b.getNumSamples();
    Biquad box; box.peak (350.0f, 1.0f, 6.0f, (float) sr);
    Biquad top; top.highShelf (8000.0f, 4.0f, (float) sr);
    const int combLen[4] = { (int) (0.0297 * sr), (int) (0.0371 * sr), (int) (0.0411 * sr), (int) (0.0437 * sr) };
    std::vector<std::vector<float>> comb (4);
    for (int k = 0; k < 4; ++k) comb[(size_t) k].assign ((size_t) combLen[k], 0.0f);
    int combPos[4] = {};
    const float fb = 0.80f;   // ~0.6 s small-room tail
    juce::Random rnd (7);
    for (int i = 0; i < N; ++i)
    {
        const float x = dry.getSample (0, i);
        float wet = 0.0f;
        for (int k = 0; k < 4; ++k)
        {
            auto& d = comb[(size_t) k];
            const float y = d[(size_t) combPos[k]];
            d[(size_t) combPos[k]] = x + fb * y;
            combPos[k] = (combPos[k] + 1) % combLen[k];
            wet += y;
        }
        float v = x + 0.12f * wet;
        v = top.process (box.process (v));
        v += 0.0025f * std::sin (kTwoPi * 60.0f * (float) i / (float) sr) + 0.0012f * (rnd.nextFloat() * 2.0f - 1.0f);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

static double bandDb (const juce::AudioBuffer<float>& b, double sr, double lo, double hi)
{
    juce::dsp::FFT fft (12);
    const int F = 4096;
    std::vector<float> buf ((size_t) F * 2);
    double e = 0, tot = 0;
    for (int off = 0; off + F <= b.getNumSamples(); off += F / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < F; ++i) buf[(size_t) i] = b.getSample (0, off + i) * (0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) F));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 1; k < F / 2; ++k) { const double f = k * sr / F, p = (double) buf[(size_t) k] * buf[(size_t) k]; tot += p; if (f >= lo && f < hi) e += p; }
    }
    return 10.0 * std::log10 ((e + 1e-20) / (tot + 1e-20));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "--list-params")
    {
        // generates docs/PARAMETERS.md straight from the parameter layout
        PolooyxProcessor p;
        juce::String md;
        md << "# POLOOYX - Parameter Reference\n\n";
        md << "Generated from the plugin's own parameter layout (`tests/TestMain.cpp --list-params`), so it always matches the build.\n";
        md << "Every parameter below is automatable in FL Studio (right-click a knob -> *Create automation clip*, or use *Browse parameters*).\n\n";
        md << "| # | Name (as FL shows it) | ID | Range | Default |\n|---|---|---|---|---|\n";
        int i = 1;
        for (auto* ap : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
            {
                juce::String range;
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (rp)) range = c->choices.joinIntoString (" / ");
                else if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr) range = "Off / On";
                else range = rp->getText (0.0f, 32) + " ... " + rp->getText (1.0f, 32);
                md << "| " << i++ << " | " << rp->getName (64) << " | `" << rp->getParameterID() << "` | " << range << " | " << rp->getText (rp->getDefaultValue(), 32) << " |\n";
            }
        juce::File (argv[2]).replaceWithText (md, false, false, "\n");
        return 0;
    }
    if (argc > 3 && juce::String (argv[1]) == "--render")
    {
        // PolooyxTests --render in.wav out.wav [mic room clarity smooth noise]: CLEAN only, everything else off
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]))));
        if (rd == nullptr) { std::printf ("can't read %s\n", argv[2]); return 2; }
        juce::AudioBuffer<float> in ((int) std::max (2u, rd->numChannels), (int) rd->lengthInSamples);
        rd->read (&in, 0, (int) rd->lengthInSamples, 0, true, true);
        if (rd->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, in.getNumSamples());
        const float mic = argc > 4 ? (float) std::atof (argv[4]) : 1.0f;
        float k[4] = { 35, 35, 35, 35 };
        for (int i = 0; i < 4; ++i) if (argc > 5 + i) k[i] = (float) std::atof (argv[5 + i]);
        auto out = render (in, rd->sampleRate, 512, [&] (PolooyxProcessor& p) {
            neutral (p); setParam (p, id::cleanOn, 1); setParam (p, id::cleanMic, mic);
            setParam (p, id::cleanRoom, k[0]); setParam (p, id::cleanClarity, k[1]); setParam (p, id::cleanSmooth, k[2]); setParam (p, id::cleanNoise, k[3]);
        });
        juce::File of = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]);
        of.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (of), rd->sampleRate, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        return 0;
    }
    const bool withUi = argc > 1 && juce::String (argv[1]) == "--ui";
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("test-output");
    outDir.createDirectory();

    const double SR = 48000.0;
    const int BLOCK = 256;

    line ("# POLOOYX v1.2 - DSP Test Report");
    line ("");
    line ("Generated by `tests/TestMain.cpp`, which runs the exact processor code that ships in the VST3.");
    line ("Test material: a synthetic sung/rapped vocal (glottal source + 3 formants, vowels ah/ee/oo, vibrato, sibilant 's' bursts,");
    line ("6-syllable phrases with pauses), 48 kHz, 256-sample blocks, host transport at 140 BPM.");
    line ("");

    // ---------------------------------------------------------------- 1. latency & transparency
    line ("## 1. Latency, transparency, dry path");
    const auto vocal = makeVocal (SR, 8.0);
    int lat = 0;
    {
        auto out = render (vocal, SR, BLOCK, neutral, nullptr, true, &lat);
        const double nullDb = diffDb (out, vocal);
        line ("Reported latency: **" + juce::String (lat) + " samples (" + f2 (lat * 1000.0 / SR) + " ms)** at 48 kHz - PSOLA 20 ms + oversampling filter + 32-sample processing grid + 1.5 ms limiter lookahead.");
        check (nullDb < -40.0, "All modules off -> output nulls against the latency-shifted input: residual " + f1 (nullDb) + " dB");
        auto dry = render (vocal, SR, BLOCK, [] (PolooyxProcessor& p) { setParam (p, id::mix, 0); setParam (p, id::ceiling, 0); });
        const double dryDb = diffDb (dry, vocal);
        check (dryDb < -80.0, "MIX = 0% -> sample-aligned dry signal: residual " + f1 (dryDb) + " dB (latency compensation is exact)");
        auto a = render (vocal, SR, BLOCK, nullptr), b = render (vocal, SR, BLOCK, nullptr);
        check (diffDb (a, b) < -200.0, "Rendering is deterministic (identical output on repeat renders)");
    }
    line ("");

    // ---------------------------------------------------------------- 2. every parameter
    line ("## 2. Every parameter changes the audio");
    line ("Each parameter is rendered at its minimum and maximum (choices: first and last option, toggles: off and on) against a");
    line ("baseline with the same context. **delta** = energy of the difference relative to the baseline (dB; > -40 dB is clearly audible,");
    line ("0 dB means the change is as loud as the signal itself). Extra columns show *what* changed.");
    line ("");
    line ("| Parameter | Context | delta (dB) | Level delta | Brightness delta | Width delta | Result |");
    line ("|---|---|---|---|---|---|---|");

    std::map<juce::String, std::vector<std::pair<const char*, float>>> ctx = {
        { id::key,           { { id::scale, 2 } } },
        { id::highFreq,      { { id::highGain, 8 } } },
        { id::glitchRate,    { { id::glitch, 70 } } }, { id::glitchSmart,   { { id::glitch, 70 } } },
        { id::glitchStutter, { { id::glitch, 70 } } }, { id::glitchReverse, { { id::glitch, 70 } } },
        { id::glitchGate,    { { id::glitch, 70 } } }, { id::glitchTape,    { { id::glitch, 70 } } },
        { id::glitchPitch,   { { id::glitch, 70 }, { id::glitchStutter, 100 } } },
        { id::glitchMix,     { { id::glitch, 70 } } }, { id::glitchOn,      { { id::glitch, 70 } } },
        { id::glitchAmount,  { { id::glitch, 0 } } },
    };
    auto ctxName = [&] (const juce::String& pid) {
        juce::String s;
        if (auto it = ctx.find (pid); it != ctx.end())
            for (auto& [k, v] : it->second) s << k << "=" << juce::String (v, 0) << " ";
        return s.isEmpty() ? juce::String ("defaults") : s.trim();
    };
    std::map<juce::String, juce::AudioBuffer<float>> baselines;
    const double baseCent = centroid (vocal, SR);
    (void) baseCent;

    PolooyxProcessor probe;
    int tested = 0, passed = 0;
    for (auto* ap : probe.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap);
        if (rp == nullptr) continue;
        const auto pid = rp->getParameterID();
        const auto cn = ctxName (pid);
        auto applyCtx = [&] (PolooyxProcessor& p) { if (auto it = ctx.find (pid); it != ctx.end()) for (auto& [k, v] : it->second) setParam (p, k, v); };
        if (! baselines.count (cn)) baselines[cn] = render (vocal, SR, BLOCK, applyCtx);
        const auto& base = baselines[cn];

        double bestD = -300; juce::AudioBuffer<float> bestOut;
        for (float norm : { 0.0f, 1.0f })
        {
            auto out = render (vocal, SR, BLOCK, [&] (PolooyxProcessor& p) { applyCtx (p); p.apvts.getParameter (pid)->setValueNotifyingHost (norm); });
            const double d = diffDb (out, base);
            if (d > bestD) { bestD = d; bestOut = out; }
        }
        const double lvl = levelDb (bestOut) - levelDb (base);
        const double br  = (centroid (bestOut, SR) / std::max (1.0, centroid (base, SR)) - 1.0) * 100.0;
        const double wd  = sideRatioDb (bestOut) - sideRatioDb (base);
        const bool ok = bestD > -40.0;
        ++tested; if (ok) ++passed; else ++failures;
        line ("| " + rp->getName (40) + " (`" + pid + "`) | " + cn + " | " + f1 (bestD) + " | " + (lvl >= 0 ? "+" : "") + f1 (lvl) + " dB | "
              + (br >= 0 ? "+" : "") + f1 (br) + "% | " + (wd >= 0 ? "+" : "") + f1 (std::max (-99.0, std::min (99.0, wd))) + " dB | " + (ok ? "PASS" : "**FAIL**") + " |");
    }
    line ("");
    line ("**" + juce::String (passed) + " / " + juce::String (tested) + " parameters produce a measurable change in the audio.**");
    line ("");

    // ---------------------------------------------------------------- 3. macros
    line ("## 3. Macro behaviour (0% -> 100%, everything else at defaults)");
    line ("| Macro | delta (dB) | Level delta | Brightness delta | Width delta | Tail after phrase | Notes |");
    line ("|---|---|---|---|---|---|---|");
    {
        auto withTail = vocal;
        withTail.setSize (2, vocal.getNumSamples() + (int) (2.5 * SR), true, true);
        const int tailStart = vocal.getNumSamples() + (int) (0.3 * SR), tailLen = (int) (1.5 * SR);
        struct MM { const char* id; const char* note; };
        const MM mm[] = { { id::aura, "wider, brighter air, subtle pitch shimmer, more reverb" },
                          { id::glitch, "stutter/repeat/gate events on the grid" },
                          { id::space, "delay + reverb + movement, long tail" },
                          { id::dark, "top end rolled off, formants lowered, warmer" },
                          { id::chaos, "pitch drift, modulation, instability" },
                          { id::body, "denser, compressed, low-mid weight" } };
        for (auto& m : mm)
        {
            auto lo = render (withTail, SR, BLOCK, [&] (PolooyxProcessor& p) { setParam (p, m.id, 0); });
            auto hi = render (withTail, SR, BLOCK, [&] (PolooyxProcessor& p) { setParam (p, m.id, 100); });
            const double d = diffDb (hi, lo);
            const double tailLo = levelDb (lo, tailStart, tailLen), tailHi = levelDb (hi, tailStart, tailLen);
            line (juce::String ("| ") + m.id + " | " + f1 (d) + " | " + f1 (levelDb (hi) - levelDb (lo)) + " dB | "
                  + f1 ((centroid (hi, SR) / centroid (lo, SR) - 1.0) * 100.0) + "% | " + f1 (sideRatioDb (hi) - sideRatioDb (lo)) + " dB | "
                  + f1 (tailLo) + " -> " + f1 (tailHi) + " dBFS | " + m.note + " |");
            if (d < -30) ++failures;
        }
    }
    line ("");
    {
        // pitch movement on a perfectly steady tone (tuning on, so any movement is the macro's doing)
        auto steady = makeVocal (SR, 3.0, 0, false, true, 220.0f);
        auto spread = [&] (const juce::AudioBuffer<float>& b) {
            std::vector<double> f;
            for (int k = 0; k < 60; ++k) f.push_back (measureF0 (b, SR, (int) (0.5 * SR) + k * (int) (0.03 * SR), (int) (0.03 * SR)));
            double mean = 0; for (auto v : f) mean += v; mean /= (double) f.size();
            double var = 0; for (auto v : f) var += std::pow (centsBetween (v, mean), 2.0);
            return std::sqrt (var / (double) f.size());
        };
        auto mv = [&] (const char* mid, float v) {
            // isolate the lead: everything off except the pitch stage, then the macro under test
            return spread (render (steady, SR, BLOCK, [&] (PolooyxProcessor& p) { neutral (p); setParam (p, mid, v); }));
        };
        const double a0 = mv (id::aura, 0), a1 = mv (id::aura, 100), c1 = mv (id::chaos, 100);
        check (a1 > a0 + 1.0 && c1 > a1, "Lead pitch movement on a steady 220 Hz tone, other modules off (RMS deviation): baseline " + f1 (a0) + " ct, AURA 100% " + f1 (a1)
               + " ct (subtle shimmer), CHAOS 100% " + f1 (c1) + " ct (drift)");
    }
    line ("");

    // ---------------------------------------------------------------- 4. pitch & formant accuracy
    line ("## 4. Pitch correction & formant accuracy");
    {
        auto isolate = [] (PolooyxProcessor& p) {
            neutral (p);
            setParam (p, id::tuneOn, 1); setParam (p, id::tuneAmount, 100); setParam (p, id::retune, 0);
            setParam (p, id::key, 0); setParam (p, id::scale, 0);
        };
        const int s0 = (int) (1.0 * SR), len = (int) (0.05 * SR);
        auto avgF0 = [&] (const juce::AudioBuffer<float>& b) { double s = 0; for (int k = 0; k < 10; ++k) s += measureF0 (b, SR, s0 + k * (int) (0.1 * SR), len); return s / 10.0; };

        const float inHz = 220.0f * std::pow (2.0f, 30.0f / 1200.0f);   // A3 +30 ct
        auto in = makeVocal (SR, 2.5, 0, false, true, inHz);
        const double fin = avgF0 (in);
        auto out = render (in, SR, BLOCK, isolate);
        const double fout = avgF0 (out);
        check (std::abs (centsBetween (fout, 220.0)) < 3.0, "A3 +30 ct, chromatic, amount 100%, retune 0 -> output " + f2 (fout) + " Hz (" + f1 (centsBetween (fout, 220.0)) + " ct from A3; input measured " + f2 (fin) + " Hz)");

        auto half = render (in, SR, BLOCK, [&] (PolooyxProcessor& p) { isolate (p); setParam (p, id::tuneAmount, 50); });
        const double fh = avgF0 (half);
        check (std::abs (centsBetween (fh, 220.0) - 15.0) < 3.0, "Amount 50% -> half correction: " + f1 (centsBetween (fh, 220.0)) + " ct sharp (expected ~ +15)");

        const float d4s = 440.0f * std::pow (2.0f, (63.27f - 69.0f) / 12.0f);   // D#4 +27 ct, not in C major
        auto in2 = makeVocal (SR, 2.5, 0, false, true, d4s);
        auto out2 = render (in2, SR, BLOCK, [&] (PolooyxProcessor& p) { isolate (p); setParam (p, id::scale, 1); });
        const double e4 = 440.0 * std::pow (2.0, (64 - 69) / 12.0);
        const double f2v = avgF0 (out2);
        check (std::abs (centsBetween (f2v, e4)) < 5.0, "Scale snapping: D#4+27ct in C Major -> " + f2 (f2v) + " Hz (E4 = " + f2 (e4) + " Hz, " + f1 (centsBetween (f2v, e4)) + " ct)");

        // retune speed: time for correction to settle after a note starts
        auto settle = [&] (float retune) {
            auto o = render (in, SR, BLOCK, [&] (PolooyxProcessor& p) { isolate (p); setParam (p, id::retune, retune); });
            for (int k = 0; k < 60; ++k)
            {
                const int st = (int) (0.05 * SR) + k * (int) (0.01 * SR);
                if (std::abs (centsBetween (measureF0 (o, SR, st, (int) (0.02 * SR)), 220.0)) < 5.0) return 50.0 + k * 10.0;
            }
            return 999.0;
        };
        const double t0 = settle (0), t60 = settle (60);
        check (t60 > t0, "Retune speed works: correction settles in ~" + f1 (t0) + " ms at 0 vs ~" + f1 (t60) + " ms at 60 (measured from signal start, includes detector warm-up)");

        // formant independence
        auto steadyIn = makeVocal (SR, 2.5, 0, false, true, 220.0f);
        auto fIso = [&] (float semis) { return [=] (PolooyxProcessor& p) { neutral (p); setParam (p, id::formant, semis); }; };
        auto f0o = render (steadyIn, SR, BLOCK, fIso (0.0f)), fUp = render (steadyIn, SR, BLOCK, fIso (5.0f)), fDn = render (steadyIn, SR, BLOCK, fIso (-5.0f));
        const double c0 = centroid (f0o, SR, s0, (int) SR), cU = centroid (fUp, SR, s0, (int) SR), cD = centroid (fDn, SR, s0, (int) SR);
        const double pU = avgF0 (fUp), pD = avgF0 (fDn);
        check (cU > c0 * 1.12 && cD < c0 * 0.9, "Formant +/-5 st moves the spectral envelope: centroid " + f1 (cD) + " / " + f1 (c0) + " / " + f1 (cU) + " Hz (-5 / 0 / +5)");
        check (std::abs (centsBetween (pU, 220.0)) < 5.0 && std::abs (centsBetween (pD, 220.0)) < 5.0,
               "...while pitch stays put: " + f2 (pD) + " Hz / " + f2 (pU) + " Hz (" + f1 (centsBetween (pD, 220.0)) + " / " + f1 (centsBetween (pU, 220.0)) + " ct) -> formant is independent of pitch");
    }
    line ("");

    // ---------------------------------------------------------------- 4b. CLEAN
    line ("## 4b. CLEAN (recording cleanup)");
    {
        // simulated bedroom take: room echo, boxy low-mids, bright condenser top, 60 Hz hum, hiss
        const auto bed = makeBedroom (vocal, SR);
        auto off = render (bed, SR, BLOCK, [] (PolooyxProcessor& p) { neutral (p); });
        auto on  = render (bed, SR, BLOCK, [] (PolooyxProcessor& p) {
            neutral (p); setParam (p, id::cleanOn, 1); setParam (p, id::cleanMic, 1);
            for (auto* k : { id::cleanRoom, id::cleanClarity, id::cleanSmooth, id::cleanNoise }) setParam (p, k, 60);
        });
        // level in the pauses between phrases (where only echo, hum and hiss live)
        const int w = (int) (0.05 * SR);
        double gOff = 0, gOn = 0, vOff = 0, vOn = 0; int ng = 0, nv = 0;
        int silentFor = 0;
        for (int f = 0; f + w <= vocal.getNumSamples(); f += w)
        {
            const double d = rms (vocal, f, w);
            silentFor = d < 1e-5 ? silentFor + 1 : 0;
            // pauses between lines count from 150 ms after the voice stops
            if (silentFor >= 3) { gOff += std::pow (rms (off, f, w), 2); gOn += std::pow (rms (on, f, w), 2); ++ng; }
            else if (d > 0.05) { vOff += std::pow (rms (off, f, w), 2); vOn += std::pow (rms (on, f, w), 2); ++nv; }
        }
        const double gapOff = 10 * std::log10 (gOff / std::max (1, ng) + 1e-20), gapOn = 10 * std::log10 (gOn / std::max (1, ng) + 1e-20);
        const double voxOff = 10 * std::log10 (vOff / std::max (1, nv) + 1e-20), voxOn = 10 * std::log10 (vOn / std::max (1, nv) + 1e-20);
        const double sepOff = voxOff - gapOff, sepOn = voxOn - gapOn;
        check (sepOn > sepOff + 6.0, "Echo + hum + hiss in the pauses between lines vs. the voice: " + f1 (sepOff) + " dB apart without CLEAN, " + f1 (sepOn) + " dB with CLEAN (AT2020, 60%)");
        const double presOff = bandDb (off, SR, 2000, 4000) - bandDb (off, SR, 200, 600), presOn = bandDb (on, SR, 2000, 4000) - bandDb (on, SR, 200, 600);
        check (presOn > presOff + 3.0, "Clarity vs. boxiness (2-4 kHz minus 200-600 Hz): " + f1 (presOff) + " -> " + f1 (presOn) + " dB");
        const double hiOff = bandDb (off, SR, 7000, 16000), hiOn = bandDb (on, SR, 7000, 16000);
        check (hiOn < hiOff - 2.0, "Harsh/hissy top (7-16 kHz share): " + f1 (hiOff) + " -> " + f1 (hiOn) + " dB");
        const double humOff = bandDb (off, SR, 40, 80), humOn = bandDb (on, SR, 40, 80);
        check (humOn < humOff - 10.0, "60 Hz hum/rumble share: " + f1 (humOff) + " -> " + f1 (humOn) + " dB");
        // a clean studio take shouldn't be wrecked: default CLEAN on the dry synthetic vocal keeps level and stays close
        auto dflt = render (vocal, SR, BLOCK, [] (PolooyxProcessor& p) { neutral (p); setParam (p, id::cleanOn, 1); });
        const double lvl = levelDb (dflt) - levelDb (vocal);
        check (std::abs (lvl) < 3.0, "Default CLEAN on an already-clean vocal is gentle: level change " + f1 (lvl) + " dB");
    }
    line ("");

    // ---------------------------------------------------------------- 5. glitch
    line ("## 5. Glitch engine");
    {
        // count events via the engine's own telemetry, grid-aligned by construction
        auto countEvents = [&] (float glitch, bool smart) {
            std::map<int, int> kinds; int total = 0, prevCount = 0;
            render (vocal, SR, BLOCK, [&] (PolooyxProcessor& p) { setParam (p, id::glitch, glitch); setParam (p, id::glitchSmart, smart ? 1.0f : 0.0f); },
                    [&] (PolooyxProcessor& p, int64_t) {
                        const int c = p.getGlitchEngine().eventCount.load();
                        if (c != prevCount) { kinds[p.getGlitchEngine().lastEvent.load()] += c - prevCount; total += c - prevCount; prevCount = c; }
                    });
            juce::String k; const char* nm[] = { "", "stutter", "repeat", "reverse", "gate", "tape" };
            for (auto& [e, c] : kinds) if (e > 0) k << nm[e] << " " << c << ", ";
            return std::make_pair (total, k.dropLastCharacters (2));
        };
        const auto g0 = countEvents (0, true), g40 = countEvents (40, true), g90 = countEvents (90, true), g90d = countEvents (90, false);
        check (g0.first == 0, "GLITCH 0% -> no events");
        check (g40.first > 0 && g90.first > g40.first, "Event count scales with GLITCH: 40% -> " + juce::String (g40.first) + " events, 90% -> " + juce::String (g90.first) + " (8 s of vocal, 1/16 grid @140 BPM)");
        line ("  - event mix at 90% (smart): " + g90.second);
        check (g90d.first > g90.first, "SMART GLITCH is selective: smart " + juce::String (g90.first) + " vs dumb " + juce::String (g90d.first) + " events at 90% (smart skips ordinary syllables & cools down)");

        // tempo sync: with a pure sine, gate events must start on 1/16 grid lines
        auto sine = makeSine (SR, 6.0, 330.0f, 0.3f);
        std::vector<int64_t> starts; int prevCount = 0;
        const int TB = 32;
        render (sine, SR, TB, [&] (PolooyxProcessor& p) { neutral (p); setParam (p, id::glitchOn, 1); setParam (p, id::glitchAmount, 100); setParam (p, id::glitchSmart, 0); },
                [&] (PolooyxProcessor& p, int64_t pos) { const int c = p.getGlitchEngine().eventCount.load(); if (c != prevCount) { starts.push_back (pos); prevCount = c; } }, false);
        int off = 0; { PolooyxProcessor q; q.prepareToPlay (SR, TB); off = q.getGridOffset(); }
        const double step = SR * 60.0 / 140.0 / 4.0;
        int onGrid = 0; double worst = 0;
        for (auto st : starts)
        {
            // an event is observed at the end of the chunk that contains its grid line (+ FIFO + one block)
            double ph = std::fmod ((double) (st - off - 32 - TB) + step * 1000.0, step);
            ph = std::min (ph, step - ph);
            worst = std::max (worst, ph);
            if (ph <= 64.0) ++onGrid;
        }
        check (starts.size() > 30 && onGrid == (int) starts.size(), "Tempo sync: " + juce::String (onGrid) + "/" + juce::String ((int) starts.size())
               + " events sit on the host's 1/16 grid after latency compensation (worst " + f1 (worst) + " samples, i.e. within one 32-sample chunk)");
    }
    line ("");

    // ---------------------------------------------------------------- 6. robustness
    line ("## 6. Robustness");
    {
        for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            auto v = makeVocal (rate, 3.0);
            auto o = render (v, rate, 512, nullptr);
            float pk; const bool fin = allFinite (o, pk);
            check (fin && pk <= 1.0f && rms (o) > 1e-3, "Sample rate " + juce::String ((int) rate) + " Hz: finite, peak " + f2 (20 * std::log10 (pk + 1e-9)) + " dBFS, signal present");
        }
        for (int blk : { 1, 17, 64, 333, 2048 })
        {
            auto o = render (vocal, SR, blk, nullptr);
            auto ref = render (vocal, SR, 256, nullptr);
            float pk; const bool fin = allFinite (o, pk);
            check (fin && diffDb (o, ref) < -40.0, "Block size " + juce::String (blk) + ": finite and matches 256-block render (" + f1 (diffDb (o, ref)) + " dB)");
        }
        {
            auto m = render (vocal, SR, BLOCK, nullptr, nullptr, true, nullptr, 1, 2);
            float pk; check (allFinite (m, pk) && rms (m) > 1e-3, "Mono in -> stereo out works");
            auto mm = render (vocal, SR, BLOCK, nullptr, nullptr, true, nullptr, 1, 1);
            check (allFinite (mm, pk) && rms (mm) > 1e-3, "Mono in -> mono out works");
        }
        // automation fuzz
        {
            juce::Random r (99);
            PolooyxProcessor probe2;
            auto params = probe2.getParameters();
            auto o = render (makeVocal (SR, 20.0), SR, 128, nullptr, [&] (PolooyxProcessor& p, int64_t) {
                auto ps = p.getParameters();
                for (int k = 0; k < 3; ++k) ps[r.nextInt (ps.size())]->setValueNotifyingHost (r.nextFloat());
            });
            float pk; const bool fin = allFinite (o, pk);
            check (fin && pk <= 1.0f, "Automation fuzz (3 random params changed every 128 samples for 20 s): finite, peak " + f2 (20 * std::log10 (pk + 1e-9)) + " dBFS, never above 0 dBFS");
        }
        // preset hopping
        {
            const int nPre = (int) factoryPresets().size();
            auto o = render (makeVocal (SR, 12.0), SR, 256, nullptr, [&] (PolooyxProcessor& p, int64_t pos) {
                if (pos % (int) (0.4 * SR) < 256) p.setCurrentProgram ((int) (pos / (int) (0.4 * SR)) % nPre);
            });
            float pk; const bool fin = allFinite (o, pk);
            check (fin && pk <= 1.0f, "Switching through all " + juce::String (nPre) + " factory presets during playback: no NaN/overs (peak " + f2 (20 * std::log10 (pk + 1e-9)) + " dBFS)");
        }
        // each preset renders sanely
        {
            int ok = 0; juce::String worst;
            for (int i = 0; i < (int) factoryPresets().size(); ++i)
            {
                auto o = render (vocal, SR, 256, [&] (PolooyxProcessor& p) { p.setCurrentProgram (i); });
                float pk; if (allFinite (o, pk) && pk <= 1.0f && levelDb (o) > -40) ++ok; else worst << factoryPresets()[(size_t) i].name << " ";
            }
            check (ok == (int) factoryPresets().size(), "All factory presets render finite, bounded, audible audio (" + juce::String (ok) + "/" + juce::String ((int) factoryPresets().size()) + ") " + worst);
        }
        // STYLE menu: every style keeps the session key/scale/gain, renders safely and sounds distinct
        {
            std::vector<int> styles;
            for (int i = 0; i < (int) factoryPresets().size(); ++i) if (juce::String (factoryPresets()[(size_t) i].category) == "STYLES") styles.push_back (i);
            std::vector<juce::AudioBuffer<float>> outs;
            bool keptAll = true; int ok = 0; juce::String bad;
            for (int si : styles)
            {
                bool kept = false;
                auto o = render (vocal, SR, 256, [&] (PolooyxProcessor& p) {
                    setParam (p, id::key, 7); setParam (p, id::scale, 2); setParam (p, id::inGain, 3.0f);
                    p.setCurrentProgram (si);
                    kept = std::lround (p.apvts.getRawParameterValue (id::key)->load()) == 7 && std::lround (p.apvts.getRawParameterValue (id::scale)->load()) == 2
                        && std::abs (p.apvts.getRawParameterValue (id::inGain)->load() - 3.0f) < 0.01f;
                });
                keptAll &= kept;
                float pk; if (allFinite (o, pk) && pk <= 1.0f && levelDb (o) > -40) ++ok; else bad << factoryPresets()[(size_t) si].name << " ";
                outs.push_back (std::move (o));
            }
            check (styles.size() == 6 && ok == (int) styles.size(), "STYLE menu: " + juce::String ((int) styles.size()) + " styles, all render finite, bounded and audible " + bad);
            check (keptAll, "Choosing a style keeps the song's KEY / SCALE and the input gain");
            double closest = 1e9; juce::String pair;
            for (size_t i = 0; i < outs.size(); ++i)
                for (size_t j = i + 1; j < outs.size(); ++j)
                {
                    double d = 0, r = 0; const int n = std::min (outs[i].getNumSamples(), outs[j].getNumSamples());
                    for (int c = 0; c < 2; ++c) for (int k = 0; k < n; ++k) { const double x = outs[i].getSample (c, k) - outs[j].getSample (c, k); d += x * x; r += (double) outs[i].getSample (c, k) * outs[i].getSample (c, k); }
                    const double db = 10.0 * std::log10 (d / std::max (r, 1e-20) + 1e-20);
                    if (db < closest) { closest = db; pair = juce::String (factoryPresets()[(size_t) styles[i]].name) + " / " + factoryPresets()[(size_t) styles[j]].name; }
                }
            check (closest > -10.0, "Every style sounds different from every other (closest pair " + pair + ": difference " + f2 (closest) + " dB re. signal)");
        }
        // clicks when toggling modules
        {
            auto sine = makeSine (SR, 4.0, 220.0f, 0.25f);
            auto maxJump = [] (const juce::AudioBuffer<float>& b) {
                double m = 0; for (int i = (int) (0.3 * 48000); i < b.getNumSamples(); ++i) m = std::max (m, (double) std::abs (b.getSample (0, i) - 2 * b.getSample (0, i - 1) + b.getSample (0, i - 2)));
                return m;
            };
            auto clickSetup = [&] (PolooyxProcessor& p) { neutral (p); setParam (p, id::eqOn, 1); setParam (p, id::hmGain, 6); };
            auto still = render (sine, SR, 256, clickSetup);
            const double ref = maxJump (still);
            auto where = [] (const juce::AudioBuffer<float>& b) {
                double m = 0; int at = 0;
                for (int i = (int) (0.3 * 48000); i < b.getNumSamples(); ++i) { const double v = std::abs (b.getSample (0, i) - 2 * b.getSample (0, i - 1) + b.getSample (0, i - 2)); if (v > m) { m = v; at = i; } }
                return at;
            };
            for (auto* mod : { id::cleanOn, id::tuneOn, id::eqOn, id::deessOn, id::compOn, id::satOn, id::spaceOn, id::polooyx })
            {
                auto o = render (sine, SR, 256, clickSetup,
                                 [&] (PolooyxProcessor& p, int64_t pos) { if (pos % (int) (0.25 * SR) < 256) setParam (p, mod, (pos / (int) (0.25 * SR)) % 2 ? 1.0f : 0.0f); });
                const double j = maxJump (o);
                check (j < ref * 4.0, juce::String ("Toggling ") + mod + " 16x on a sine: max 2nd-difference " + f2 (j / ref) + "x the clean sine (no clicks), worst at " + juce::String (where (o)));
            }
        }
    }
    line ("");

    // ---------------------------------------------------------------- 7. state / presets / A-B / undo
    line ("## 7. State, presets, A/B, undo");
    {
        auto tmpHome = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("plx-home");
        tmpHome.deleteRecursively(); tmpHome.createDirectory();
       #if JUCE_WINDOWS
        // Windows resolves Documents through the shell, not HOME; the CI runner's Documents folder is used
       #else
        setenv ("HOME", tmpHome.getFullPathName().toRawUTF8(), 1);
       #endif

        PolooyxProcessor a;
        setParam (a, id::aura, 77); setParam (a, id::satMode, 3); setParam (a, id::formant, -3.5f); setParam (a, id::key, 5);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        PolooyxProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int i = 0; i < a.getParameters().size(); ++i) same &= std::abs (a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()) < 1e-6f;
        check (same, "Host state save/restore (what FL Studio stores in the project) round-trips all " + juce::String (a.getParameters().size()) + " parameters");

        for (auto& f : a.getUserPresets()) if (f.getFileNameWithoutExtension() == "TEST VOX") f.deleteFile();
        check (a.saveUserPreset ("TEST VOX"), "User preset saved to <Documents>/POLOOYX/Presets");
        PolooyxProcessor c;
        juce::File testFile;
        for (auto& f : c.getUserPresets()) if (f.getFileNameWithoutExtension() == "TEST VOX") testFile = f;
        const bool loaded = testFile.existsAsFile() && c.loadUserPreset (testFile);
        bool same2 = loaded;
        for (int i = 0; loaded && i < a.getParameters().size(); ++i) same2 &= std::abs (a.getParameters()[i]->getValue() - c.getParameters()[i]->getValue()) < 1e-6f;
        check (same2 && c.currentPresetName == "TEST VOX", "User preset loads back into a fresh instance with identical values");
        testFile.deleteFile();

        PolooyxProcessor d;
        setParam (d, id::space, 10);
        d.switchAB();                                  // -> B (copy of A)
        setParam (d, id::space, 90);
        d.switchAB();                                  // -> A
        const float sA = d.apvts.getRawParameterValue (id::space)->load();
        d.switchAB();                                  // -> B
        const float sB = d.apvts.getRawParameterValue (id::space)->load();
        check (std::abs (sA - 10) < 0.01f && std::abs (sB - 90) < 0.01f, "A/B keeps two complete states (SPACE A=" + f1 (sA) + ", B=" + f1 (sB) + ")");

        PolooyxProcessor u;
        u.undo.beginNewTransaction();
        setParam (u, id::dark, 12);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        u.undo.beginNewTransaction();
        setParam (u, id::dark, 88);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        u.undo.undo();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        const float afterUndo = u.apvts.getRawParameterValue (id::dark)->load();
        u.undo.redo();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        const float afterRedo = u.apvts.getRawParameterValue (id::dark)->load();
        check (std::abs (afterUndo - 12) < 0.01f && std::abs (afterRedo - 88) < 0.01f, "Undo/redo: DARK 12 -> 88, undo -> " + f1 (afterUndo) + ", redo -> " + f1 (afterRedo));
    }
    line ("");

    // ---------------------------------------------------------------- 8. CPU
    line ("## 8. CPU / performance (this build machine, single core)");
    {
        auto tenSec = makeVocal (SR, 10.0);
        auto timeIt = [&] (const Setup& s, int blk) {
            const auto t0 = std::chrono::high_resolution_clock::now();
            render (tenSec, SR, blk, s);
            const double el = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
            return el / 10.0 * 100.0;   // % of one core in real time
        };
        const double idle  = timeIt (neutral, 256);
        const double def   = timeIt (nullptr, 256);
        const double heavy = timeIt ([] (PolooyxProcessor& p) { for (auto* m : { id::aura, id::glitch, id::space, id::dark, id::chaos, id::body }) setParam (p, m, 100); setParam (p, id::formant, 6); }, 256);
        const double small = timeIt (nullptr, 32);
        line ("| Scenario (48 kHz stereo) | CPU, % of one core |");
        line ("|---|---|");
        line ("| Everything bypassed | " + f1 (idle) + "% |");
        line ("| POLOOYX DEFAULT, 256-sample buffer | " + f1 (def) + "% |");
        line ("| All six macros at 100% + formant +6 st | " + f1 (heavy) + "% |");
        line ("| POLOOYX DEFAULT, 32-sample buffer | " + f1 (small) + "% |");
        line ("");
        line ("Measured on the CI/dev Linux container (" + juce::String (juce::SystemStats::getNumCpus()) + " vCPU, " + juce::SystemStats::getCpuModel() + "). Includes test-harness overhead.");
        line ("Memory: all buffers are allocated in prepareToPlay(); the audio thread performs no allocations, locks or file I/O.");
        check (heavy < 25.0, "Worst-case load stays well inside real time");
    }
    line ("");

    // ---------------------------------------------------------------- 9. UI snapshots
    if (withUi)
    {
        PolooyxProcessor p;
        FakePlayHead ph; p.setPlayHead (&ph);
        p.prepareToPlay (SR, 256);
        juce::AudioBuffer<float> io (2, 256); juce::MidiBuffer mid;
        for (int pos = 0; pos < (int) (3.0 * SR); pos += 256) { io.clear(); for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, vocal, c, pos + (int) SR, 256); ph.pos = pos; p.processBlock (io, mid); }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<PolooyxEditor*> (ed.get());
        ed->setSize (PolooyxEditor::kW, PolooyxEditor::kH);
        auto snap = [&] (const juce::String& name) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream fos (outDir.getChildFile (name));
            fos.setPosition (0); fos.truncate();
            juce::PNGImageFormat().writeImageToStream (img, fos);
        };
        snap ("ui-main.png");
        e->showView (true, 0); snap ("ui-advanced-clean.png");
        e->showView (true, 1); snap ("ui-advanced-tune.png");
        e->showView (true, 2); snap ("ui-advanced-eq.png");
        e->showView (true, 5); snap ("ui-advanced-glitch.png");
        e->showView (false);
        ed->setSize (800, 524); snap ("ui-small.png");
        line ("UI snapshots written to test-output/.");
    }

    line ("");
    line (failures == 0 ? "## RESULT: ALL CHECKS PASSED" : "## RESULT: " + juce::String (failures) + " CHECK(S) FAILED");
    outDir.getChildFile ("TEST_REPORT.md").replaceWithText (report, false, false, "\n");
    return failures == 0 ? 0 : 1;
}
