#include "PluginEditor.h"
#include "Presets.h"
#include <complex>

using namespace plx;
using namespace plx::ui;

// ============================================================ Knob
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& title,
            bool big, const juce::String& sub, bool bipolar)
    : name (title), subtitle (sub), isBig (big), param (s.getParameter (paramId))
{
    jassert (param != nullptr);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    slider.setMouseDragSensitivity (big ? 260 : 200);
    slider.setVelocityBasedMode (false);
    slider.getProperties().set ("big", big);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    slider.addListener (this);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    slider.setTooltip (param->getName (64));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    const int top = isBig ? 26 : 16;
    const int bottom = isBig ? (subtitle.isNotEmpty() ? 38 : 22) : 16;
    r.removeFromTop (top);
    r.removeFromBottom (bottom);
    const int sz = std::min (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (sz, sz));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (slider.isMouseOverOrDragging() ? col::hot : col::text);
    g.setFont (font (isBig ? 15.0f : 10.5f, true));
    g.drawFittedText (name, r.removeFromTop (isBig ? 22 : 14), juce::Justification::centred, 1);

    const auto valText = param->getCurrentValueAsText();
    if (isBig)
    {
        auto b = r.removeFromBottom (subtitle.isNotEmpty() ? 38 : 22);
        g.setColour (col::accent);
        g.setFont (font (13.0f, true));
        g.drawFittedText (valText, b.removeFromTop (18), juce::Justification::centred, 1);
        g.setColour (col::dim);
        g.setFont (font (10.0f));
        g.drawFittedText (subtitle, b, juce::Justification::centred, 1);
    }
    else
    {
        g.setColour (slider.isMouseOverOrDragging() ? col::accent : col::mid);
        g.setFont (font (10.0f));
        g.drawFittedText (valText, r.removeFromBottom (14), juce::Justification::centred, 1);
    }
}

// ============================================================ Choice / Toggle
Choice::Choice (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& title) : name (title)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, paramId, box);
}
void Choice::resized() { auto r = getLocalBounds(); r.removeFromLeft (std::min (70, getWidth() / 3)); box.setBounds (r); }
void Choice::paint (juce::Graphics& g)
{
    g.setColour (col::mid); g.setFont (font (10.5f, true));
    g.drawFittedText (name, getLocalBounds().removeFromLeft (std::min (70, getWidth() / 3) - 6), juce::Justification::centredRight, 1);
}

Toggle::Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& title)
{
    btn.setButtonText (title);
    addAndMakeVisible (btn);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, paramId, btn);
}

// ============================================================ Visualizer
void Visualizer::tick()
{
    const int w = proc.scopeWrite.load();
    for (int i = 0; i < PolooyxProcessor::kScopeSize; ++i)
    {
        const int k = (w + i) % PolooyxProcessor::kScopeSize;
        in[(size_t) i]  = proc.scopeIn[(size_t) k].load (std::memory_order_relaxed);
        out[(size_t) i] = proc.scopeOut[(size_t) k].load (std::memory_order_relaxed);
    }
    phase += 0.016f;
    if (proc.getGlitchEngine().active.load()) glitchFlash = 1.0f; else glitchFlash *= 0.85f;
    repaint();
}

void Visualizer::paint (juce::Graphics& g)
{
    auto& st = proc.apvts;
    auto P = [&] (const char* pid) { return st.getRawParameterValue (pid)->load() * 0.01f; };
    const float aura = P (id::aura), glitch = P (id::glitch), space = P (id::space), dark = P (id::dark), chaos = P (id::chaos), body = P (id::body);

    auto r = getLocalBounds().toFloat();
    g.setColour (col::bg2);
    g.fillRoundedRectangle (r, 6.0f);

    // faint grid
    g.setColour (col::line.withAlpha (0.5f));
    for (int i = 1; i < 12; ++i) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 12.0f), r.getY() + 8, r.getBottom() - 8);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 8, r.getRight() - 8);

    const float cx = r.getCentreX(), cy = r.getCentreY();

    // SPACE: stereo field halo expands; PSYCHE-ish drift via CHAOS/AURA
    {
        float rms = 0.0f; for (auto v : out) rms += v * v; rms = std::sqrt (rms / (float) out.size());
        const float base = r.getHeight() * (0.18f + 0.30f * space) * (1.0f + 1.6f * rms);
        for (int k = 0; k < 3; ++k)
        {
            const float rr = base * (1.0f + 0.35f * (float) k) * (1.0f + 0.04f * std::sin (phase * (1.0f + (float) k) + (float) k));
            const float wx = rr * (1.4f + 1.4f * space + 0.3f * aura);
            g.setColour (col::accent.withAlpha ((0.06f + 0.05f * space) / (1.0f + (float) k)));
            g.drawEllipse (cx - wx, cy - rr, wx * 2.0f, rr * 2.0f, 1.0f);
        }
    }

    auto wave = [&] (const std::array<float, PolooyxProcessor::kScopeSize>& data, float gain, float distort, bool fragment) {
        juce::Path p;
        const int N = (int) data.size();
        const int segs = 24;
        float segOff[segs] {}, segY[segs] {};
        if (fragment)
            for (int s = 0; s < segs; ++s)
                if (rnd.nextFloat() < glitch * (0.15f + 0.6f * glitchFlash))
                { segOff[s] = (rnd.nextFloat() - 0.5f) * 40.0f * glitch; segY[s] = (rnd.nextFloat() - 0.5f) * 18.0f * glitch; }
        for (int i = 0; i < N; ++i)
        {
            const int s = i * segs / N;
            float v = data[(size_t) i] * gain;
            if (distort > 0.0f) v = std::tanh (v * (1.0f + 5.0f * distort)) / std::tanh (1.0f + 5.0f * distort) * std::abs (gain) * 0.5f + v * 0.5f;
            const float wob = chaos * 6.0f * std::sin (phase * 3.0f + (float) i * 0.02f);
            const float x = r.getX() + 10.0f + (r.getWidth() - 20.0f) * (float) i / (float) (N - 1) + segOff[s];
            const float y = cy - v * r.getHeight() * 0.42f + segY[s] + wob;
            if (i == 0 || (fragment && s != (i - 1) * segs / N && segOff[s] != 0.0f)) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    g.setColour (col::dim.withAlpha (0.55f));
    g.strokePath (wave (in, 1.4f, 0.0f, false), juce::PathStrokeType (1.0f));

    auto outPath = wave (out, 1.4f, 0.0f, true);
    g.setColour (col::accent.withAlpha (0.12f + 0.08f * body));
    g.strokePath (outPath, juce::PathStrokeType (4.0f));
    g.setColour (col::hot.withAlpha (0.9f));
    g.strokePath (outPath, juce::PathStrokeType (1.2f + 0.8f * (glitchFlash > 0.2f ? 1.0f : 0.0f)));

    // DARK: vignette
    if (dark > 0.01f)
    {
        juce::ColourGradient vg (juce::Colours::transparentBlack, cx, cy, col::bg.withAlpha (0.75f * dark), r.getX(), cy, true);
        g.setGradientFill (vg);
        g.fillRoundedRectangle (r, 6.0f);
    }

    // labels
    g.setFont (font (10.0f, true));
    g.setColour (col::dim);  g.drawText ("IN", juce::Rectangle<float> (r.getX() + 12, r.getY() + 8, 40, 14), juce::Justification::left);
    g.setColour (col::text); g.drawText ("OUT", juce::Rectangle<float> (r.getX() + 36, r.getY() + 8, 40, 14), juce::Justification::left);

    static const char* evNames[] = { "", "STUTTER", "REPEAT", "REVERSE", "GATE", "TAPE STOP" };
    const int ev = juce::jlimit (0, 5, proc.getGlitchEngine().lastEvent.load());
    if (glitchFlash > 0.05f && ev > 0)
    {
        g.setColour (col::hot.withAlpha (glitchFlash));
        g.drawText (evNames[ev], juce::Rectangle<float> (r.getRight() - 170, r.getY() + 8, 158, 14), juce::Justification::right);
    }
    g.setColour (col::dim);
    g.drawText (juce::String (proc.hostBpm.load(), 1) + " BPM", juce::Rectangle<float> (r.getRight() - 170, r.getBottom() - 22, 158, 14), juce::Justification::right);

    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
}

// ============================================================ Meter
void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto label = r.removeFromBottom (14);
    g.setColour (col::dim); g.setFont (font (9.5f, true));
    g.drawText (title, label, juce::Justification::centred);
    r = r.withSizeKeepingCentre (8.0f, r.getHeight());
    g.setColour (col::bg2); g.fillRoundedRectangle (r, 2.0f);
    auto toY = [&] (float lin) { const float db = juce::jlimit (-60.0f, 3.0f, 20.0f * std::log10 (std::max (lin, 1.0e-6f))); return juce::jmap (db, -60.0f, 3.0f, r.getBottom(), r.getY()); };
    const float y = toY (level);
    g.setColour (level > 0.97f ? col::hot : col::accent.withAlpha (0.85f));
    g.fillRoundedRectangle (r.withTop (y), 2.0f);
    g.setColour (col::text); g.fillRect (r.getX(), toY (peak), r.getWidth(), 1.0f);
    g.setColour (col::line); g.drawRoundedRectangle (r, 2.0f, 1.0f);
}

// ============================================================ EQ curve
void EqCurve::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::bg2); g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::line); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    auto P = [&] (const char* pid) { return st.getRawParameterValue (pid)->load(); };
    const float fs = 48000.0f;
    plx::Biquad b[7];
    b[0].highpass (P (id::hpf), 0.5412f, fs); b[1].highpass (P (id::hpf), 1.3066f, fs);
    b[2].lowShelf (P (id::lowFreq), P (id::lowGain), fs);
    b[3].peak (P (id::lmFreq), 0.9f, P (id::lmGain), fs);
    b[4].peak (P (id::hmFreq), 0.8f, P (id::hmGain), fs);
    b[5].highShelf (P (id::highFreq), P (id::highGain), fs);
    b[6].lowpass (P (id::lpf), 0.707f, fs);

    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
        g.setColour (col::line); g.drawVerticalLine ((int) x, r.getY() + 4, r.getBottom() - 4);
    }
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX() + 4, r.getRight() - 4);

    juce::Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        std::complex<double> z = std::polar (1.0, -w), z2 = z * z, H = 1.0;
        for (auto& q : b)
            H *= ((double) q.b0 + (double) q.b1 * z + (double) q.b2 * z2) / (1.0 + (double) q.a1 * z + (double) q.a2 * z2);
        const float db = (float) (20.0 * std::log10 (std::max (1.0e-6, std::abs (H))));
        const float x = r.getX() + r.getWidth() * (float) i / 200.0f;
        const float y = juce::jmap (juce::jlimit (-18.0f, 18.0f, db), -18.0f, 18.0f, r.getBottom() - 6, r.getY() + 6);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col::accent.withAlpha (0.15f)); g.strokePath (p, juce::PathStrokeType (4.0f));
    g.setColour (col::accent); g.strokePath (p, juce::PathStrokeType (1.4f));
    g.setColour (col::dim); g.setFont (font (9.5f));
    g.drawText ("advanced base curve (macros add on top)", r.reduced (8, 4), juce::Justification::topRight);
}

// ============================================================ Page
void Page::resized()
{
    auto r = getLocalBounds().reduced (16, 10);
    const int knobW = 124, knobH = 112;
    for (auto& grp : groups)
    {
        r.removeFromTop (22);                                       // group title
        if (grp.note.isNotEmpty()) r.removeFromTop (18);
        if (! grp.controls.empty())
        {
            auto line = r.removeFromTop (28);
            for (auto& c : grp.controls)
            {
                const int w = dynamic_cast<Choice*> (c.get()) != nullptr ? 230 : 150;
                c->setBounds (line.removeFromLeft (w).reduced (0, 2));
                line.removeFromLeft (14);
            }
            r.removeFromTop (6);
        }
        if (grp.wide) { grp.wide->setBounds (r.removeFromTop (130)); r.removeFromTop (6); }
        const int perRow = std::max (1, r.getWidth() / knobW);   // wraps long groups onto a second row
        for (size_t i = 0; i < grp.knobs.size(); i += (size_t) perRow)
        {
            auto row = r.removeFromTop (knobH);
            for (size_t k = i; k < std::min (grp.knobs.size(), i + (size_t) perRow); ++k)
                grp.knobs[k]->setBounds (row.removeFromLeft (knobW));
        }
        r.removeFromTop (10);
    }
}

void Page::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (16, 10);
    int y = r.getY();
    for (auto& grp : groups)
    {
        g.setColour (col::accent); g.setFont (font (11.0f, true));
        g.drawText (grp.title, r.getX(), y, 400, 16, juce::Justification::left);
        g.setColour (col::line); g.drawHorizontalLine (y + 18, (float) r.getX(), (float) r.getRight());
        int h = 22;
        if (grp.note.isNotEmpty())
        {
            g.setColour (col::dim); g.setFont (font (10.5f));
            g.drawText (grp.note, r.getX(), y + 22, r.getWidth(), 14, juce::Justification::left);
            h += 18;
        }
        if (! grp.controls.empty()) h += 34;
        if (grp.wide) h += 136;
        const int perRow = std::max (1, r.getWidth() / 124);
        h += (int) ((grp.knobs.size() + (size_t) perRow - 1) / (size_t) perRow) * 112 + 10;
        y += h;
    }
}

// ============================================================ Editor
PolooyxEditor::PolooyxEditor (PolooyxProcessor& p)
    : AudioProcessorEditor (&p), proc (p), vis (p),
      inMeter (p.inPeak, "IN"), outMeter (p.outPeak, "OUT")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.setBounds (0, 0, kW, kH);
    content.addMouseListener (this, true);

    // ---- top bar
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &aBtn, &bBtn, &undoBtn, &redoBtn, &advBtn, &modeBtn })
        content.addAndMakeVisible (b);
    content.addAndMakeVisible (presetBox);
    modeBtn.setClickingTogglesState (true);
    modeBtn.getProperties().set ("pill", true);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id::polooyx, modeBtn);
    modeBtn.setTooltip ("Signature POLOOYX voicing: shadow layer, macro cross-links, ducked space, glitch echoes");

    rebuildPresetMenu();

    // ---- STYLE menu: one-click complete vocal voicings (the STYLES preset category)
    content.addAndMakeVisible (styleBox);
    styleBox.setTextWhenNothingSelected ("CHOOSE A STYLE");
    styleBox.setTooltip ("Complete vocal styles: tuning, tone, grit, layers and space set together. Your key, scale and gain are kept. Shape it further with the six macros.");
    {
        const auto& fp = factoryPresets();
        for (size_t i = 0; i < fp.size(); ++i)
            if (juce::String (fp[i].category) == "STYLES") styleBox.addItem (fp[i].name, (int) i + 1);
    }
    styleBox.onChange = [this] {
        const int sel = styleBox.getSelectedId();
        if (sel > 0 && juce::String (factoryPresets()[(size_t) sel - 1].name) != proc.currentPresetName) proc.loadFactoryPreset (sel - 1);
    };
    syncStyleBox();
    presetBox.onChange = [this] {
        const int sel = presetBox.getSelectedId();
        if (sel <= 0) return;
        if (sel < 1000) proc.loadFactoryPreset (sel - 1);
        else
        {
            auto files = proc.getUserPresets();
            if (juce::isPositiveAndBelow (sel - 1000, files.size())) proc.loadUserPreset (files[sel - 1000]);
        }
    };
    auto step = [this] (int d) {
        const int n = presetBox.getNumItems();
        if (n == 0) return;
        int idx = presetBox.getSelectedItemIndex();
        idx = (idx + d + n) % n;
        presetBox.setSelectedItemIndex (idx);
    };
    prevBtn.onClick = [step] { step (-1); };
    nextBtn.onClick = [step] { step (1); };

    saveBtn.onClick = [this] {
        saveDialog = std::make_unique<juce::AlertWindow> ("SAVE PRESET", "Name your preset. It's saved to Documents/POLOOYX/Presets.", juce::MessageBoxIconType::NoIcon, this);
        saveDialog->addTextEditor ("name", proc.currentPresetName == "POLOOYX DEFAULT" ? "MY VOCAL" : proc.currentPresetName);
        saveDialog->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
        saveDialog->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r) {
            if (r == 1 && saveDialog != nullptr)
            {
                const auto name = saveDialog->getTextEditorContents ("name");
                if (proc.saveUserPreset (name)) rebuildPresetMenu();
                else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "POLOOYX", "Couldn't save the preset. Check the name and that Documents is writable.");
            }
            saveDialog.reset();
        }), false);
    };

    aBtn.setClickingTogglesState (false); bBtn.setClickingTogglesState (false);
    aBtn.onClick = [this] { if (proc.activeAB != 0) proc.switchAB(); };
    bBtn.onClick = [this] { if (proc.activeAB != 1) proc.switchAB(); };
    aBtn.setTooltip ("A/B: two complete plugin states. Switching stores the current one.");
    bBtn.setTooltip (aBtn.getTooltip());
    undoBtn.onClick = [this] { proc.undo.undo(); };
    redoBtn.onClick = [this] { proc.undo.redo(); };
    advBtn.setClickingTogglesState (false);
    advBtn.onClick = [this] { setAdvanced (! advanced); };

    // ---- macros
    content.addAndMakeVisible (vis);
    struct M { const char* id; const char* name; const char* sub; };
    const M ms[] = {
        { id::aura,   "AURA",   "width  shimmer  air  haze" },
        { id::glitch, "GLITCH", "stutter  repeat  gate  pitch" },
        { id::space,  "SPACE",  "delay  reverb  movement" },
        { id::dark,   "DARK",   "filter  formant  warmth" },
        { id::chaos,  "CHAOS",  "drift  modulation  instability" },
        { id::body,   "BODY",   "weight  compression  density" },
    };
    for (auto& m : ms)
    {
        macros.push_back (std::make_unique<Knob> (proc.apvts, m.id, m.name, true, m.sub));
        content.addAndMakeVisible (*macros.back());
    }

    // ---- advanced
    struct C { const char* id; const char* label; };
    const C chips[] = { { id::tuneOn, "TUNE" }, { id::eqOn, "EQ" }, { id::deessOn, "DE-ESS" }, { id::compOn, "COMP" },
                        { id::satOn, "SATURATE" }, { id::glitchOn, "GLITCH" }, { id::spaceOn, "SPACE" } };
    for (auto& c : chips)
    {
        auto chip = std::make_unique<ChainChip>();
        chip->btn.setButtonText (c.label);
        chip->btn.setClickingTogglesState (true);
        chip->btn.setTooltip ("Click to bypass / enable this module");
        chip->att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, c.id, chip->btn);
        content.addChildComponent (chip->btn);
        chain.push_back (std::move (chip));
    }
    buildPages();

    // ---- bottom
    inKnob   = std::make_unique<Knob> (proc.apvts, id::inGain, "INPUT", false, juce::String(), true);
    mixKnob  = std::make_unique<Knob> (proc.apvts, id::mix, "MIX");
    outKnob  = std::make_unique<Knob> (proc.apvts, id::outGain, "OUTPUT", false, juce::String(), true);
    ceilKnob = std::make_unique<Knob> (proc.apvts, id::ceiling, "CEILING");
    punchKnob = std::make_unique<Knob> (proc.apvts, id::punch, "PUNCH");
    for (auto* c : std::initializer_list<juce::Component*> { inKnob.get(), mixKnob.get(), outKnob.get(), punchKnob.get(), ceilKnob.get(), &inMeter, &outMeter })
        content.addAndMakeVisible (c);

    content.painter = [this] (juce::Graphics& g) {
        g.fillAll (col::bg);
        // top bar
        g.setColour (col::bg2); g.fillRect (0, 0, kW, 60);
        g.setColour (col::line); g.drawHorizontalLine (60, 0, (float) kW);
        g.setColour (col::text); g.setFont (font (24.0f, true));
        g.drawText ("P O L O O Y X", 22, 10, 260, 26, juce::Justification::left);
        g.setColour (col::dim); g.setFont (font (10.0f, true));
        g.drawText ("VOCAL ENGINE   v1.1", 24, 36, 260, 14, juce::Justification::left);

        // bottom bar
        g.setColour (col::bg2); g.fillRect (0, 620, kW, 100);
        g.setColour (col::line); g.drawHorizontalLine (620, 0, (float) kW);
        g.setColour (col::dim); g.setFont (font (10.0f, true));
        g.drawText ("PITCH", 562, 636, 200, 14, juce::Justification::left);
        g.drawText ("GAIN REDUCTION", 562, 676, 200, 14, juce::Justification::left);
        g.setColour (col::text); g.setFont (font (13.0f, true));
        g.drawText (tunerText, 562, 651, 300, 18, juce::Justification::left);
        g.setColour (col::mid); g.setFont (font (11.5f, true));
        g.drawText (grText, 562, 691, 300, 16, juce::Justification::left);

        if (advanced)
        {
            g.setColour (col::dim); g.setFont (font (10.0f, true));
            g.drawText ("SIGNAL CHAIN", 22, 72, 120, 14, juce::Justification::left);
            g.setColour (col::line2);
            // arrows between chips
            for (size_t i = 0; i + 1 < chain.size(); ++i)
            {
                auto a = chain[i]->btn.getBounds(), b = chain[i + 1]->btn.getBounds();
                g.drawLine ((float) a.getRight() + 2, (float) a.getCentreY(), (float) b.getX() - 2, (float) b.getCentreY(), 1.0f);
            }
            g.setColour (col::dim); g.setFont (font (10.5f, true));
            g.drawText ("IN  >", chain.front()->btn.getX() - 50, chain.front()->btn.getY(), 46, 26, juce::Justification::right);
            g.drawText (">  WIDTH  >  LIMIT  >  OUT", chain.back()->btn.getRight() + 6, chain.back()->btn.getY(), 220, 26, juce::Justification::left);
        }
        else
        {
            g.setColour (col::dim); g.setFont (font (10.0f, true));
            g.drawText ("STYLE", 22, 318, 50, 14, juce::Justification::left);
            g.drawText ("PICK A STYLE, THEN SHAPE IT WITH THE SIX MACROS.   OPEN ADVANCED TO FINE-TUNE EVERY MODULE.", 330, 318, kW - 352, 14, juce::Justification::right);
        }
    };

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setResizeLimits (800, 524, 1760, 1152);
    setSize (kW, kH);
    setAdvanced (false);
    startTimerHz (30);
}

PolooyxEditor::~PolooyxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PolooyxEditor::buildPages()
{
    auto& s = proc.apvts;
    auto knob = [&] (const char* pid, const char* name, bool bip = false) { return std::make_unique<Knob> (s, pid, name, false, juce::String(), bip); };
    auto page = std::make_unique<Page>();

    auto addPage = [&] (const char* tabName) {
        auto t = std::make_unique<juce::TextButton> (tabName);
        t->setClickingTogglesState (false);
        const int idx = (int) tabs.size();
        t->onClick = [this, idx] { currentTab = idx; setAdvanced (true); };
        content.addChildComponent (*t);
        tabs.push_back (std::move (t));
        for (auto& g : page->groups)
        {
            for (auto& c : g.controls) page->addAndMakeVisible (*c);
            for (auto& k : g.knobs) page->addAndMakeVisible (*k);
            if (g.wide) page->addAndMakeVisible (*g.wide);
        }
        content.addChildComponent (*page);
        pages.push_back (std::move (page));
        page = std::make_unique<Page>();
    };
    auto group = [&] (const char* title) -> Page::Group& { page->groups.emplace_back(); page->groups.back().title = title; return page->groups.back(); };

    { auto& g = group ("PITCH CORRECTION  /  FORMANT");
      g.note = "Real-time YIN pitch tracking + pitch-synchronous grain shifting. Formant moves independently of pitch. DARK lowers formants on top of this.";
      g.controls.push_back (std::make_unique<Toggle> (s, id::tuneOn, "TUNE ON"));
      g.controls.push_back (std::make_unique<Choice> (s, id::key, "KEY"));
      g.controls.push_back (std::make_unique<Choice> (s, id::scale, "SCALE"));
      g.knobs.push_back (knob (id::tuneAmount, "AMOUNT"));
      g.knobs.push_back (knob (id::retune, "RETUNE SPEED"));
      g.knobs.push_back (knob (id::formant, "FORMANT", true));
      addPage ("TUNE"); }

    { auto& g = group ("4-BAND EQ  +  FILTERS");
      g.note = "This is the base curve. DARK, BODY, AURA and POLOOYX mode add their own moves on top in real time.";
      g.controls.push_back (std::make_unique<Toggle> (s, id::eqOn, "EQ ON"));
      g.wide = std::make_unique<EqCurve> (s);
      g.knobs.push_back (knob (id::hpf, "HPF"));
      g.knobs.push_back (knob (id::lowFreq, "LOW FREQ"));  g.knobs.push_back (knob (id::lowGain, "LOW GAIN", true));
      g.knobs.push_back (knob (id::lmFreq, "LO-MID FREQ")); g.knobs.push_back (knob (id::lmGain, "LO-MID GAIN", true));
      g.knobs.push_back (knob (id::hmFreq, "HI-MID FREQ")); g.knobs.push_back (knob (id::hmGain, "HI-MID GAIN", true));
      g.knobs.push_back (knob (id::highFreq, "HIGH FREQ")); g.knobs.push_back (knob (id::highGain, "HIGH GAIN", true));
      g.knobs.push_back (knob (id::lpf, "LPF"));
      addPage ("EQ"); }

    { auto& g1 = group ("DE-ESSER");
      g1.note = "Split-band: only the sibilant band is turned down. AURA adds air, so it also deepens the de-essing.";
      g1.controls.push_back (std::make_unique<Toggle> (s, id::deessOn, "DE-ESS ON"));
      g1.knobs.push_back (knob (id::deessFreq, "FREQ")); g1.knobs.push_back (knob (id::deessThresh, "THRESHOLD")); g1.knobs.push_back (knob (id::deessRange, "RANGE"));
      auto& g2 = group ("COMPRESSOR");
      g2.controls.push_back (std::make_unique<Toggle> (s, id::compOn, "COMP ON"));
      g2.knobs.push_back (knob (id::compThresh, "THRESHOLD")); g2.knobs.push_back (knob (id::compRatio, "RATIO"));
      g2.knobs.push_back (knob (id::compAttack, "ATTACK")); g2.knobs.push_back (knob (id::compRelease, "RELEASE"));
      g2.knobs.push_back (knob (id::compMakeup, "MAKEUP"));
      addPage ("DYNAMICS"); }

    { auto& g = group ("SATURATION  /  DAMAGE");
      g.note = "2x oversampled. DIGITAL and CRUSH add controlled digital artifacts. DARK, BODY, CHAOS and AURA all push drive.";
      g.controls.push_back (std::make_unique<Toggle> (s, id::satOn, "SATURATE ON"));
      g.controls.push_back (std::make_unique<Choice> (s, id::satMode, "MODE"));
      g.knobs.push_back (knob (id::satDrive, "DRIVE")); g.knobs.push_back (knob (id::satMix, "MIX"));
      g.knobs.push_back (knob (id::satTone, "TONE", true)); g.knobs.push_back (knob (id::satOut, "OUTPUT", true));
      addPage ("SATURATION"); }

    { auto& g = group ("GLITCH ENGINE  (tempo-synced, real buffer manipulation)");
      g.note = "The GLITCH macro adds probability on top of BASE AMOUNT. Weights below set which events get picked. SMART targets phrase ends, held notes and pauses.";
      g.controls.push_back (std::make_unique<Toggle> (s, id::glitchOn, "GLITCH ON"));
      g.controls.push_back (std::make_unique<Toggle> (s, id::glitchSmart, "SMART GLITCH"));
      g.controls.push_back (std::make_unique<Choice> (s, id::glitchRate, "RATE"));
      g.knobs.push_back (knob (id::glitchAmount, "BASE AMOUNT")); g.knobs.push_back (knob (id::glitchStutter, "STUTTER"));
      g.knobs.push_back (knob (id::glitchReverse, "REVERSE")); g.knobs.push_back (knob (id::glitchGate, "GATE"));
      g.knobs.push_back (knob (id::glitchTape, "TAPE STOP")); g.knobs.push_back (knob (id::glitchPitch, "PITCH VAR"));
      g.knobs.push_back (knob (id::glitchMix, "MIX"));
      addPage ("GLITCH"); }

    { auto& g1 = group ("REVERB");
      g1.note = "SPACE scales size, decay, pre-delay and level. DUCKING pulls the space down while the lead is singing.";
      g1.knobs.push_back (knob (id::revSize, "SIZE")); g1.knobs.push_back (knob (id::revDecay, "DECAY"));
      g1.knobs.push_back (knob (id::revPredelay, "PRE-DELAY")); g1.knobs.push_back (knob (id::revDamp, "DAMPING"));
      g1.knobs.push_back (knob (id::revMix, "LEVEL")); g1.knobs.push_back (knob (id::duck, "DUCKING"));
      g1.controls.push_back (std::make_unique<Toggle> (s, id::spaceOn, "SPACE ON"));
      auto& g2 = group ("DELAY");
      g2.controls.push_back (std::make_unique<Choice> (s, id::dlyDiv, "TIME"));
      g2.controls.push_back (std::make_unique<Toggle> (s, id::dlyPingPong, "PING PONG"));
      g2.knobs.push_back (knob (id::dlyFeedback, "FEEDBACK")); g2.knobs.push_back (knob (id::dlyMix, "LEVEL"));
      g2.knobs.push_back (knob (id::dlyHp, "HP FILTER")); g2.knobs.push_back (knob (id::dlyLp, "LP FILTER"));
      addPage ("SPACE"); }

    { auto& g = group ("STEREO  /  LAYERS   (the lead stays centred; these widen what surrounds it)");
      g.note = "MICRO PITCH = detuned L/R doubles. SHADOW = hidden filtered, driven layer under the lead. MOVEMENT = auto-pan of the space.";
      g.knobs.push_back (knob (id::width, "WIDTH")); g.knobs.push_back (knob (id::movement, "MOVEMENT"));
      g.knobs.push_back (knob (id::microPitch, "MICRO PITCH")); g.knobs.push_back (knob (id::shadow, "SHADOW"));
      addPage ("STEREO"); }

    for (auto& pg : pages)
        for (auto& grp : pg->groups)
            for (auto& k : grp.knobs) k->onGestureStart = [this] { proc.undo.beginNewTransaction(); };
    for (auto& k : macros) k->onGestureStart = [this] { proc.undo.beginNewTransaction(); };
}

void PolooyxEditor::mouseDown (const juce::MouseEvent&)
{
    proc.undo.beginNewTransaction();   // every click/drag becomes its own undo step
}

void PolooyxEditor::syncStyleBox()
{
    if (styleBox.isPopupActive()) return;
    int id = 0;
    for (int i = 0; i < styleBox.getNumItems(); ++i)
        if (styleBox.getItemText (i) == proc.currentPresetName) id = styleBox.getItemId (i);
    if (styleBox.getSelectedId() != id) styleBox.setSelectedId (id, juce::dontSendNotification);
}

void PolooyxEditor::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    const auto& fp = factoryPresets();
    juce::String lastCat;
    for (size_t i = 0; i < fp.size(); ++i)
    {
        if (lastCat != fp[i].category) { presetBox.addSectionHeading (fp[i].category); lastCat = fp[i].category; }
        presetBox.addItem (fp[i].name, (int) i + 1);
    }
    auto users = proc.getUserPresets();
    if (! users.isEmpty())
    {
        presetBox.addSectionHeading ("USER");
        for (int i = 0; i < users.size(); ++i) presetBox.addItem (users[i].getFileNameWithoutExtension(), 1000 + i);
    }
    presetBox.setText (proc.currentPresetName, juce::dontSendNotification);
}

void PolooyxEditor::setAdvanced (bool adv)
{
    advanced = adv;
    advBtn.setToggleState (adv, juce::dontSendNotification);
    vis.setVisible (! adv);
    for (auto& m : macros) m->setVisible (! adv);
    styleBox.setVisible (! adv);
    for (auto& c : chain) c->btn.setVisible (adv);
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        tabs[i]->setVisible (adv);
        tabs[i]->setToggleState ((int) i == currentTab, juce::dontSendNotification);
        pages[i]->setVisible (adv && (int) i == currentTab);
    }
    resized();
    content.repaint();
}

void PolooyxEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void PolooyxEditor::resized()
{
    const float scale = (float) getWidth() / (float) kW;
    content.setTransform (juce::AffineTransform::scale (scale));

    // top bar
    presetBox.setBounds (330, 15, 250, 30);
    prevBtn.setBounds (296, 15, 30, 30);
    nextBtn.setBounds (584, 15, 30, 30);
    saveBtn.setBounds (620, 15, 60, 30);
    aBtn.setBounds (696, 15, 30, 30);
    bBtn.setBounds (728, 15, 30, 30);
    undoBtn.setBounds (772, 15, 56, 30);
    redoBtn.setBounds (832, 15, 56, 30);
    modeBtn.setBounds (902, 15, 176, 30);
    advBtn.setBounds (kW - 120, 626, 100, 26);

    // main
    vis.setBounds (22, 76, kW - 44, 232);
    const int mw = (kW - 44) / 6;
    for (size_t i = 0; i < macros.size(); ++i)
        macros[i]->setBounds (22 + (int) i * mw, 344, mw, 262);
    styleBox.setBounds (74, 312, 230, 26);

    // advanced
    int x = 72;
    for (auto& c : chain) { c->btn.setBounds (x, 88, 92, 26); x += 92 + 16; }
    x = 22;
    for (auto& t : tabs) { t->setBounds (x, 128, 118, 28); x += 122; }
    for (auto& p : pages) p->setBounds (22, 162, kW - 44, 452);

    // bottom
    inMeter.setBounds (22, 632, 22, 78);
    outMeter.setBounds (48, 632, 26, 78);
    inKnob->setBounds (86, 628, 86, 86);
    mixKnob->setBounds (178, 628, 86, 86);
    outKnob->setBounds (270, 628, 86, 86);
    punchKnob->setBounds (362, 628, 86, 86);
    ceilKnob->setBounds (454, 628, 86, 86);
}

void PolooyxEditor::timerCallback()
{
    if (! advanced) vis.tick();
    inMeter.tick(); outMeter.tick();

    aBtn.setToggleState (proc.activeAB == 0, juce::dontSendNotification);
    bBtn.setToggleState (proc.activeAB == 1, juce::dontSendNotification);
    undoBtn.setEnabled (proc.undo.canUndo());
    redoBtn.setEnabled (proc.undo.canRedo());
    if (presetBox.getText() != proc.currentPresetName && ! presetBox.isPopupActive())
        presetBox.setText (proc.currentPresetName, juce::dontSendNotification);
    syncStyleBox();

    auto& pe = proc.getPitchEngine();
    const float midi = pe.detectedMidi.load();
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto noteName = [] (float m) { const int n = (int) std::lround (m); return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1); };
    juce::String t;
    if (midi > 0.0f)
    {
        const float cents = (midi - std::round (midi)) * 100.0f;
        t << noteName (midi) << (cents >= 0 ? "  +" : "  ") << juce::String ((int) std::lround (cents)) << " ct";
        if (proc.apvts.getRawParameterValue (id::tuneOn)->load() > 0.5f)
            t << "   >   " << noteName (pe.targetMidi.load()) << "   (corr " << juce::String ((int) std::lround (pe.correctionCents.load())) << " ct)";
    }
    else t = "--";
    const auto gr = juce::String ("COMP ") + juce::String (proc.compGr.load(), 1) + "   DE-ESS " + juce::String (proc.deessGr.load(), 1)
                  + "   LIMIT " + juce::String (proc.limGr.load(), 1) + " dB";
    if (t != tunerText || gr != grText) { tunerText = t; grText = gr; content.repaint (460, 630, 360, 86); }
    if (advanced && currentTab == 1) pages[1]->repaint();
}
