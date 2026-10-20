#include "PluginEditor.h"
#include <complex>

using namespace seed;

namespace
{
    constexpr int kWidth = 900, kHeight = 700;
    constexpr int kContentX = 190, kContentY = 52;
    constexpr int kKnobW = 72, kKnobH = 64;

    struct Pos { int x, y; };

    // Knob positions, relative to the content area, in parameter order.
    const Pos kPos[kNumParams] =
    {
        // I. Envelope and LFO
        { 40, 0 }, { 195, 0 }, { 350, 0 }, { 505, 0 },            // env_time env_loop env_tilt env_kf
        { 14, 86 }, { 90, 86 }, { 14, 152 }, { 90, 152 },         // vol_atk vol_dcy vol_sus vol_fade
        { 14, 230 }, { 90, 230 }, { 14, 296 }, { 90, 296 },       // mod_atk mod_dcy mod_sh mod_vel
        { 14, 374 }, { 90, 374 }, { 14, 440 }, { 90, 440 },       // lfo_rate lfo_amt lfo_bal lfo_dly
        // II. Oscillators
        { 14, 0 }, { 90, 0 }, { 14, 66 }, { 90, 66 }, { 52, 132 },// a_form a_noise a_mod a_color a_freq
        { 14, 204 }, { 90, 204 },                                 // fm_mod fm_amt
        { 500, 204 }, { 580, 204 },                               // mix_mod osc_mix
        { 14, 290 }, { 90, 290 }, { 14, 356 }, { 90, 356 }, { 14, 422 }, { 90, 422 },   // b_*
        // III. Filter and effects
        { 14, 0 }, { 90, 0 }, { 14, 66 }, { 90, 66 }, { 14, 132 }, { 90, 132 },        // flt_*
        { 52, 206 },                                                                   // saturate
        { 14, 276 }, { 90, 276 }, { 14, 342 }, { 90, 342 }, { 14, 408 }, { 90, 408 },  // rvb_*
        { 14, 484 }, { 90, 484 }, { 520, 484 }, { 596, 484 }                           // adj_bass adj_treb adj_pan adj_clip
    };

    const char* kRoman[3] = { "I.", "II.", "III." };

    float hashRnd (int k)
    {
        uint32_t x = (uint32_t) k * 2654435761u;
        x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
        return (float) (x & 0xffff) / 65535.f;
    }

    void strokeCurve (juce::Graphics& g, const std::vector<juce::Point<float>>& pts, juce::Colour c,
                      bool fill, float baseY, float thick = 1.8f)
    {
        if (pts.size() < 2) return;
        juce::Path path;
        path.startNewSubPath (pts.front());
        for (size_t i = 1; i < pts.size(); ++i) path.lineTo (pts[i]);

        if (fill)
        {
            juce::Path f (path);
            f.lineTo (pts.back().x, baseY);
            f.lineTo (pts.front().x, baseY);
            f.closeSubPath();
            g.setColour (c.withAlpha (0.16f));
            g.fillPath (f);
        }
        g.setColour (c);
        g.strokePath (path, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void arrowHead (juce::Graphics& g, juce::Point<float> tip, bool pointingUp)
    {
        juce::Path p;
        const float s = pointingUp ? 1.f : -1.f;
        p.addTriangle (tip.x, tip.y, tip.x - 4.5f, tip.y + 8.f * s, tip.x + 4.5f, tip.y + 8.f * s);
        g.fillPath (p);
    }
}

//==============================================================================
juce::Font ui::serif (float height, bool italic, bool bold)
{
    const int style = (italic ? juce::Font::italic : 0) | (bold ? juce::Font::bold : 0);
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), height, style));
}

juce::Colour ui::groupColour (int group)
{
    switch (group)
    {
        case gVol: case gOscA: case gAdj:          return ink;
        case gMod: case gOscB: case gFlt:          return red;
        default:                                   return slate;   // env, lfo, fm, mix, sat, reverb
    }
}

//==============================================================================
SeedLookAndFeel::SeedLookAndFeel()
{
    using namespace ui;
    setColour (juce::TextButton::buttonColourId, ink);
    setColour (juce::TextButton::textColourOffId, paper);
    setColour (juce::TextButton::textColourOnId, paper);
    setColour (juce::TextEditor::backgroundColourId, mist.withAlpha (0.5f));
    setColour (juce::TextEditor::textColourId, ink);
    setColour (juce::TextEditor::outlineColourId, slate.withAlpha (0.6f));
    setColour (juce::TextEditor::focusedOutlineColourId, red);
    setColour (juce::CaretComponent::caretColourId, red);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::BubbleComponent::backgroundColourId, ink);
    setColour (juce::BubbleComponent::outlineColourId, ink);
    setColour (juce::TooltipWindow::textColourId, paper);
}

void SeedLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    using namespace ui;
    const juce::Colour accent ((juce::uint32) (juce::int64) s.getProperties()["accent"]);
    const bool bipolar = (bool) s.getProperties()["bipolar"];

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float thick = 4.6f;
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f - thick * 0.5f - 1.f;
    const float ang = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (cx, cy, r, r, 0.f, startAngle, endAngle, true);
    g.setColour (mist);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    juce::Path val;
    const float from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
    if (std::abs (ang - from) > 0.01f)
    {
        val.addCentredArc (cx, cy, r, r, 0.f, std::min (from, ang), std::max (from, ang), true);
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    const float ri = r - thick * 0.5f - 2.f;
    g.setColour (paper.darker (0.05f));
    g.fillEllipse (cx - ri, cy - ri, ri * 2.f, ri * 2.f);

    g.setColour (ink);
    g.drawLine (cx + std::sin (ang) * ri * 0.25f, cy - std::cos (ang) * ri * 0.25f,
                cx + std::sin (ang) * ri * 0.95f, cy - std::cos (ang) * ri * 0.95f, 2.f);
}

void SeedLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                        float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    using namespace ui;
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (mist);
    g.fillRoundedRectangle ((float) x, cy - 2.f, (float) w, 4.f, 2.f);
    g.setColour (red);
    g.fillRoundedRectangle ((float) x, cy - 2.f, sliderPos - (float) x, 4.f, 2.f);
    g.setColour (ink);
    g.fillEllipse (sliderPos - 6.f, cy - 6.f, 12.f, 12.f);
}

void SeedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    using namespace ui;
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? red : (highlighted ? slate : ink));
    g.fillRoundedRectangle (r, 4.f);
}

juce::Font SeedLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return ui::serif (14.f); }

void SeedLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    using namespace ui;
    const auto r = b.getLocalBounds().toFloat();
    const float s = 14.f;
    const juce::Rectangle<float> box (r.getX() + 2.f, r.getCentreY() - s * 0.5f, s, s);
    g.setColour (highlighted ? slate : ink);
    g.drawRoundedRectangle (box, 3.f, 1.4f);
    if (b.getToggleState())
    {
        g.setColour (red);
        g.fillRoundedRectangle (box.reduced (3.f), 2.f);
    }
    g.setColour (ink);
    g.setFont (serif (14.f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (s + 8.f), juce::Justification::centredLeft);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& apvts, int index) : paramIndex (index)
{
    const auto& d = kParams[index];
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
    slider.setMouseDragSensitivity (200);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.getProperties().set ("accent", (juce::int64) ui::groupColour (d.group).getARGB());
    slider.getProperties().set ("bipolar", (d.flags & fBipolar) != 0);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, d.id, slider);
    slider.setDoubleClickReturnValue (true, d.def);
}

void Knob::resized() { slider.setBounds ((getWidth() - 46) / 2, 0, 46, 46); }

void Knob::paint (juce::Graphics& g)
{
    const auto& d = kParams[paramIndex];
    g.setFont (ui::serif (13.f));
    g.setColour (ui::ink.withAlpha (0.85f));
    g.drawText (d.id, 0, 47, getWidth(), 16, juce::Justification::centred);

    if (d.flags & fStar)
    {
        g.setFont (ui::serif (16.f, false, true));
        g.drawText ("*", slider.getRight() + 1, slider.getY() + 1, 12, 16, juce::Justification::centred);
    }
}

//==============================================================================
Figure::Figure (juce::String f, juce::String t, DrawFn fn) : fig (std::move (f)), caption (std::move (t)), draw (std::move (fn)) {}

void Figure::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::Rectangle<float> cap;
    if (fig.isNotEmpty()) cap = b.removeFromBottom (17.f);
    const auto plot = b.reduced (0.f, 1.f);

    g.setColour (ui::mist.withAlpha (0.32f));
    g.fillRoundedRectangle (plot, 3.f);
    g.setColour (ui::slate.withAlpha (0.45f));
    g.drawLine (plot.getX(), plot.getY(), plot.getX(), plot.getBottom(), 1.f);
    g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getBottom(), 1.f);

    if (draw) draw (g, plot.reduced (8.f, 7.f));

    if (fig.isNotEmpty())
    {
        g.setFont (ui::serif (12.5f));
        g.setColour (ui::slate);
        g.drawText ("figure " + fig, cap.withWidth (70.f), juce::Justification::centredLeft);
        g.setColour (ui::ink);
        g.drawText (caption, cap.withTrimmedLeft (72.f), juce::Justification::centredLeft);
    }
}

//==============================================================================
Helix::Helix() { startTimerHz (30); }

void Helix::timerCallback() { phase += 0.012f; if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi; repaint(); }

void Helix::mouseDown (const juce::MouseEvent& e)
{
    const float top = 8.f, zoneH = ((float) getHeight() - 44.f) / 3.f;
    const int z = juce::jlimit (0, 2, (int) ((e.position.y - top) / zoneH));
    if (z != page) { page = z; if (onPage) onPage (z); }
    repaint();
}

void Helix::paint (juce::Graphics& g)
{
    using namespace ui;
    const float w = (float) getWidth();
    const float top = 8.f, H = (float) getHeight() - 44.f, zoneH = H / 3.f;
    const float cx = w * 0.5f, amp = w * 0.27f, period = H / 2.1f;
    const float twoPi = juce::MathConstants<float>::twoPi;

    auto angleAt = [&] (float y) { return (y - top) / period * twoPi + phase; };

    auto strand = [&] (float sign, juce::Colour c, bool front)
    {
        float px = 0.f, py = top;
        bool have = false;
        for (float y = top; y <= top + H; y += 3.f)
        {
            const float a = angleAt (y);
            const float xx = cx + sign * amp * std::sin (a);
            const float depth = sign * std::cos (a);          // > 0 = in front
            if (have && ((depth > 0.f) == front))
            {
                g.setColour (c.withAlpha (front ? 1.f : 0.55f));
                g.drawLine (px, py, xx, y, front ? 5.5f : 3.5f);
            }
            px = xx; py = y; have = true;
        }
    };

    strand (1.f, red, false);
    strand (-1.f, slate, false);

    // rungs and beads
    int start[3] = { 0, 0, 0 }, count[3] = { 0, 0, 0 };
    for (int i = 0; i < kNumParams; ++i) { const int p = kParams[i].page; if (count[p] == 0) start[p] = i; ++count[p]; }

    for (int p = 0; p < 3; ++p)
        for (int j = 0; j < count[p]; ++j)
        {
            const float y = top + (float) p * zoneH + ((float) j + 0.5f) * zoneH / (float) count[p];
            const float a = angleAt (y);
            const float x1 = cx + amp * std::sin (a), x2 = cx - amp * std::sin (a);
            g.setColour (slate.withAlpha (0.35f));
            g.drawLine (x1, y, x2, y, 1.f);

            const int idx = start[p] + j;
            const float v = valueOf ? juce::jlimit (0.f, 1.f, valueOf (idx)) : 0.5f;
            const float bx = x1 + (x2 - x1) * v;
            g.setColour (paper);
            g.fillEllipse (bx - 4.f, y - 4.f, 8.f, 8.f);
            g.setColour (groupColour (kParams[idx].group));
            g.drawEllipse (bx - 4.f, y - 4.f, 8.f, 8.f, 1.5f);
        }

    strand (1.f, red, true);
    strand (-1.f, slate, true);

    // page boxes + roman numerals
    for (int p = 0; p < 3; ++p)
    {
        const juce::Rectangle<float> zone (3.f, top + (float) p * zoneH + 1.f, w - 6.f, zoneH - 2.f);
        const bool active = p == page;
        g.setColour (active ? ink : slate.withAlpha (0.5f));

        juce::Path box; box.addRectangle (zone);
        juce::Path dashed;
        const float dashes[] = { 4.f, 3.f };
        juce::PathStrokeType (1.f).createDashedStroke (dashed, box, dashes, 2);
        g.fillPath (dashed);

        g.setFont (serif (17.f, false, active));
        g.setColour (active ? ink : slate);
        g.drawText (kRoman[p], zone.getX() + 5.f, zone.getY() + 2.f, 40.f, 20.f, juce::Justification::topLeft);
    }

    g.setFont (serif (12.f));
    g.setColour (slate);
    g.drawText (caption ? caption() : juce::String(), 0, getHeight() - 30, getWidth(), 20, juce::Justification::centred);
}

//==============================================================================
SeedEditor::SeedEditor (SeedProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    setSize (kWidth, kHeight);
    setResizable (false, false);

    proc.ensureFactoryPresets();

    for (int i = 0; i < kNumParams; ++i)
    {
        auto k = std::make_unique<Knob> (proc.apvts, i);
        addChildComponent (*k);
        knobs.push_back (std::move (k));
        vals[i] = lastVals[i] = proc.getReal (i);
    }
    dv = makeDerived (vals);

    // ---- figures -----------------------------------------------------------------
    addFigure (0, { 190, 88, 480, 134 },  "1a", "Volume envelope",       [this] (auto& g, auto r) { drawVolEnv (g, r); });
    addFigure (0, { 190, 232, 480, 134 }, "1b", "Modulation envelope",   [this] (auto& g, auto r) { drawModEnv (g, r); });
    addFigure (0, { 190, 376, 480, 62 },  "1c", "Pitch oscillation (vibrato)",  [this] (auto& g, auto r) { drawVibrato (g, r); });
    addFigure (0, { 190, 444, 480, 62 },  "1d", "Volume oscillation (tremolo)", [this] (auto& g, auto r) { drawTremolo (g, r); });

    addFigure (1, { 190, 0, 480, 134 },   "2a", "Oscillator A", [this] (auto& g, auto r) { drawOsc (g, r, false); });
    addFigure (1, { 190, 290, 480, 198 }, "2b", "Oscillator B", [this] (auto& g, auto r) { drawOsc (g, r, true); });

    addFigure (2, { 190, 0, 480, 196 },   "3a", "Filter response curve",   [this] (auto& g, auto r) { drawFilter (g, r); });
    addFigure (2, { 190, 206, 150, 70 },  "",   "",                        [this] (auto& g, auto r) { drawSaturate (g, r); });
    addFigure (2, { 190, 276, 480, 196 }, "3b", "Artificial reverberation", [this] (auto& g, auto r) { drawReverb (g, r); });
    addFigure (2, { 190, 484, 300, 66 },  "",   "",                        [this] (auto& g, auto r) { drawEq (g, r); });

    // ---- helix -------------------------------------------------------------------
    helix.valueOf = [this] (int idx) { return proc.getNormalised (idx); };
    helix.caption = [this] { return proc.getSeed() > 0 ? "seed #" + juce::String (proc.getSeed()) : juce::String ("seed  -"); };
    helix.onPage = [this] (int pg) { setPage (pg); };
    addAndMakeVisible (helix);

    // ---- bottom bar ----------------------------------------------------------------
    randomizeBtn.onClick = [this] { proc.randomizeNew(); refreshLabels(); };
    tuneBtn.onClick = [this] { proc.correctTune(); };
    prevBtn.onClick = [this] { proc.stepPreset (-1); refreshLabels(); };
    nextBtn.onClick = [this] { proc.stepPreset (+1); refreshLabels(); };
    saveBtn.onClick = [this] { savePresetDialog(); };
    loadBtn.onClick = [this] { loadPresetDialog(); };
    randomizeBtn.setTooltip ("Generate a new random patch (soft and light by default)");
    tuneBtn.setTooltip ("Snap oscillator A to octaves and B to perfect intervals so the keys play in tune");

    seedBox.setInputRestrictions (9, "0123456789");
    seedBox.setFont (ui::serif (14.f));
    seedBox.setJustification (juce::Justification::centred);
    seedBox.setTooltip ("Seed of the current random patch. Type a seed and press Return to recall it.");
    seedBox.onReturnKey = [this]
    {
        const auto s = (uint32_t) seedBox.getText().getLargeIntValue();
        if (s > 0) { proc.randomize (s); refreshLabels(); }
        seedBox.giveAwayKeyboardFocus();
    };

    chaosSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    chaosSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    chaosSlider.setTooltip ("Randomize range: soft and light  <->  wild");
    chaosAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, kChaosId, chaosSlider);
    autoTuneAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kAutoTuneId, autoTuneBtn);

    presetLabel.setJustificationType (juce::Justification::centred);
    presetLabel.setFont (ui::serif (14.f, true));
    presetLabel.setInterceptsMouseClicks (false, false);

    for (auto* c : std::initializer_list<juce::Component*> { &randomizeBtn, &tuneBtn, &prevBtn, &nextBtn, &saveBtn, &loadBtn,
                                                              &seedBox, &chaosSlider, &autoTuneBtn, &presetLabel })
        addAndMakeVisible (c);

    resized();      // setSize() ran before the knobs/figures existed, so lay everything out now
    refreshLabels();
    setPage (0);
    startTimerHz (20);
}

SeedEditor::~SeedEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SeedEditor::addFigure (int pg, juce::Rectangle<int> rect, const juce::String& fig, const juce::String& title, Figure::DrawFn fn)
{
    FigureEntry e { std::make_unique<Figure> (fig, title, std::move (fn)), pg, rect };
    addChildComponent (*e.comp);
    figures.push_back (std::move (e));
}

void SeedEditor::setPage (int newPage)
{
    page = newPage;
    helix.page = newPage;
    for (auto& k : knobs) k->setVisible (kParams[k->paramIndex].page == page);
    for (auto& f : figures) f.comp->setVisible (f.page == page);
    repaint();
}

void SeedEditor::resized()
{
    helix.setBounds (8, 8, 164, 624);

    for (auto& k : knobs)
    {
        const auto& p = kPos[k->paramIndex];
        k->setBounds (kContentX + p.x, kContentY + p.y, kKnobW, kKnobH);
    }
    for (auto& f : figures)
        f.comp->setBounds (f.rect.translated (kContentX, kContentY));

    const int y = 650, h = 30;
    randomizeBtn.setBounds (14, y, 96, h);
    seedBox.setBounds (118, y + 2, 84, h - 4);
    softLabelRect = { 210, y, 32, h };
    chaosSlider.setBounds (242, y, 92, h);
    wildLabelRect = { 336, y, 32, h };
    tuneBtn.setBounds (378, y, 104, h);
    autoTuneBtn.setBounds (490, y, 96, h);

    prevBtn.setBounds (606, y, 26, h);
    presetLabel.setBounds (634, y, 128, h);
    nextBtn.setBounds (764, y, 26, h);
    saveBtn.setBounds (798, y, 46, h);
    loadBtn.setBounds (848, y, 42, h);
}

void SeedEditor::refreshLabels()
{
    lastPresetShown = proc.getPresetName();
    presetLabel.setText (lastPresetShown, juce::dontSendNotification);
    lastSeedShown = proc.getSeed();
    if (! seedBox.hasKeyboardFocus (true))
        seedBox.setText (lastSeedShown > 0 ? juce::String (lastSeedShown) : juce::String(), juce::dontSendNotification);
}

void SeedEditor::timerCallback()
{
    bool changed = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        vals[i] = proc.getReal (i);
        if (vals[i] != lastVals[i]) { changed = true; lastVals[i] = vals[i]; }
    }
    if (changed)
    {
        dv = makeDerived (vals);
        for (auto& f : figures) if (f.page == page) f.comp->repaint();
    }
    if (proc.getSeed() != lastSeedShown || proc.getPresetName() != lastPresetShown) refreshLabels();
}

void SeedEditor::savePresetDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Save preset",
                                                   proc.getPresetDirectory().getChildFile (proc.getPresetName() + ".seedling"),
                                                   "*.seedling");
    juce::Component::SafePointer<SeedEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (safe == nullptr || f == juce::File()) return;
                              safe->proc.savePreset (f.withFileExtension ("seedling"));
                              safe->refreshLabels();
                          });
}

void SeedEditor::loadPresetDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Load preset", proc.getPresetDirectory(), "*.seedling");
    juce::Component::SafePointer<SeedEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (safe == nullptr || ! f.existsAsFile()) return;
                              safe->proc.loadPreset (f);
                              safe->refreshLabels();
                          });
}

//==============================================================================
void SeedEditor::paint (juce::Graphics& g)
{
    using namespace ui;
    g.fillAll (paper);

    // panel behind the content
    g.setColour (mist.withAlpha (0.22f));
    g.fillRoundedRectangle (178.f, 6.f, (float) kWidth - 190.f, 626.f, 6.f);

    g.setColour (ink);
    g.setFont (serif (26.f, false, true));
    g.drawText (juce::String (kRoman[page]) + "   " + kPageTitles[page], kContentX, 10, 480, 36, juce::Justification::centred);

    // signal-flow arrows on the oscillator page
    if (page == 1)
    {
        g.setColour (slate);
        const float ox = (float) kContentX, oy = (float) kContentY;

        g.drawLine (ox + 576.f, oy + 136.f, ox + 576.f, oy + 192.f, 1.3f);
        arrowHead (g, { ox + 576.f, oy + 202.f }, false);

        juce::Path conn;
        conn.startNewSubPath (ox + 330.f, oy + 290.f);
        conn.lineTo (ox + 330.f, oy + 280.f);
        conn.lineTo (ox + 51.f, oy + 280.f);
        conn.lineTo (ox + 51.f, oy + 276.f);
        g.strokePath (conn, juce::PathStrokeType (1.3f));
        arrowHead (g, { ox + 51.f, oy + 268.f }, true);
    }

    // bottom bar
    g.setColour (mist);
    g.drawLine (14.f, 640.f, (float) kWidth - 10.f, 640.f, 1.f);
    g.setColour (slate);
    g.setFont (serif (13.f, true));
    g.drawText ("soft", softLabelRect, juce::Justification::centredRight);
    g.drawText ("wild", wildLabelRect, juce::Justification::centredLeft);
}

//==============================================================================
// Figures
//==============================================================================
void SeedEditor::drawVolEnv (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& d = dv;
    const float vs = d.envScale;
    const float tA = std::max (1e-4f, d.volAtk * d.atkMul * vs);
    const float tD = std::max (1e-3f, d.volDcy * d.dcyMul * vs);
    const float tR = std::max (1e-3f, d.volRel * vs);
    const float hold = std::max (0.25f, tA + tD * 1.3f);
    const float T = hold + std::max (tR * 1.2f, hold * 0.3f);

    const int N = std::max (20, (int) r.getWidth());
    const float dt = T / (float) N;
    const float fadeMul = d.fadeTau > 0.f ? std::exp (-dt / (d.fadeTau * vs)) : 1.f;
    const float dcyCo = 1.f - std::exp (-5.f * dt / tD), relCo = std::exp (-5.f * dt / tR);

    std::vector<juce::Point<float>> pts;
    float lvl = 0.f, fade = 1.f;
    int stage = 1;
    for (int i = 0; i <= N; ++i)
    {
        const float t = (float) i * dt;
        if (t >= hold) stage = 3;
        if (stage == 1) { lvl += dt / tA; if (lvl >= 1.f) { lvl = 1.f; stage = 2; } }
        else if (stage == 2) { lvl += (d.volSus - lvl) * dcyCo; fade *= fadeMul; }
        else lvl *= relCo;
        pts.push_back ({ r.getX() + r.getWidth() * (float) i / (float) N, r.getBottom() - lvl * fade * r.getHeight() });
    }
    strokeCurve (g, pts, ui::groupColour (gVol), true, r.getBottom());
}

void SeedEditor::drawModEnv (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& d = dv;
    const float vs = d.envScale;
    const float a = std::max (1e-4f, d.modAtk * d.atkMul * vs), dc = std::max (1e-3f, d.modDcy * d.dcyMul * vs);
    const float exponent = 1.f + 5.f * d.modShape;
    const float loop = d.envLoop > 0.02f ? std::max (0.015f, (a + dc) * (1.f + 3.f * (1.f - d.envLoop))) : 0.f;
    const float T = loop > 0.f ? loop * 2.2f : (a + dc) * 1.25f;

    const int N = std::max (20, (int) r.getWidth());
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= N; ++i)
    {
        float t = T * (float) i / (float) N;
        if (loop > 0.f) t = std::fmod (t, loop);
        float m;
        if (t < a) m = t / a;
        else { const float u = (t - a) / dc; m = u >= 1.f ? 0.f : std::pow (1.f - u, exponent); }
        pts.push_back ({ r.getX() + r.getWidth() * (float) i / (float) N, r.getBottom() - m * r.getHeight() });
    }
    strokeCurve (g, pts, ui::groupColour (gMod), true, r.getBottom());
}

void SeedEditor::drawVibrato (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& d = dv;
    const int N = std::max (40, (int) r.getWidth() * 2);
    const float seconds = 2.f;
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= N; ++i)
    {
        const float t = seconds * (float) i / (float) N;
        const float ramp = d.lfoDelay > 0.01f ? std::min (1.f, t / d.lfoDelay) : 1.f;
        const float v = d.vibSemis * std::sin (kTwoPi * d.lfoHz * t) * ramp;     // -1..1 semitone
        pts.push_back ({ r.getX() + r.getWidth() * (float) i / (float) N, r.getCentreY() - v * r.getHeight() * 0.45f });
    }
    strokeCurve (g, pts, ui::slate, false, 0.f, 1.6f);
}

void SeedEditor::drawTremolo (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& d = dv;
    const int N = std::max (40, (int) r.getWidth() * 2);
    const float seconds = 2.f;
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= N; ++i)
    {
        const float t = seconds * (float) i / (float) N;
        const float ramp = d.lfoDelay > 0.01f ? std::min (1.f, t / d.lfoDelay) : 1.f;
        const float lfo = std::sin (kTwoPi * d.lfoHz * t) * ramp;
        const float gain = 1.f - d.tremDepth * 0.5f * (1.f - lfo);               // 1-depth .. 1
        pts.push_back ({ r.getX() + r.getWidth() * (float) i / (float) N, r.getBottom() - gain * r.getHeight() * 0.9f });
    }
    strokeCurve (g, pts, ui::slate, true, r.getBottom(), 1.6f);
}

void SeedEditor::drawOsc (juce::Graphics& g, juce::Rectangle<float> r, bool isB)
{
    const auto& d = dv;
    const float form = isB ? d.bForm : d.aForm;
    const float cycles = isB ? 3.f : 2.f;
    const int N = std::max (60, (int) r.getWidth() * 2);
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= N; ++i)
    {
        const float x = cycles * (float) i / (float) N;
        float ph = x - std::floor (x);
        if (! isB && d.aColor > 0.f) ph = colorWarp (ph, d.aColor);
        const float v = morphShape (isB, form, ph, 0.f);
        pts.push_back ({ r.getX() + r.getWidth() * (float) i / (float) N, r.getCentreY() - v * r.getHeight() * 0.42f });
    }
    g.setColour (ui::slate.withAlpha (0.4f));
    g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.f);
    strokeCurve (g, pts, ui::groupColour (isB ? gOscB : gOscA), false, 0.f, 2.f);
}

void SeedEditor::drawFilter (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& d = dv;
    const float w = d.fltType * 2.f;
    const float wl = w < 1.f ? 1.f - w : 0.f, wb = w < 1.f ? w : 2.f - w, wh = w < 1.f ? 0.f : w - 1.f;
    const float fc1 = d.fltHz, fc2 = fc1 * std::exp2 (d.fltSep * 3.f);

    auto response = [&] (float f, float fc)
    {
        const float u = f / fc;
        const std::complex<float> den (1.f - u * u, d.fltK * u);
        return (wl + std::complex<float> (0.f, d.fltK * u) * wb + (-u * u) * wh) / den;
    };

    const int N = std::max (40, (int) r.getWidth());
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= N; ++i)
    {
        const float f = 20.f * std::pow (1000.f, (float) i / (float) N);
        const float mag = std::abs (0.5f * (response (f, fc1) + response (f, fc2)));
        const float db = 20.f * std::log10 (std::max (mag, 1e-4f));
        const float y01 = juce::jlimit (0.f, 1.f, (db + 48.f) / 72.f);
        pts.push_back ({ r.getX() + r.getWidth() * (float) i / (float) N, r.getBottom() - y01 * r.getHeight() });
    }
    const float zeroDb = r.getBottom() - (48.f / 72.f) * r.getHeight();
    g.setColour (ui::slate.withAlpha (0.55f));
    g.drawLine (r.getX(), zeroDb, r.getRight(), zeroDb, 1.f);
    strokeCurve (g, pts, ui::groupColour (gFlt), true, r.getBottom());
}

void SeedEditor::drawSaturate (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float side = std::min (r.getWidth(), r.getHeight());
    const auto sq = juce::Rectangle<float> (side, side).withCentre (r.getCentre());
    g.setColour (ui::slate.withAlpha (0.6f));
    g.drawLine (sq.getX(), sq.getCentreY(), sq.getRight(), sq.getCentreY(), 1.f);
    g.drawLine (sq.getCentreX(), sq.getY(), sq.getCentreX(), sq.getBottom(), 1.f);
    g.setColour (ui::slate.withAlpha (0.3f));
    g.drawLine (sq.getX(), sq.getBottom(), sq.getRight(), sq.getY(), 1.f);   // identity

    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= 60; ++i)
    {
        const float x = -1.f + 2.f * (float) i / 60.f;
        const float y = juce::jlimit (-1.f, 1.f, saturateCurve (x, dv.sat));
        pts.push_back ({ sq.getCentreX() + x * sq.getWidth() * 0.5f, sq.getCentreY() - y * sq.getHeight() * 0.5f });
    }
    strokeCurve (g, pts, ui::groupColour (gSat), false, 0.f, 2.f);
}

void SeedEditor::drawReverb (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& d = dv;
    const float T = d.rvbRt60 * 0.55f;
    const float pre = d.rvbAtk * 0.06f, swell = d.rvbAtk * 0.5f + 1e-3f;
    const int bars = std::max (30, (int) (r.getWidth() / 3.5f));

    std::vector<juce::Point<float>> env;
    g.setColour (ui::slate.withAlpha (0.8f));
    for (int k = 0; k < bars; ++k)
    {
        const float t = T * (float) k / (float) bars;
        float a = t < pre ? 0.f : std::pow (10.f, -3.f * t / d.rvbRt60);
        if (t >= pre && d.rvbAtk > 0.01f) a *= 1.f - std::exp (-(t - pre) / swell * 3.f);
        const float amp = a * (0.55f + 0.45f * hashRnd (k + 1));
        const float x = r.getX() + r.getWidth() * (float) k / (float) bars;
        g.drawLine (x, r.getBottom(), x, r.getBottom() - amp * r.getHeight(), 1.4f);
        env.push_back ({ x, r.getBottom() - a * r.getHeight() });
    }
    strokeCurve (g, env, ui::groupColour (gRvb), true, r.getBottom(), 1.2f);
}

void SeedEditor::drawEq (juce::Graphics& g, juce::Rectangle<float> r)
{
    Biquad low, high;
    low.setLowShelf (48000.f, 150.f, dv.bass * 12.f);
    high.setHighShelf (48000.f, 6000.f, dv.treb * 12.f);

    const int N = std::max (40, (int) r.getWidth());
    std::vector<juce::Point<float>> pts;
    for (int i = 0; i <= N; ++i)
    {
        const float f = 20.f * std::pow (1000.f, (float) i / (float) N);
        const float db = 20.f * std::log10 (std::max (1e-4f, low.magnitude (f, 48000.f) * high.magnitude (f, 48000.f)));
        const float y01 = juce::jlimit (0.f, 1.f, 0.5f + db / 30.f);
        pts.push_back ({ r.getX() + r.getWidth() * (float) i / (float) N, r.getBottom() - y01 * r.getHeight() });
    }
    g.setColour (ui::slate.withAlpha (0.45f));
    g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.f);
    strokeCurve (g, pts, ui::groupColour (gAdj), true, r.getCentreY(), 1.8f);
}
