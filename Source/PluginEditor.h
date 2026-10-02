#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"

// ============================================================ reusable controls
class Knob : public juce::Component, private juce::Slider::Listener
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& title,
          bool big = false, const juce::String& sub = {}, bool bipolar = false);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
    std::function<void()> onGestureStart;

private:
    void sliderValueChanged (juce::Slider*) override { repaint(); }
    void sliderDragStarted (juce::Slider*) override { if (onGestureStart) onGestureStart(); }
    juce::String name, subtitle;
    bool isBig;
    juce::RangedAudioParameter* param;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& title);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::ComboBox box;
private:
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

class Toggle : public juce::Component
{
public:
    Toggle (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& title);
    void resized() override { btn.setBounds (getLocalBounds()); }
    juce::ToggleButton btn;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

// ============================================================ visualizer
class Visualizer : public juce::Component
{
public:
    explicit Visualizer (PolooyxProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick();
private:
    PolooyxProcessor& proc;
    std::array<float, PolooyxProcessor::kScopeSize> in {}, out {};
    float phase = 0.0f, glitchFlash = 0.0f;
    juce::Random rnd;
};

class Meter : public juce::Component
{
public:
    Meter (std::atomic<float>& src, juce::String t) : source (src), title (std::move (t)) {}
    void paint (juce::Graphics&) override;
    void tick() { const float v = source.load(); level = std::max (v, level * 0.86f); peak = std::max (v, peak - 0.004f); repaint(); }
private:
    std::atomic<float>& source;
    juce::String title;
    float level = 0.0f, peak = 0.0f;
};

class EqCurve : public juce::Component
{
public:
    explicit EqCurve (juce::AudioProcessorValueTreeState& s) : st (s) {}
    void paint (juce::Graphics&) override;
private:
    juce::AudioProcessorValueTreeState& st;
};

// ============================================================ advanced page (generic grid of groups)
class Page : public juce::Component
{
public:
    struct Group
    {
        juce::String title, note;
        std::vector<std::unique_ptr<juce::Component>> controls;   // toggles / choices (header line)
        std::vector<std::unique_ptr<Knob>> knobs;
        std::unique_ptr<juce::Component> wide;                     // optional full-width display (eq curve)
    };
    std::vector<Group> groups;
    void resized() override;
    void paint (juce::Graphics&) override;
};

// ============================================================ editor
class PolooyxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PolooyxEditor (PolooyxProcessor&);
    ~PolooyxEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kW = 1100, kH = 720;
    void showView (bool adv, int tab = 0) { currentTab = tab; setAdvanced (adv); }   // used by tests / future settings

private:
    void timerCallback() override;
    void rebuildPresetMenu();
    void syncStyleBox();
    void setAdvanced (bool);
    void buildPages();
    void mouseDown (const juce::MouseEvent&) override;

    PolooyxProcessor& proc;
    plx::ui::LookAndFeel lnf;

    struct Content : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    } content;

    // top bar
    juce::ComboBox presetBox, styleBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, aBtn { "A" }, bBtn { "B" },
                     undoBtn { "UNDO" }, redoBtn { "REDO" }, advBtn { "ADVANCED" };
    juce::TextButton modeBtn { "POLOOYX MODE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> modeAtt;

    // main
    Visualizer vis;
    std::vector<std::unique_ptr<Knob>> macros;

    // advanced
    struct ChainChip { juce::TextButton btn; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att; };
    std::vector<std::unique_ptr<ChainChip>> chain;
    std::vector<std::unique_ptr<juce::TextButton>> tabs;
    std::vector<std::unique_ptr<Page>> pages;
    int currentTab = 0;
    bool advanced = false;

    // bottom
    Meter inMeter, outMeter;
    std::unique_ptr<Knob> inKnob, mixKnob, outKnob, ceilKnob, punchKnob;
    juce::String tunerText, grText;

    std::unique_ptr<juce::AlertWindow> saveDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolooyxEditor)
};
