#include "PluginEditor.h"
#include "SeedFontData.h"
#include <complex>

using namespace seed;

namespace
{
    constexpr int kWidth = 900, kHeight = 744;
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
        { 14, 484 }, { 90, 484 }, { 520, 484 }, { 596, 484 },                          // adj_bass adj_treb adj_pan adj_clip
    // Play mode (page II)
    { 300, 204 }, { 0, 0 }                                                         // glide (mono is a switch, placed in resized())
    };

    float hashRnd (int k)
    {
        uint32_t x = (uint32_t) k * 2654435761u;
        x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
        return (float) (x & 0xffff) / 65535.f;
    }

    void strokeCurve (juce::Graphics& g, const std::vector<juce::Point<float>>& pts, juce::Colour col,
                      bool fill, float baseY, float thick = 1.6f)
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
            g.setColour (col.withAlpha (0.16f));
            g.fillPath (f);
        }
        g.setColour (col);
        g.strokePath (path, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void arrowHead (juce::Graphics& g, juce::Point<float> tip, bool pointingUp)
    {
        juce::Path p;
        const float s = pointingUp ? 1.f : -1.f;
        p.addTriangle (tip.x, tip.y, tip.x - 4.5f, tip.y + 8.f * s, tip.x + 4.5f, tip.y + 8.f * s);
        g.fillPath (p);
    }

    // small tracked capitals, the way labels are set throughout the new look
    void capsText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, float size, int weight,
                   juce::Colour col, juce::Justification j, float tracking = 0.14f)
    {
        g.setFont (ui::font (size, weight).withExtraKerningFactor (tracking));
        g.setColour (col);
        g.drawText (text.toUpperCase(), area, j);
    }
}

//==============================================================================
void ui::apply (const seed::Palette& k)
{
    auto c = [] (seed::Rgb x) { return juce::Colour (x.r, x.g, x.b); };
    ink = c (k.ink); red = c (k.accent); slate = c (k.slate); mist = c (k.mist); paper = c (k.paper);
}

juce::Font ui::font (float height, int weight)
{
    static juce::Typeface::Ptr faces[4];
    static bool loaded = false;
    if (! loaded)
    {
        faces[0] = juce::Typeface::createSystemTypefaceFor (SeedFontData::RobotoLight_ttf,   (size_t) SeedFontData::RobotoLight_ttfSize);
        faces[1] = juce::Typeface::createSystemTypefaceFor (SeedFontData::RobotoRegular_ttf, (size_t) SeedFontData::RobotoRegular_ttfSize);
        faces[2] = juce::Typeface::createSystemTypefaceFor (SeedFontData::RobotoMedium_ttf,  (size_t) SeedFontData::RobotoMedium_ttfSize);
        faces[3] = juce::Typeface::createSystemTypefaceFor (SeedFontData::RobotoBold_ttf,    (size_t) SeedFontData::RobotoBold_ttfSize);
        loaded = true;
    }
    const auto& f = faces[juce::jlimit (0, 3, weight)];
    return f != nullptr ? juce::Font (juce::FontOptions (f).withHeight (height)) : juce::Font (juce::FontOptions (height));
}

juce::Font ui::serif (float height, bool, bool bold) { return font (height, bold ? 2 : 1); }

juce::Colour ui::groupColour (int) { return paper; }       // one ink for every control: the colour comes from the seed's gradient

//==============================================================================
SeedLookAndFeel::SeedLookAndFeel() { applyPalette(); }

void SeedLookAndFeel::applyPalette()
{
    using namespace ui;
    setColour (juce::TextButton::buttonColourId, ink);
    setColour (juce::TextButton::textColourOffId, paper);
    setColour (juce::TextButton::textColourOnId, paper);
    setColour (juce::TextEditor::backgroundColourId, ink.withAlpha (0.4f));
    setColour (juce::TextEditor::textColourId, paper);
    setColour (juce::TextEditor::outlineColourId, paper.withAlpha (0.35f));
    setColour (juce::TextEditor::focusedOutlineColourId, paper);
    setColour (juce::TextEditor::highlightColourId, paper.withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId, paper);
    setColour (juce::CaretComponent::caretColourId, paper);
    setColour (juce::Label::textColourId, paper);
    setColour (juce::BubbleComponent::backgroundColourId, ink);
    setColour (juce::BubbleComponent::outlineColourId, paper.withAlpha (0.5f));
    setColour (juce::TooltipWindow::backgroundColourId, ink);
    setColour (juce::TooltipWindow::outlineColourId, paper.withAlpha (0.5f));
    setColour (juce::TooltipWindow::textColourId, paper);
    setColour (juce::PopupMenu::backgroundColourId, ink);
    setColour (juce::PopupMenu::textColourId, paper);
    setColour (juce::PopupMenu::headerTextColourId, paper.withAlpha (0.7f));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, paper.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, paper);
}

void SeedLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    using namespace ui;
    const bool bipolar = (bool) s.getProperties()["bipolar"];

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float thick = 2.2f;
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f - thick * 0.5f - 0.5f;
    const float ang = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (cx, cy, r, r, 0.f, startAngle, endAngle, true);
    g.setColour (paper.withAlpha (0.26f));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    const float from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
    if (std::abs (ang - from) > 0.01f)
    {
        val.addCentredArc (cx, cy, r, r, 0.f, std::min (from, ang), std::max (from, ang), true);
        g.setColour (paper);
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float ri = r - 5.f;
    g.setColour (ink.withAlpha (0.85f));
    g.fillEllipse (cx - ri, cy - ri, ri * 2.f, ri * 2.f);
    g.setColour (paper.withAlpha (0.3f));
    g.drawEllipse (cx - ri, cy - ri, ri * 2.f, ri * 2.f, 1.2f);

    g.setColour (paper);
    juce::Path pointer;
    pointer.startNewSubPath (cx + std::sin (ang) * ri * 0.28f, cy - std::cos (ang) * ri * 0.28f);
    pointer.lineTo (cx + std::sin (ang) * ri * 0.86f, cy - std::cos (ang) * ri * 0.86f);
    g.strokePath (pointer, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SeedLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                        float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    using namespace ui;
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (paper.withAlpha (0.3f));
    g.fillRoundedRectangle ((float) x, cy - 0.8f, (float) w, 1.6f, 0.8f);
    g.setColour (paper);
    g.fillRoundedRectangle ((float) x, cy - 1.5f, sliderPos - (float) x, 3.f, 1.5f);
    g.setColour (ink);
    g.fillEllipse (sliderPos - 7.f, cy - 7.f, 14.f, 14.f);
    g.setColour (paper);
    g.drawEllipse (sliderPos - 7.f, cy - 7.f, 14.f, 14.f, 1.5f);
}

void SeedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    using namespace ui;
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const bool solid = (bool) b.getProperties()["solid"];
    const float corner = r.getHeight() * 0.5f;
    g.setColour (solid ? paper.withAlpha (down ? 0.8f : (highlighted ? 0.94f : 1.f))
                       : paper.withAlpha (down ? 0.22f : (highlighted ? 0.12f : 0.04f)));
    g.fillRoundedRectangle (r, corner);
    g.setColour (paper);
    g.drawRoundedRectangle (r, corner, 1.2f);
}

void SeedLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    using namespace ui;
    const bool solid = (bool) b.getProperties()["solid"];
    const bool small = b.getHeight() < 30;
    g.setFont (font (small ? 14.f : 11.f, solid ? 3 : 2).withExtraKerningFactor (small ? 0.f : 0.09f));
    g.setColour (solid ? ink : paper);
    g.drawText (small ? b.getButtonText() : b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred);
}

juce::Font SeedLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return ui::font (h < 30 ? 14.f : 11.f, 2); }

void SeedLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    using namespace ui;
    const auto r = b.getLocalBounds().toFloat();
    const float s = 11.f;
    const juce::Rectangle<float> box (r.getX() + 2.f, r.getCentreY() - s * 0.5f, s, s);
    g.setColour (paper.withAlpha (highlighted ? 1.f : 0.8f));
    g.drawEllipse (box, 1.2f);
    if (b.getToggleState())
    {
        g.setColour (paper);
        g.fillEllipse (box.reduced (2.6f));
    }
    g.setColour (paper.withAlpha (0.8f));
    g.setFont (font (9.5f, 2).withExtraKerningFactor (0.12f));
    g.drawText (b.getButtonText().toUpperCase(), r.withTrimmedLeft (s + 7.f), juce::Justification::centredLeft);
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
    slider.getProperties().set ("group", (int) d.group);
    slider.getProperties().set ("bipolar", (d.flags & fBipolar) != 0);
    addAndMakeVisible (slider);

    slider.onDragStart = [this] { dragging = true; notify (1.f); };
    slider.onDragEnd   = [this] { dragging = false; notify (slider.isMouseOver (true) ? 0.5f : 0.f); };
    slider.addMouseListener (this, false);          // so hovering the dial reaches mouseEnter / mouseExit above

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, d.id, slider);
    slider.setDoubleClickReturnValue (true, d.def);
}

void Knob::resized() { slider.setBounds ((getWidth() - 46) / 2, 0, 46, 46); }

void Knob::paint (juce::Graphics& g)
{
    const auto& d = kParams[paramIndex];
    g.setFont (ui::font (9.5f, 2).withExtraKerningFactor (0.1f));
    g.setColour (ui::paper.withAlpha (0.85f));
    g.drawText (juce::String (d.id).toUpperCase(), 0, 48, getWidth(), 14, juce::Justification::centred);

    if (d.flags & fStar)
    {
        g.setColour (ui::paper);
        g.setFont (ui::font (14.f, 3));
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

    g.setColour (ui::ink.withAlpha (0.38f));
    g.fillRoundedRectangle (plot, 12.f);
    g.setColour (ui::paper.withAlpha (0.06f));                      // graticule
    for (int i = 1; i < 4; ++i)
    {
        g.drawLine (plot.getX() + 6.f, plot.getY() + plot.getHeight() * (float) i / 4.f, plot.getRight() - 6.f, plot.getY() + plot.getHeight() * (float) i / 4.f, 1.f);
        g.drawLine (plot.getX() + plot.getWidth() * (float) i / 4.f, plot.getY() + 6.f, plot.getX() + plot.getWidth() * (float) i / 4.f, plot.getBottom() - 6.f, 1.f);
    }
    g.setColour (ui::paper.withAlpha (0.2f));
    g.drawRoundedRectangle (plot.reduced (0.5f), 12.f, 1.f);

    if (draw) draw (g, plot.reduced (10.f, 8.f));

    if (fig.isNotEmpty())
    {
        g.setFont (ui::font (9.5f, 2).withExtraKerningFactor (0.12f));
        g.setColour (ui::paper.withAlpha (0.6f));
        g.drawText (("fig " + fig).toUpperCase(), cap.withWidth (50.f).withTrimmedLeft (4.f), juce::Justification::centredLeft);
        g.setColour (ui::paper.withAlpha (0.85f));
        g.drawText (caption.toUpperCase(), cap.withTrimmedLeft (54.f), juce::Justification::centredLeft);
    }
}

//==============================================================================
SeedTree::SeedTree() { startTimerHz (30); }

void SeedTree::timerCallback()
{
    constexpr float dt = 1.f / 30.f;
    const float twoPi = juce::MathConstants<float>::twoPi;
    timeS += dt;
    if (growth < 1.f) growth = std::min (1.f, growth + dt / 2.8f);

    // bulb values ease towards the knobs (so branches grow / recline smoothly); a changed value flashes its branch
    if (valueOf)
    {
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const float v = juce::jlimit (0.f, 1.f, valueOf (nodes[i].param));
            nodeV[i] = nodeV[i] < 0.f ? v : nodeV[i] + (v - nodeV[i]) * 0.3f;
        }
        for (int i = 0; i < seed::kNumParams; ++i)
        {
            const float v = valueOf (i);
            if (havePrev && growth >= 1.f && std::abs (v - prevV[i]) > 0.002f) flash[i] = 1.f;
            prevV[i] = v;
            flash[i] *= 0.9f;
            hl[i] += (interact[i] - hl[i]) * 0.35f;
        }
        havePrev = true;
    }

    // spin: one turn every kBeatsPerRev beats of the host tempo (120 bpm when there is no host)
    const HostInfo h = hostInfo ? hostInfo() : HostInfo {};
    const double bpm = h.bpm > 20.0 ? h.bpm : 120.0;
    spin += (float) (bpm / 60.0 / kBeatsPerRev) * dt * twoPi;

    if (h.playing && h.hasPpq)       // locked to the song position while the transport runs
    {
        const float target = (float) std::fmod (h.ppq / kBeatsPerRev, 1.0) * twoPi;
        float err = target - spin;
        err = std::remainder (err, twoPi);
        spin += err * 0.2f;
    }
    spin = std::fmod (spin + twoPi, twoPi);
    repaint();
}

void SeedTree::settle()
{
    for (size_t i = 0; i < nodes.size(); ++i)
        if (valueOf) nodeV[i] = juce::jlimit (0.f, 1.f, valueOf (nodes[i].param));
    for (int i = 0; i < seed::kNumParams; ++i) hl[i] = interact[i];
}

void SeedTree::setSeed (uint32_t s)
{
    if (s == rawSeed) return;
    rawSeed = s;
    treeSeed = s > 0 ? s : 29;
    growth = 0.f;
    rebuild();
}

void SeedTree::resized() { rebuild(); }

int SeedTree::zoneOfY (float y) const { return y < yA ? 2 : (y < ground ? 1 : 0); }

SeedTree::V3 SeedTree::bez (const Seg& s, float u) const
{
    const float v = 1.f - u;
    return { v * v * s.a.x + 2.f * v * u * s.c.x + u * u * s.b.x,
             v * v * s.a.y + 2.f * v * u * s.c.y + u * u * s.b.y,
             v * v * s.a.z + 2.f * v * u * s.c.z + u * u * s.b.z };
}

// world -> screen, with the tree turned by `spin` about its trunk, a little perspective and a slight downward camera tilt
juce::Point<float> SeedTree::project (V3 p, float& depth, float& scale) const
{
    const float h = juce::jlimit (0.f, 1.f, (ground - p.y) / (0.55f * H));
    const float k = h * h;                                           // wind: more sway higher up
    p.x += (std::sin (timeS * 0.9f + p.y * 0.02f) * 3.2f + std::sin (timeS * 2.1f + p.y * 0.05f) * 0.8f) * k;
    p.z += std::cos (timeS * 0.7f + p.y * 0.02f) * 2.5f * k;

    const float ca = std::cos (spin), sa = std::sin (spin);
    const float xr = p.x * ca + p.z * sa;
    const float zr = -p.x * sa + p.z * ca;                           // > 0 = away from the viewer
    scale = 900.f / (900.f + zr);
    depth = zr;
    const float cp = std::cos (kPitch), sp = std::sin (kPitch);
    return { (float) getWidth() * 0.5f + xr * scale, ground + ((p.y - ground) * cp - zr * sp) * scale };
}

void SeedTree::mouseDown (const juce::MouseEvent& e)
{
    if (home) return;
    const int z = zoneOfY (e.position.y);
    if (z != page) { page = z; if (onPage) onPage (z); }
    repaint();
}

void SeedTree::rebuild()
{
    segs.clear(); nodes.clear();
    const float w = (float) getWidth();
    if (w <= 0.f || getHeight() < 100) return;

    H = (float) getHeight() - 44.f;
    yA = topY + 0.34f * H;
    ground = topY + 0.70f * H;
    rMax = std::min (60.f, w * 0.5f - 22.f);
    const float pi = juce::MathConstants<float>::pi;

    seed::SplitMix r ((uint64_t) treeSeed * 0x9E3779B1ull + 0x7EEDull);
    auto rnd = [&r] { return r.next(); };

    auto add   = [] (V3 a, V3 b) { return V3 { a.x + b.x, a.y + b.y, a.z + b.z }; };
    auto mul   = [] (V3 a, float k) { return V3 { a.x * k, a.y * k, a.z * k }; };
    auto cross = [] (V3 a, V3 b) { return V3 { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; };
    auto norm  = [] (V3 a) { const float l = std::max (1e-5f, std::sqrt (a.x * a.x + a.y * a.y + a.z * a.z)); return V3 { a.x / l, a.y / l, a.z / l }; };

    // y grows downward (like the screen): "up" is -y
    std::function<V3 (V3, V3, float, int, int, float, bool, float, int)> grow;
    grow = [&] (V3 p, V3 dir, float len, int depth, int maxDepth, float thick, bool root, float t0, int parent) -> V3
    {
        V3 b = add (p, mul (dir, len));
        const float rr = std::sqrt (b.x * b.x + b.z * b.z);
        if (rr > rMax) { b.x *= rMax / rr; b.z *= rMax / rr; }                   // stay inside the cylinder
        if (b.y < topY + 8.f && p.y - b.y > 1.f)                                  // and below the top
        {
            const float f = std::max (0.15f, (p.y - (topY + 8.f)) / (p.y - b.y));
            b = { p.x + (b.x - p.x) * f, p.y + (b.y - p.y) * f, p.z + (b.z - p.z) * f };
        }
        b.y = juce::jlimit (topY + 6.f, topY + H - 8.f, b.y);

        const V3 mid = mul (add (p, b), 0.5f);
        const V3 side = norm (cross (norm (V3 { b.x - p.x, b.y - p.y, b.z - p.z }), V3 { rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f }));
        const V3 c = add (mid, mul (side, (rnd() - 0.5f) * 0.35f * len));

        const float dur = 0.10f + 0.07f * (len / 100.f);
        const int me = (int) segs.size();
        Seg sg { p, c, b, thick, t0, t0 + dur, 0, root, parent };
        sg.zone = zoneOfY (b.y);
        segs.push_back (sg);

        if (depth < maxDepth && len > 7.f)
        {
            const int n = rnd() < (root ? 0.22f : 0.38f) ? 3 : 2;
            const V3 ref = std::abs (dir.y) < 0.9f ? V3 { 0.f, 1.f, 0.f } : V3 { 1.f, 0.f, 0.f };
            const V3 u = norm (cross (dir, ref)), v = cross (dir, u);
            const float phi0 = rnd() * 2.f * pi;
            for (int k = 0; k < n; ++k)
            {
                const float phi = phi0 + (float) k * 2.f * pi / (float) n + (rnd() - 0.5f) * 0.6f;
                const float spread = 0.5f + 0.4f * rnd();
                V3 cd = add (mul (dir, std::cos (spread)), mul (add (mul (u, std::cos (phi)), mul (v, std::sin (phi))), std::sin (spread)));
                cd = norm (add (cd, V3 { 0.f, root ? 0.22f : -0.25f, 0.f }));    // roots sink, branches reach for the light
                grow (b, cd, len * (0.7f + 0.14f * rnd()), depth + 1, maxDepth, std::max (0.9f, thick * 0.72f), root, t0 + dur - 0.01f, me);
            }
        }
        return b;
    };

    // roots, fanning out around the seed
    const V3 seedPos { 0.f, ground + 16.f, 0.f };
    for (int k = 0; k < 5; ++k)
    {
        const float phi = (float) k * 2.f * pi / 5.f + rnd() * 0.5f, t = 0.8f + 0.5f * rnd();
        grow (seedPos, norm (V3 { std::sin (t) * std::cos (phi), std::cos (t), std::sin (t) * std::sin (phi) }),
              0.10f * H * (0.85f + 0.3f * rnd()), 1, 4, 3.4f, true, 0.02f, -1);
    }

    // trunk with spiralling side branches (page II) and a crown (page III)
    V3 p { 0.f, ground, 0.f };
    float t0 = 0.f;
    int trunkParent = -1;
    for (int i = 0; i < 5; ++i)
    {
        const float thick = 7.5f - 0.7f * (float) i;
        p = grow (p, norm (V3 { (rnd() - 0.5f) * 0.2f, -1.f, (rnd() - 0.5f) * 0.2f }), 0.062f * H * (0.97f + 0.08f * rnd()), 99, 99, thick, false, t0, trunkParent);
        t0 = segs.back().t1 - 0.01f;
        trunkParent = (int) segs.size() - 1;
        if (i >= 1 && i <= 3)
        {
            const float phi = (float) i * 2.4f + rnd() * 0.5f, t = 0.9f + 0.4f * rnd();
            grow (p, norm (V3 { std::sin (t) * std::cos (phi), -std::cos (t), std::sin (t) * std::sin (phi) }),
                  0.13f * H * (0.9f + 0.3f * rnd()), 1, 4, thick * 0.7f, false, t0, trunkParent);
        }
    }
    grow (p, norm (V3 { (rnd() - 0.5f) * 0.2f, -1.f, (rnd() - 0.5f) * 0.2f }), 0.13f * H, 1, 5, 4.2f, false, t0, trunkParent);

    float maxT = 0.01f;
    for (auto& s : segs) maxT = std::max (maxT, s.t1);
    for (auto& s : segs) { s.t0 /= maxT; s.t1 /= maxT; }

    // ---- one bulb per parameter, spread over the branches of its zone --------------------------
    struct Cand { int seg; float u; V3 pos; };
    std::vector<Cand> cands[3];
    for (int i = 0; i < (int) segs.size(); ++i)
    {
        if (segs[(size_t) i].thick > 5.2f) continue;                 // keep the trunk clear
        for (float u : { 1.f, 0.62f, 0.3f })
        {
            const auto pos = bez (segs[(size_t) i], u);
            cands[zoneOfY (pos.y)].push_back ({ i, u, pos });
        }
    }
    auto dist3 = [] (V3 a, V3 b) { return std::sqrt ((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); };

    for (int pg = 0; pg < 3; ++pg)
    {
        std::vector<int> params;
        for (int i = 0; i < kNumParams; ++i) if (kParams[i].page == pg) params.push_back (i);

        auto& c = cands[pg];
        for (int i = (int) c.size() - 1; i > 0; --i)                // Fisher-Yates with the tree's own rng
            std::swap (c[(size_t) i], c[(size_t) (rnd() * (float) (i + 1)) % (size_t) (i + 1)]);

        std::vector<Cand> chosen;
        std::vector<char> used (c.size(), 0);
        for (float minD : { 14.f, 9.f, 4.f, 0.f })
            for (size_t i = 0; i < c.size() && chosen.size() < params.size(); ++i)
            {
                if (used[i]) continue;
                bool tooClose = false;
                for (auto& o : chosen) if (dist3 (o.pos, c[i].pos) < minD) { tooClose = true; break; }
                if (tooClose) continue;
                used[i] = 1; chosen.push_back (c[i]);
            }

        std::sort (chosen.begin(), chosen.end(), [] (const Cand& a, const Cand& b)
                   { return a.pos.y != b.pos.y ? a.pos.y < b.pos.y : a.pos.x < b.pos.x; });
        for (size_t i = 0; i < chosen.size(); ++i)
            nodes.push_back ({ chosen[i].seg, chosen[i].u, params[i], rnd() * 6.28f });
    }
    prog.assign (segs.size(), 0.f);
    dsegs = segs;
    nodeV.assign (nodes.size(), -1.f);
    havePrev = false;
}

void SeedTree::paint (juce::Graphics& g)
{
    using namespace ui;
    const float w = (float) getWidth();
    if (segs.empty()) return;
    const float cx = w * 0.5f, bottom = topY + H;
    const float eg = 1.f - std::pow (1.f - growth, 3.f);
    const float nearK = 1.f / (2.f * rMax);

    // the ground plane: rings and spokes that turn with the tree
    {
        float d, sc;
        for (float rad : { 0.34f, 0.67f, 1.f })
        {
            juce::Path ring;
            for (int k = 0; k <= 48; ++k)
            {
                const float a = (float) k / 48.f * juce::MathConstants<float>::twoPi;
                const auto q = project ({ std::cos (a) * rad * rMax, ground, std::sin (a) * rad * rMax }, d, sc);
                if (k == 0) ring.startNewSubPath (q); else ring.lineTo (q);
            }
            g.setColour (rad == 1.f ? paper.withAlpha (0.35f) : paper.withAlpha (0.12f));
            g.strokePath (ring, juce::PathStrokeType (rad == 1.f ? 1.4f : 1.f));
        }
        g.setColour (paper.withAlpha (0.08f));
        for (int k = 0; k < 12; ++k)
        {
            const float a = (float) k / 12.f * juce::MathConstants<float>::twoPi;
            g.drawLine (juce::Line<float> (project ({ 0.f, ground, 0.f }, d, sc), project ({ std::cos (a) * rMax, ground, std::sin (a) * rMax }, d, sc)), 1.f);
        }
    }

    // home view: a ring of 32 beat ticks around the tree (a bar every 4) that turns with it; the bright bead marks beat 1
    if (home)
    {
        float d, sc;
        const float R = rMax * 3.8f, twoPi = juce::MathConstants<float>::twoPi;
        juce::Path ring;
        for (int k = 0; k <= 96; ++k)
        {
            const auto q = project ({ std::cos ((float) k / 96.f * twoPi) * R, ground, std::sin ((float) k / 96.f * twoPi) * R }, d, sc);
            if (k == 0) ring.startNewSubPath (q); else ring.lineTo (q);
        }
        g.setColour (paper.withAlpha (0.4f));
        g.strokePath (ring, juce::PathStrokeType (1.f));
        for (int k = 0; k < 32; ++k)
        {
            const float a = (float) k / 32.f * twoPi, bar = k % 4 == 0 ? 1.f : 0.f;
            const auto p0 = project ({ std::cos (a) * R, ground, std::sin (a) * R }, d, sc);
            const auto p1 = project ({ std::cos (a) * R * (1.f + 0.05f + 0.04f * bar), ground, std::sin (a) * R * (1.f + 0.05f + 0.04f * bar) }, d, sc);
            g.setColour (paper.withAlpha (0.4f + 0.45f * bar));
            g.drawLine (juce::Line<float> (p0, p1), 1.f + 0.6f * bar);
        }
        const auto bead = project ({ R, ground, 0.f }, d, sc);
        g.setColour (paper.withAlpha (0.25f));
        g.fillEllipse (bead.x - 9.f, bead.y - 9.f, 18.f, 18.f);
        g.setColour (paper);
        g.fillEllipse (bead.x - 3.5f, bead.y - 3.5f, 7.f, 7.f);
    }

    // floating spores
    for (int i = 0; i < 16; ++i)
    {
        const float fx = std::fmod ((float) i * 0.618034f, 1.f);
        const float x = 12.f + (w - 24.f) * fx + std::sin (timeS * 0.5f + (float) i) * 8.f;
        const float y = bottom - std::fmod (timeS * (5.f + (float) (i % 5) * 2.f) + (float) i * 37.f, H * 0.95f);
        const float a = 0.22f + 0.2f * std::sin (timeS * 1.3f + (float) i * 1.7f);
        g.setColour (paper.withAlpha (a * 0.3f));
        g.fillEllipse (x - 3.f, y - 3.f, 6.f, 6.f);
        g.setColour (paper.withAlpha (a));
        g.fillEllipse (x - 1.1f, y - 1.1f, 2.2f, 2.2f);
    }

    // The branch that carries a parameter follows its value: low = shorter and reclined (drooping), high = longer and lifted.
    // Whatever grows beyond it moves with it, so a long chain of twigs swings as one.
    {
        const size_t ns = segs.size();
        dsegs.resize (ns);
        cum.assign (ns, V3 {});
        segVal.assign (ns, 0.f); segCnt.assign (ns, 0); segHL.assign (ns, 0.f);
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& n = nodes[i];
            segVal[(size_t) n.seg] += nodeV[i] < 0.f ? 0.5f : nodeV[i];
            ++segCnt[(size_t) n.seg];
            segHL[(size_t) n.seg] = std::max (segHL[(size_t) n.seg], std::max (hl[n.param], flash[n.param]));
        }
        const float gk = juce::jlimit (0.f, 1.f, (growth - 0.6f) / 0.4f);      // eases in once the tree is grown
        for (size_t i = 0; i < ns; ++i)
        {
            const Seg& s = segs[i];
            const V3 pc = s.parent >= 0 ? cum[(size_t) s.parent] : V3 {};
            V3 d;
            if (segCnt[i] > 0 && gk > 0.f)
            {
                const float v = segVal[i] / (float) segCnt[i];
                const V3 dir { s.b.x - s.a.x, s.b.y - s.a.y, s.b.z - s.a.z };
                const float len = std::max (1e-3f, std::sqrt (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z));
                const float stretch = (0.78f + 0.44f * v - 1.f) * gk;            // 0.78x .. 1.22x
                const float lift = (v - 0.5f) * 0.6f * len * gk;                 // + = up (y points down on screen)
                d = { dir.x * stretch, dir.y * stretch - lift, dir.z * stretch };
            }
            cum[i] = { pc.x + d.x, pc.y + d.y, pc.z + d.z };
            dsegs[i] = s;
            dsegs[i].a = { s.a.x + pc.x, s.a.y + pc.y, s.a.z + pc.z };
            dsegs[i].c = { s.c.x + pc.x + 0.5f * d.x, s.c.y + pc.y + 0.5f * d.y, s.c.z + pc.z + 0.5f * d.z };
            dsegs[i].b = { s.b.x + cum[i].x, s.b.y + cum[i].y, s.b.z + cum[i].z };
        }
        for (size_t i = ns; i-- > 0;)                                           // light the path back down to the seed
            if (segs[i].parent >= 0)
                segHL[(size_t) segs[i].parent] = std::max (segHL[(size_t) segs[i].parent], segHL[i] * 0.6f);
    }

    // everything solid goes into one list and is painted far to near
    items.clear();
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const auto& s = segs[i];
        const float p = juce::jlimit (0.f, 1.f, (eg - s.t0) / std::max (1e-3f, s.t1 - s.t0));
        prog[i] = p;
        if (p <= 0.f) continue;
        float d, sc;
        project (bez (dsegs[i], p * 0.5f), d, sc);
        items.push_back ({ d, 0, (int) i });
    }
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& n = nodes[i];
        if (prog[(size_t) n.seg] - n.u <= 0.f) continue;
        float d, sc;
        project (bez (dsegs[(size_t) n.seg], n.u), d, sc);
        items.push_back ({ d - 0.5f, 1, (int) i });
    }
    items.push_back ({ 0.f, 2, 0 });
    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.depth > b.depth; });

    for (const auto& it : items)
    {
        if (it.kind == 0)
        {
            const auto& s = segs[(size_t) it.idx];
            const float p = prog[(size_t) it.idx];
            juce::Path path;
            float d = 0.f, sc = 1.f, dm = 0.f, scm = 1.f;
            for (int k = 0; k <= 8; ++k)
            {
                const auto q = project (bez (dsegs[(size_t) it.idx], p * (float) k / 8.f), d, sc);
                if (k == 4) { dm = d; scm = sc; }
                if (k == 0) path.startNewSubPath (q); else path.lineTo (q);
            }
            const float depthK = 0.45f + 0.55f * juce::jlimit (0.f, 1.f, 0.5f - dm * nearK);   // far side fades
            const float dim = (home || s.zone == page ? 1.f : 0.55f) * depthK;
            const float th = std::max (0.7f, s.thick * 0.36f) * scm;                           // fine line work, thicker near the trunk
            const float hv = segHL[(size_t) it.idx];
            if (hv > 0.02f)                                        // being edited: a soft halo and a brighter, heavier line
            {
                g.setColour (paper.withAlpha (0.22f * hv));
                g.strokePath (path, juce::PathStrokeType (th + 6.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (paper.withAlpha (juce::jlimit (0.f, 1.f, (s.root ? 0.34f : 0.55f) * dim + 0.45f * hv)));
            g.strokePath (path, juce::PathStrokeType (th + 0.9f * hv, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (it.kind == 1)
        {
            const auto& n = nodes[(size_t) it.idx];
            const float sg = juce::jlimit (0.f, 1.f, (prog[(size_t) n.seg] - n.u) / 0.15f);
            float d, sc;
            const auto pos = project (bez (dsegs[(size_t) n.seg], n.u), d, sc);
            const float v = valueOf ? juce::jlimit (0.f, 1.f, valueOf (n.param)) : 0.5f;
            const bool act = home || kParams[n.param].page == page;
            const float flick = 0.94f + 0.06f * std::sin (timeS * 17.f + n.phase);
            const float depthK = 0.55f + 0.45f * juce::jlimit (0.f, 1.f, 0.5f - d * nearK);
            const float hv = std::max (hl[n.param], flash[n.param]);
            const float dim = std::max ((act ? 1.f : 0.5f) * flick * depthK, hv);
            const float rad = (2.2f + 2.4f * v) * (1.f + 0.5f * hv) * sg * sc;

            if (v > 0.6f || hv > 0.05f)                                        // high values and the one being edited glow
            {
                g.setGradientFill (juce::ColourGradient (paper.withAlpha (0.3f * dim + 0.3f * hv), pos.x, pos.y, paper.withAlpha (0.f), pos.x + rad * 3.2f, pos.y, true));
                g.fillEllipse (pos.x - rad * 3.2f, pos.y - rad * 3.2f, rad * 6.4f, rad * 6.4f);
            }
            g.setColour (v > 0.6f ? paper.withAlpha (dim) : ink);
            g.fillEllipse (pos.x - rad, pos.y - rad, rad * 2.f, rad * 2.f);
            g.setColour (paper.withAlpha (juce::jlimit (0.f, 1.f, dim)));
            g.drawEllipse (pos.x - rad, pos.y - rad, rad * 2.f, rad * 2.f, 1.2f);
            if (hv > 0.05f)
            {
                g.setColour (paper.withAlpha (juce::jlimit (0.f, 1.f, hv)));
                g.drawEllipse (pos.x - rad - 3.f, pos.y - rad - 3.f, (rad + 3.f) * 2.f, (rad + 3.f) * 2.f, 1.f);
            }
        }
        else
        {
            // the seed, pulsing at the base of the trunk
            const juce::Point<float> sp { cx, ground + 16.f };
            const float pulse = 0.5f + 0.5f * std::sin (timeS * 2.2f);
            g.setGradientFill (juce::ColourGradient (paper.withAlpha (0.28f + 0.14f * pulse), sp.x, sp.y, paper.withAlpha (0.f), sp.x, sp.y + 24.f, true));
            g.fillEllipse (sp.x - 24.f, sp.y - 24.f, 48.f, 48.f);
            juce::Path seedShape;
            seedShape.addEllipse (-4.f, -6.5f, 8.f, 13.f);
            const auto tf = juce::AffineTransform::rotation (0.45f).translated (sp.x, sp.y);
            g.setColour (paper);
            g.fillPath (seedShape, tf);
        }
    }

    // name of the parameter being edited, next to its bulb
    for (const auto& n : nodes)
    {
        const float hv = hl[n.param];
        if (hv < 0.3f || prog[(size_t) n.seg] - n.u <= 0.f) continue;
        float d, sc;
        const auto pos = project (bez (dsegs[(size_t) n.seg], n.u), d, sc);
        const juce::String txt (kParams[n.param].id);
        const auto f = font (10.f, 2).withExtraKerningFactor (0.1f);
        const float tw = juce::GlyphArrangement::getStringWidth (f, txt.toUpperCase()) + 16.f;
        const bool right = pos.x < w * 0.5f;
        juce::Rectangle<float> box (right ? pos.x + 11.f : pos.x - 11.f - tw, pos.y - 9.f, tw, 18.f);
        box = box.withX (juce::jlimit (2.f, w - tw - 2.f, box.getX()));
        g.setColour (ink.withAlpha (0.7f * hv));
        g.fillRoundedRectangle (box, 9.f);
        g.setColour (paper.withAlpha (hv));
        g.drawRoundedRectangle (box, 9.f, 1.f);
        g.setFont (font (10.f, 2).withExtraKerningFactor (0.1f));
        g.drawText (txt.toUpperCase(), box, juce::Justification::centred);
    }

    // page zones (editor sidebar): thin dashed bands, the active one lightly filled
    const float zoneTop[3] = { ground, yA, topY };
    const float zoneBot[3] = { bottom, ground, yA };
    const char* roman[3] = { "I", "II", "III" };
    for (int pg = 0; pg < 3 && ! home; ++pg)
    {
        const juce::Rectangle<float> zone (3.f, zoneTop[pg] + 1.f, w - 6.f, zoneBot[pg] - zoneTop[pg] - 2.f);
        const bool active = pg == page;
        if (active) { g.setColour (paper.withAlpha (0.08f)); g.fillRoundedRectangle (zone, 8.f); }

        juce::Path box; box.addRoundedRectangle (zone, 8.f);
        juce::Path dashed;
        const float dashes[] = { 3.f, 4.f };
        juce::PathStrokeType (1.f).createDashedStroke (dashed, box, dashes, 2);
        g.setColour (paper.withAlpha (active ? 0.75f : 0.25f));
        g.fillPath (dashed);

        capsText (g, roman[pg], { (int) zone.getX() + 10, (int) zone.getY() + 6, 40, 14 }, 10.f, 2, paper.withAlpha (active ? 1.f : 0.55f), juce::Justification::topLeft, 0.16f);
    }

    g.setFont (font (10.f, 1));
    g.setColour (paper.withAlpha (0.7f));
    if (home) g.drawText (caption ? caption() : juce::String(), 0, getHeight() - 26, getWidth() / 2 - 104, 20, juce::Justification::centredRight);
    else      g.drawText (caption ? caption() : juce::String(), 0, getHeight() - 22, getWidth(), 18, juce::Justification::centred);
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
        if (! (kParams[i].flags & fSwitch))              // switches (mono) are buttons, not knobs
        {
            auto k = std::make_unique<Knob> (proc.apvts, i);
            k->onInteract = [this] (int idx, float level) { tree.setInteraction (idx, level); };
            addChildComponent (*k);
            knobs.push_back (std::move (k));
        }
        vals[i] = lastVals[i] = proc.getReal (i);
    }

    monoBtn.setTooltip ("Monophonic: one note at a time. Playing a new key while another is held glides legato to it (set the time with glide).");
    monoAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParams[mono].id, monoBtn);
    monoBtn.onStateChange = [this] { tree.setInteraction (mono, monoBtn.isDown() ? 1.f : (monoBtn.isOver() ? 0.5f : 0.f)); };
    addChildComponent (monoBtn);
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

    // ---- seed tree -------------------------------------------------------------------
    tree.valueOf = [this] (int idx) { return proc.getNormalised (idx); };
    tree.caption = [this] { return proc.getSeed() > 0 ? "seed #" + juce::String (proc.getSeed()) : juce::String ("seed  -"); };
    tree.onPage = [this] (int pg) { setPage (pg); };
    tree.hostInfo = [this] { return proc.getHostInfo(); };
    addChildComponent (tree);
    tree.setSeed (proc.getSeed());

    homeTree.setHome (true);
    homeTree.valueOf = tree.valueOf;
    homeTree.caption = tree.caption;
    homeTree.hostInfo = tree.hostInfo;
    addAndMakeVisible (homeTree);
    homeTree.setSeed (proc.getSeed());

    editBtn.setTooltip ("Open the sound editor: envelopes, oscillators, filter and effects");
    editBtn.onClick = [this] { setHomeView (! homeView); };
    addAndMakeVisible (editBtn);
    for (int i = 0; i < 3; ++i)
    {
        tabBtn[i].onClick = [this, i] { setPage (i); };
        addChildComponent (tabBtn[i]);
    }

    // ---- bottom bar ----------------------------------------------------------------
    randomizeBtn.onClick = [this] { proc.randomizeNew(); refreshLabels(); };
    tuneBtn.onClick = [this] { proc.correctTune(); };
    prevBtn.onClick = [this] { proc.stepPreset (-1); refreshLabels(); };
    nextBtn.onClick = [this] { proc.stepPreset (+1); refreshLabels(); };
    saveBtn.onClick = [this] { savePresetDialog(); };
    loadBtn.onClick = [this] { loadPresetDialog(); };
    randomizeBtn.getProperties().set ("solid", true);
    randomizeBtn.setTooltip ("Generate a new random patch (soft and light by default)");
    tuneBtn.setTooltip ("Snap oscillator A to octaves and B to perfect intervals so the keys play in tune");

    seedBox.setInputRestrictions (9, "0123456789");
    seedBox.setFont (ui::font (13.f, 1));
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
    presetLabel.setFont (ui::font (13.f, 2));
    presetLabel.setInterceptsMouseClicks (false, false);

    showPalette (seed::palette::fromSeed (proc.getSeed()));
    paletteSeed = proc.getSeed();

    for (auto* c : std::initializer_list<juce::Component*> { &randomizeBtn, &tuneBtn, &prevBtn, &nextBtn, &saveBtn, &loadBtn,
                                                              &seedBox, &chaosSlider, &autoTuneBtn, &presetLabel })
        addAndMakeVisible (c);

    resized();      // setSize() ran before the knobs/figures existed, so lay everything out now
    refreshLabels();
    setPage (0);
    setHomeView (true);
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

void SeedEditor::setHomeView (bool h)
{
    homeView = h;
    homeTree.setVisible (h);
    tree.setVisible (! h);
    editBtn.setButtonText (h ? "Edit sound" : "Tree");
    editBtn.setTooltip (h ? "Open the sound editor: envelopes, oscillators, filter and effects" : "Back to the spinning tree");
    for (auto& t : tabBtn) t.setVisible (! h);
    for (auto& k : knobs) k->setVisible (! h && kParams[k->paramIndex].page == page);
    for (auto& f : figures) f.comp->setVisible (! h && f.page == page);
    monoBtn.setVisible (! h && page == 1);
    rebuildBackdrop();
    repaint();
}

void SeedEditor::setPage (int newPage)
{
    page = newPage;
    tree.page = newPage;
    for (int i = 0; i < 3; ++i) tabBtn[i].getProperties().set ("solid", i == page);
    for (auto& t : tabBtn) t.repaint();
    for (auto& k : knobs) k->setVisible (! homeView && kParams[k->paramIndex].page == page);
    for (auto& f : figures) f.comp->setVisible (! homeView && f.page == page);
    monoBtn.setVisible (! homeView && page == 1);
    repaint();
}

juce::Rectangle<int> SeedEditor::chipRect (int i) const
{
    return juce::Rectangle<int> (26, 26).withCentre ({ 720 + 26 * i, 26 });
}

void SeedEditor::resized()
{
    tree.setBounds (8, 8, 164, 632);
    homeTree.setBounds (0, 8, kWidth, 632);

    for (auto& k : knobs)
    {
        const auto& p = kPos[k->paramIndex];
        k->setBounds (kContentX + p.x, kContentY + p.y, kKnobW, kKnobH);
    }
    for (auto& f : figures)
        f.comp->setBounds (f.rect.translated (kContentX, kContentY));

    // row 1: five equal pills; row 2: small controls
    const int bw = 116, bh = 40, gap = 10, x0 = (kWidth - (5 * bw + 4 * gap)) / 2, y = 652;
    juce::TextButton* row[5] = { &tuneBtn, &saveBtn, &loadBtn, &randomizeBtn, &editBtn };
    for (int i = 0; i < 5; ++i) row[i]->setBounds (x0 + i * (bw + gap), y, bw, bh);

    const int y2 = 702, h2 = 28;
    seedBox.setBounds (x0, y2, 84, h2);
    softLabelRect = { x0 + 96, y2, 32, h2 };
    chaosSlider.setBounds (x0 + 130, y2, 100, h2);
    wildLabelRect = { x0 + 234, y2, 32, h2 };
    autoTuneBtn.setBounds (x0 + 290, y2, 100, h2);
    prevBtn.setBounds (x0 + 420, y2, 26, h2);
    presetLabel.setBounds (x0 + 450, y2, 124, h2);
    nextBtn.setBounds (x0 + 578, y2, 26, h2);

    for (int i = 0; i < 3; ++i) tabBtn[i].setBounds (kContentX + i * 156, 10, 148, 32);
    monoBtn.setBounds (kContentX + 388, kContentY + 222, 84, 24);
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
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (lastDriftClock > 0.0) driftT += (float) std::min (0.25, now - lastDriftClock);     // a stalled host must not make it jump
        lastDriftClock = now;
        repaint();
    }
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
    followSeed();
    tree.setSeed (proc.getSeed());
    homeTree.setSeed (proc.getSeed());
    if (proc.getSeed() != lastSeedShown || proc.getPresetName() != lastPresetShown) refreshLabels();
}

// The palette follows the seed. A new seed glides to its colours over about half a second.
void SeedEditor::showPalette (const seed::Palette& p)
{
    palShown = p;
    ui::apply (p);
    lnf.applyPalette();
    sendLookAndFeelChange();
    rebuildBackdrop();
    repaint();
}

void SeedEditor::followSeed()
{
    const uint32_t sd = proc.getSeed();
    if (sd != paletteSeed)
    {
        paletteSeed = sd;
        palFrom = palShown;
        palTo = seed::palette::fromSeed (sd);
        palT = 0.f;
        palGliding = true;
    }
    if (palGliding)
    {
        palT = std::min (1.f, palT + 0.1f);
        const float e = palT * palT * (3.f - 2.f * palT);
        showPalette (palT >= 1.f ? palTo : seed::palette::mix (palFrom, palTo, e));
        if (palT >= 1.f) palGliding = false;
    }
}

// The whole window is one soft gradient: ink base, big blurred blobs of the seed's other colours that drift slowly
// around (painted live, every frame), then film grain and a vignette (built once).
void SeedEditor::rebuildBackdrop()
{
    if (backdrop.isValid()) return;
    const float W = (float) kWidth, H = (float) kHeight;
    backdrop = juce::Image (juce::Image::ARGB, kWidth, kHeight, true);

    {
        juce::Random rnd (7);
        juce::Image::BitmapData bd (backdrop, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
            {
                const int d = rnd.nextInt (13) - 6;
                bd.setPixelColour (x, y, d >= 0 ? juce::Colour ((juce::uint8) 255, (juce::uint8) 255, (juce::uint8) 255, (juce::uint8) (d * 2))
                                                : juce::Colour ((juce::uint8) 0, (juce::uint8) 0, (juce::uint8) 0, (juce::uint8) (-d * 2)));
            }
    }
    juce::Graphics g (backdrop);
    juce::ColourGradient v (juce::Colours::transparentBlack, W * 0.5f, H * 0.5f, juce::Colours::black.withAlpha (0.45f), 0.f, 0.f, true);
    v.addColour (0.55, juce::Colours::transparentBlack);
    g.setGradientFill (v);
    g.fillRect (0.f, 0.f, W, H);
}

void SeedEditor::paintBackdrop (juce::Graphics& g)
{
    using namespace ui;
    g.fillAll (ink);

    const float W = (float) kWidth, H = (float) kHeight;
    const float t = driftT;

    // one blob: a home position, a drift radius and its own slow loop (periods between roughly 50 s and 2 min)
    auto blob = [&] (juce::Colour c, float cx, float cy, float r, float a, float driftX, float driftY, float periodS, float phase)
    {
        const float w = juce::MathConstants<float>::twoPi / periodS;
        const float x = cx + driftX * std::sin (t * w + phase);
        const float y = cy + driftY * std::cos (t * w * 0.83f + phase * 1.7f);
        const float rr = r * (1.f + 0.08f * std::sin (t * w * 1.3f + phase));
        juce::ColourGradient gr (c.withAlpha (a), x, y, c.withAlpha (0.f), x + rr, y, true);
        gr.addColour (0.45, c.withAlpha (a * 0.55f));
        g.setGradientFill (gr);
        g.fillRect (0.f, 0.f, W, H);
    };

    if (homeView)
    {
        blob (red,   W * 0.12f, H * 0.92f, 520.f, 0.85f, 150.f, 110.f,  71.f, 0.0f);
        blob (slate, W * 0.88f, H * 0.12f, 470.f, 0.85f, 130.f, 120.f,  93.f, 1.9f);
        blob (mist,  W * 0.50f, H * 0.50f, 380.f, 0.22f, 220.f, 160.f, 117.f, 3.4f);
        blob (red,   W * 0.92f, H * 0.98f, 400.f, 0.65f, 140.f,  90.f,  59.f, 4.6f);
    }
    else
    {
        blob (red,   W * 0.88f, H * 0.92f, 520.f, 0.85f, 150.f, 110.f,  71.f, 0.0f);
        blob (slate, W * 0.10f, H * 0.10f, 470.f, 0.85f, 130.f, 120.f,  93.f, 1.9f);
        blob (mist,  W * 0.50f, H * 0.50f, 380.f, 0.18f, 220.f, 160.f, 117.f, 3.4f);
        blob (red,   W * 0.06f, H * 0.98f, 400.f, 0.65f, 140.f,  90.f,  59.f, 4.6f);
    }

    rebuildBackdrop();
    g.drawImageAt (backdrop, 0, 0);
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
    paintBackdrop (g);

    if (homeView)
        capsText (g, "Seedling", { 26, 14, 300, 24 }, 14.f, 3, paper, juce::Justification::centredLeft, 0.32f);

    // five dots: this seed's colours (just a readout, nothing to click)
    {
        const seed::Rgb cols[5] { palShown.ink, palShown.accent, palShown.slate, palShown.mist, palShown.paper };
        for (int i = 0; i < 5; ++i)
        {
            const auto r = chipRect (i).toFloat().reduced (7.f);
            g.setColour (juce::Colour (cols[i].r, cols[i].g, cols[i].b));
            g.fillEllipse (r);
            g.setColour (paper.withAlpha (0.55f));
            g.drawEllipse (r, 1.f);
        }
    }

    // signal-flow arrows on the oscillator page
    if (! homeView && page == 1)
    {
        g.setColour (paper.withAlpha (0.5f));
        const float ox = (float) kContentX, oy = (float) kContentY;

        g.drawLine (ox + 576.f, oy + 136.f, ox + 576.f, oy + 192.f, 1.2f);
        arrowHead (g, { ox + 576.f, oy + 202.f }, false);

        juce::Path conn;
        conn.startNewSubPath (ox + 330.f, oy + 290.f);
        conn.lineTo (ox + 330.f, oy + 280.f);
        conn.lineTo (ox + 51.f, oy + 280.f);
        conn.lineTo (ox + 51.f, oy + 276.f);
        g.strokePath (conn, juce::PathStrokeType (1.2f));
        arrowHead (g, { ox + 51.f, oy + 268.f }, true);
    }

    // bottom bar
    g.setColour (paper.withAlpha (0.22f));
    g.drawLine (138.f, 644.f, (float) kWidth - 138.f, 644.f, 1.f);
    capsText (g, "soft", softLabelRect, 9.f, 2, paper.withAlpha (0.75f), juce::Justification::centredRight);
    capsText (g, "wild", wildLabelRect, 9.f, 2, paper.withAlpha (0.75f), juce::Justification::centredLeft);
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
    strokeCurve (g, pts, ui::paper, false, 0.f, 1.6f);
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
    strokeCurve (g, pts, ui::paper, true, r.getBottom(), 1.6f);
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
    g.setColour (ui::paper.withAlpha (0.4f));
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
    g.setColour (ui::paper.withAlpha (0.55f));
    g.drawLine (r.getX(), zeroDb, r.getRight(), zeroDb, 1.f);
    strokeCurve (g, pts, ui::groupColour (gFlt), true, r.getBottom());
}

void SeedEditor::drawSaturate (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float side = std::min (r.getWidth(), r.getHeight());
    const auto sq = juce::Rectangle<float> (side, side).withCentre (r.getCentre());
    g.setColour (ui::paper.withAlpha (0.6f));
    g.drawLine (sq.getX(), sq.getCentreY(), sq.getRight(), sq.getCentreY(), 1.f);
    g.drawLine (sq.getCentreX(), sq.getY(), sq.getCentreX(), sq.getBottom(), 1.f);
    g.setColour (ui::paper.withAlpha (0.3f));
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
    g.setColour (ui::paper.withAlpha (0.55f));
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
    g.setColour (ui::paper.withAlpha (0.45f));
    g.drawLine (r.getX(), r.getCentreY(), r.getRight(), r.getCentreY(), 1.f);
    strokeCurve (g, pts, ui::groupColour (gAdj), true, r.getCentreY(), 1.8f);
}
