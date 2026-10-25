#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "Palette.h"

//==============================================================================
namespace ui
{
    // The current palette (it follows the patch seed). Defaults to the original palette: https://coolors.co/0c120c-c20114-6d7275-c7d6d5-ecebf3
    // ink = background, red = the neon accent colour (whatever the seed makes it), slate = lines, mist = secondary, paper = text
    inline juce::Colour ink   { 0xff0c120c };
    inline juce::Colour red   { 0xffc20114 };
    inline juce::Colour slate { 0xff6d7275 };
    inline juce::Colour mist  { 0xffc7d6d5 };
    inline juce::Colour paper { 0xffecebf3 };

    void apply (const seed::Palette&);
    juce::Font serif (float height, bool italic = false, bool bold = false);     // kept for old call sites: Roboto regular / medium
    juce::Font font (float height, int weight = 1);                              // Roboto: 0 light, 1 regular, 2 medium, 3 bold
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
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void applyPalette();     // re-read the ui:: colours (after a palette change)
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
    std::function<void (int, float)> onInteract;     // (parameter, 0 = idle / 0.5 = hover / 1 = dragging)
    void mouseEnter (const juce::MouseEvent&) override { if (! dragging) notify (0.5f); }
    void mouseExit  (const juce::MouseEvent&) override { if (! dragging) notify (0.f); }

private:
    juce::Slider slider;
    bool dragging = false;
    void notify (float l) { if (onInteract) onInteract (paramIndex, l); }
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
// Page selector: a tree grown from a seed. Roots = I (envelope + LFO), trunk and low branches = II
// (oscillators), canopy = III (filter + effects). Every parameter is a glowing bulb on the tree whose
// size / brightness shows its value. Each seed grows its own tree (and it regrows when the seed changes).
class SeedTree : public juce::Component, private juce::Timer
{
public:
    using HostInfo = SeedProcessor::HostInfo;

    SeedTree();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void setSeed (uint32_t s);
    void skipGrowth (float g = 1.f) { growth = g; }
    void setSpin (float radians) { spin = radians; }
    void setHome (bool b) { home = b; repaint(); }     // home view: the big spinning tree with its beat ring, no page boxes

    // The branch carrying a parameter lights up while you hover (0.5) or drag (1) its control, and flashes when the value changes.
    void setInteraction (int param, float level) { if (param >= 0 && param < seed::kNumParams) interact[param] = level; }
    void settle();      // jump the animated state to its targets (used by the snapshot test)

    int page = 0;
    std::function<void (int)> onPage;
    std::function<float (int)> valueOf;      // normalised 0..1 for a parameter index
    std::function<juce::String()> caption;
    std::function<HostInfo()> hostInfo;      // host tempo / song position, drives the spin

    static constexpr double kBeatsPerRev = 32.0;   // one slow turn every 8 bars of 4/4

private:
    struct V3 { float x = 0.f, y = 0.f, z = 0.f; };
    struct Seg  { V3 a, c, b; float thick, t0, t1; int zone; bool root; int parent; };
    struct Node { int seg; float u; int param; float phase; };
    struct Item { float depth; int kind; int idx; };   // kind 0 = branch, 1 = bulb, 2 = the seed

    void timerCallback() override;
    void rebuild();
    V3 bez (const Seg&, float u) const;
    juce::Point<float> project (V3 p, float& depth, float& scale) const;
    int zoneOfY (float y) const;

    static constexpr float kPitch = 0.22f;   // camera looks down on the tree a little

    std::vector<Seg> segs;
    std::vector<Node> nodes;
    std::vector<float> prog;
    std::vector<Item> items;
    std::vector<Seg> dsegs;                       // branches as drawn: base shape + the value-driven extend / recline
    std::vector<V3> cum;                          // accumulated tip displacement along each branch's ancestry
    std::vector<float> nodeV, segVal, segHL;      // eased bulb values, per-branch mean value, per-branch highlight
    std::vector<int> segCnt;
    float interact[seed::kNumParams] = {}, hl[seed::kNumParams] = {}, flash[seed::kNumParams] = {}, prevV[seed::kNumParams] = {};
    bool havePrev = false, home = false;
    uint32_t rawSeed = 0xffffffffu, treeSeed = 29;
    float growth = 0.f, timeS = 0.f, spin = 0.f;
    float topY = 8.f, H = 0.f, yA = 0.f, ground = 0.f, rMax = 60.f;
};

//==============================================================================
class SeedEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SeedEditor (SeedProcessor&);
    ~SeedEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showPageForTest (int p) { setHomeView (false); setPage (p); }
    void setGrowthForTest (float g) { tree.skipGrowth (g); homeTree.skipGrowth (g); }
    void interactForTest (int param, float level) { tree.setInteraction (param, level); tree.settle(); homeTree.setInteraction (param, level); homeTree.settle(); }
    void setSpinForTest (float r) { tree.setSpin (r); homeTree.setSpin (r); }
    void showHomeForTest (bool h) { setHomeView (h); }
    bool isHomeView() const { return homeView; }
    void setSeedPaletteForTest (uint32_t sd) { paletteSeed = sd; palGliding = false; showPalette (seed::palette::fromSeed (sd)); }
    void syncForTest() { timerCallback(); driftT = 0.0; }
    void setDriftForTest (double beats) { driftT = beats; lastDriftClock = 0.0; }

private:
    void timerCallback() override;
    void setPage (int newPage);
    void setHomeView (bool home);
    void refreshLabels();
    void showPalette (const seed::Palette&);
    void followSeed();
    void rebuildBackdrop();
    void paintBackdrop (juce::Graphics&);
    void layoutChrome();
    juce::Rectangle<int> chipRect (int i) const;
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

    SeedTree tree, homeTree;
    bool homeView = true;
    juce::TextButton editBtn { "Edit sound" };
    juce::Image backdrop;
    double driftT = 0.0;                // beats of gradient drift (0 in the screenshot tests = home positions)
    double lastDriftClock = 0.0;
    juce::TextButton tabBtn[3] { juce::TextButton ("I  Envelope + LFO"), juce::TextButton ("II  Oscillators"), juce::TextButton ("III  Filter + FX") };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<FigureEntry> figures;

    juce::TextButton randomizeBtn { "Randomize" }, tuneBtn { "Tune" },
                     prevBtn { juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xb9")) }, nextBtn { juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")) }, saveBtn { "Save" }, loadBtn { "Load" };
    juce::TextEditor seedBox;
    juce::Slider chaosSlider;
    juce::ToggleButton autoTuneBtn { "auto-tune" };
    juce::ToggleButton monoBtn { "mono" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAttach;
    seed::Palette palShown = seed::palette::original(), palFrom = seed::palette::original(), palTo = seed::palette::original();
    uint32_t paletteSeed = 0;
    float palT = 1.f;
    bool palGliding = false;
    juce::Label presetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> chaosAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoTuneAttach;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> softLabelRect, wildLabelRect, seedCaptionRect;
    juce::String lastPresetShown;
    uint32_t lastSeedShown = 0xffffffffu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeedEditor)
};
