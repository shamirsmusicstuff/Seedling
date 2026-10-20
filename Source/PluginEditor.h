#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"

//==============================================================================
namespace ui
{
    // Palette: https://coolors.co/0c120c-c20114-6d7275-c7d6d5-ecebf3
    inline const juce::Colour ink   { 0xff0c120c };
    inline const juce::Colour red   { 0xffc20114 };
    inline const juce::Colour slate { 0xff6d7275 };
    inline const juce::Colour mist  { 0xffc7d6d5 };
    inline const juce::Colour paper { 0xffecebf3 };

    juce::Font serif (float height, bool italic = false, bool bold = false);
    juce::Colour groupColour (int group);
}

//==============================================================================
class SeedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SeedLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

//==============================================================================
// One knob: a rotary slider bound to a parameter, with the parameter name underneath (as in the design).
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, int paramIndex);
    void paint (juce::Graphics&) override;
    void resized() override;

    const int paramIndex;

private:
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// A small live plot ("figure 1a ...") drawn by a callback.
class Figure : public juce::Component
{
public:
    using DrawFn = std::function<void (juce::Graphics&, juce::Rectangle<float>)>;
    Figure (juce::String figureName, juce::String title, DrawFn fn);
    void paint (juce::Graphics&) override;

private:
    juce::String fig, caption;
    DrawFn draw;
};

//==============================================================================
// The DNA helix page selector. Every rung is one parameter; the bead on the rung shows its value.
class Helix : public juce::Component, private juce::Timer
{
public:
    Helix();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int page = 0;
    std::function<void (int)> onPage;
    std::function<float (int)> valueOf;      // normalised 0..1 for a parameter index
    std::function<juce::String()> caption;

private:
    void timerCallback() override;
    float phase = 0.f;
};

//==============================================================================
class SeedEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SeedEditor (SeedProcessor&);
    ~SeedEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showPageForTest (int p) { setPage (p); }

private:
    void timerCallback() override;
    void setPage (int newPage);
    void refreshLabels();
    void savePresetDialog();
    void loadPresetDialog();

    // figure painters
    void drawVolEnv (juce::Graphics&, juce::Rectangle<float>);
    void drawModEnv (juce::Graphics&, juce::Rectangle<float>);
    void drawVibrato (juce::Graphics&, juce::Rectangle<float>);
    void drawTremolo (juce::Graphics&, juce::Rectangle<float>);
    void drawOsc (juce::Graphics&, juce::Rectangle<float>, bool isB);
    void drawFilter (juce::Graphics&, juce::Rectangle<float>);
    void drawSaturate (juce::Graphics&, juce::Rectangle<float>);
    void drawReverb (juce::Graphics&, juce::Rectangle<float>);
    void drawEq (juce::Graphics&, juce::Rectangle<float>);

    struct FigureEntry
    {
        std::unique_ptr<Figure> comp;
        int page;
        juce::Rectangle<int> rect;      // relative to the content origin
    };
    void addFigure (int page, juce::Rectangle<int> rect, const juce::String& fig, const juce::String& title,
                    Figure::DrawFn fn);

    SeedProcessor& proc;
    SeedLookAndFeel lnf;                // declared first, destroyed last
    juce::TooltipWindow tooltipWindow { this, 700 };

    int page = 0;
    float vals[seed::kNumParams] {};
    float lastVals[seed::kNumParams] {};
    seed::Derived dv {};

    Helix helix;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<FigureEntry> figures;

    juce::TextButton randomizeBtn { "Randomize" }, tuneBtn { "Correct Tune" },
                     prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" }, loadBtn { "Load" };
    juce::TextEditor seedBox;
    juce::Slider chaosSlider;
    juce::ToggleButton autoTuneBtn { "auto-tune" };
    juce::Label presetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chaosAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoTuneAttach;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> softLabelRect, wildLabelRect;
    juce::String lastPresetShown;
    uint32_t lastSeedShown = 0xffffffffu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeedEditor)
};
