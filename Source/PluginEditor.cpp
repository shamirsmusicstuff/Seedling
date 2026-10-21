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
        { 14, 484 }, { 90, 484 }, { 520, 484 }, { 596, 484 },                          // adj_bass adj_treb adj_pan adj_clip
    // Play mode (page II)
    { 300, 204 }, { 0, 0 }                                                         // glide (mono is a switch, placed in resized())
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
            g.setColour (c.withAlpha (0.2f));
            g.fillPath (f);
        }
        g.setColour (c.withAlpha (0.2f));
        g.strokePath (path, juce::PathStrokeType (thick + 4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

    // red neon text: a soft halo of offset copies underneath the crisp text
    void glowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, const juce::Font& f,
                   juce::Colour c, juce::Justification j)
    {
        g.setFont (f);
        const float radii[] = { 5.f, 3.f, 1.6f };
        const float alphas[] = { 0.05f, 0.09f, 0.16f };
        for (int k = 0; k < 3; ++k)
            for (int a = 0; a < 8; ++a)
            {
                const float ang = (float) a * juce::MathConstants<float>::pi * 0.25f;
                g.setColour (c.withAlpha (alphas[k]));
                g.drawText (text, area.translated ((int) std::lround (std::cos (ang) * radii[k]),
                                                   (int) std::lround (std::sin (ang) * radii[k])), j);
            }
        g.setColour (c.interpolatedWith (ui::paper, 0.18f));
        g.drawText (text, area, j);
    }
}

//==============================================================================
void ui::apply (const seed::Skin& k) { ink = k.ink; red = k.accent; slate = k.slate; mist = k.mist; paper = k.paper; }

juce::Font ui::serif (float height, bool italic, bool bold)
{
    const int style = (italic ? juce::Font::italic : 0) | (bold ? juce::Font::bold : 0);
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), height, style));
}

juce::Colour ui::groupColour (int group)
{
    switch (group)
    {
        case gVol: case gOscA: case gAdj:          return paper;
        case gMod: case gOscB: case gFlt:          return red;
        default:                                   return mist;    // env, lfo, fm, mix, sat, reverb
    }
}

//==============================================================================
SeedLookAndFeel::SeedLookAndFeel() { applyPalette(); }

void SeedLookAndFeel::applyPalette()
{
    using namespace ui;
    setColour (juce::TextButton::buttonColourId, ink);
    setColour (juce::TextButton::textColourOffId, paper);
    setColour (juce::TextButton::textColourOnId, paper);
    setColour (juce::TextEditor::backgroundColourId, ink);
    setColour (juce::TextEditor::textColourId, paper);
    setColour (juce::TextEditor::outlineColourId, slate.withAlpha (0.7f));
    setColour (juce::TextEditor::focusedOutlineColourId, red);
    setColour (juce::TextEditor::highlightColourId, red.withAlpha (0.5f));
    setColour (juce::CaretComponent::caretColourId, red);
    setColour (juce::Label::textColourId, paper);
    setColour (juce::BubbleComponent::backgroundColourId, ink);
    setColour (juce::BubbleComponent::outlineColourId, red);
    setColour (juce::TooltipWindow::backgroundColourId, ink);
    setColour (juce::TooltipWindow::outlineColourId, red);
    setColour (juce::TooltipWindow::textColourId, paper);
    setColour (juce::PopupMenu::backgroundColourId, ink);
    setColour (juce::PopupMenu::textColourId, paper);
    setColour (juce::PopupMenu::headerTextColourId, mist);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, red);
    setColour (juce::PopupMenu::highlightedTextColourId, paper);
    setColour (juce::TextButton::buttonColourId, ink);
    setColour (juce::TextButton::textColourOffId, paper);
    setColour (juce::TextButton::textColourOnId, paper);
}

void SeedLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    using namespace ui;
    const juce::Colour accent = groupColour ((int) s.getProperties()["group"]);
    const bool bipolar = (bool) s.getProperties()["bipolar"];

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float thick = 4.2f;
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f - thick * 0.5f - 1.f;
    const float ang = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (cx, cy, r, r, 0.f, startAngle, endAngle, true);
    g.setColour (slate.withAlpha (0.4f));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    juce::Path val;
    const float from = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
    if (std::abs (ang - from) > 0.01f)
    {
        val.addCentredArc (cx, cy, r, r, 0.f, std::min (from, ang), std::max (from, ang), true);
        g.setColour (accent.withAlpha (0.22f));
        g.strokePath (val, juce::PathStrokeType (thick + 4.f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    const float ri = r - thick * 0.5f - 2.f;
    g.setGradientFill (juce::ColourGradient (slate.interpolatedWith (ink, 0.45f), cx - ri * 0.4f, cy - ri * 0.5f,
                                             ink, cx + ri * 0.7f, cy + ri * 0.8f, true));
    g.fillEllipse (cx - ri, cy - ri, ri * 2.f, ri * 2.f);
    g.setColour (slate.withAlpha (0.8f));
    g.drawEllipse (cx - ri, cy - ri, ri * 2.f, ri * 2.f, 1.f);

    g.setColour (paper);
    g.drawLine (cx + std::sin (ang) * ri * 0.3f, cy - std::cos (ang) * ri * 0.3f,
                cx + std::sin (ang) * ri * 0.92f, cy - std::cos (ang) * ri * 0.92f, 2.2f);
}

void SeedLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                                        float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    using namespace ui;
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (slate.withAlpha (0.5f));
    g.fillRoundedRectangle ((float) x, cy - 2.f, (float) w, 4.f, 2.f);
    g.setColour (red.withAlpha (0.25f));
    g.fillRoundedRectangle ((float) x, cy - 4.f, sliderPos - (float) x, 8.f, 4.f);
    g.setColour (red);
    g.fillRoundedRectangle ((float) x, cy - 2.f, sliderPos - (float) x, 4.f, 2.f);
    g.setColour (paper);
    g.fillEllipse (sliderPos - 6.f, cy - 6.f, 12.f, 12.f);
    g.setColour (red);
    g.drawEllipse (sliderPos - 6.f, cy - 6.f, 12.f, 12.f, 1.4f);
}

void SeedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    using namespace ui;
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    if (highlighted || down)
    {
        g.setColour (red.withAlpha (0.25f));
        g.fillRoundedRectangle (r.expanded (1.5f), 6.f);
    }
    g.setColour (down ? red : ink.interpolatedWith (slate, 0.14f));
    g.fillRoundedRectangle (r, 4.f);
    g.setColour (highlighted || down ? paper : red);
    g.drawRoundedRectangle (r, 4.f, 1.3f);
}

juce::Font SeedLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return ui::serif (h < 22 ? 11.5f : 14.f, false, true).withExtraKerningFactor (0.04f); }

void SeedLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    using namespace ui;
    const auto r = b.getLocalBounds().toFloat();
    const float s = 14.f;
    const juce::Rectangle<float> box (r.getX() + 2.f, r.getCentreY() - s * 0.5f, s, s);
    g.setColour (highlighted ? paper : mist);
    g.drawRoundedRectangle (box, 3.f, 1.4f);
    if (b.getToggleState())
    {
        g.setColour (red.withAlpha (0.3f));
        g.fillRoundedRectangle (box.expanded (2.f), 4.f);
        g.setColour (red);
        g.fillRoundedRectangle (box.reduced (3.f), 2.f);
    }
    g.setColour (mist);
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
    slider.getProperties().set ("group", (int) d.group);
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
    g.setColour (ui::paper.withAlpha (0.88f));
    g.drawText (d.id, 0, 47, getWidth(), 16, juce::Justification::centred);

    if (d.flags & fStar)
    {
        g.setColour (ui::red);
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

    g.setColour (ui::ink);
    g.fillRoundedRectangle (plot, 3.f);
    g.setColour (ui::mist.withAlpha (0.07f));                       // graticule
    for (int i = 1; i < 4; ++i)
    {
        g.drawLine (plot.getX(), plot.getY() + plot.getHeight() * (float) i / 4.f, plot.getRight(), plot.getY() + plot.getHeight() * (float) i / 4.f, 1.f);
        g.drawLine (plot.getX() + plot.getWidth() * (float) i / 4.f, plot.getY(), plot.getX() + plot.getWidth() * (float) i / 4.f, plot.getBottom(), 1.f);
    }
    g.setColour (ui::slate.withAlpha (0.7f));
    g.drawRoundedRectangle (plot.reduced (0.5f), 3.f, 1.f);

    if (draw) draw (g, plot.reduced (8.f, 7.f));

    if (fig.isNotEmpty())
    {
        g.setFont (ui::serif (12.5f, true));
        g.setColour (ui::slate.brighter (0.3f));
        g.drawText ("figure " + fig, cap.withWidth (70.f), juce::Justification::centredLeft);
        g.setColour (ui::mist);
        g.setFont (ui::serif (12.5f));
        g.drawText (caption, cap.withTrimmedLeft (72.f), juce::Justification::centredLeft);
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
    std::function<V3 (V3, V3, float, int, int, float, bool, float)> grow;
    grow = [&] (V3 p, V3 dir, float len, int depth, int maxDepth, float thick, bool root, float t0) -> V3
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
        Seg sg { p, c, b, thick, t0, t0 + dur, 0, root };
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
                grow (b, cd, len * (0.7f + 0.14f * rnd()), depth + 1, maxDepth, std::max (0.9f, thick * 0.72f), root, t0 + dur - 0.01f);
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
              0.10f * H * (0.85f + 0.3f * rnd()), 1, 4, 3.4f, true, 0.02f);
    }

    // trunk with spiralling side branches (page II) and a crown (page III)
    V3 p { 0.f, ground, 0.f };
    float t0 = 0.f;
    for (int i = 0; i < 5; ++i)
    {
        const float thick = 7.5f - 0.7f * (float) i;
        p = grow (p, norm (V3 { (rnd() - 0.5f) * 0.2f, -1.f, (rnd() - 0.5f) * 0.2f }), 0.062f * H * (0.97f + 0.08f * rnd()), 99, 99, thick, false, t0);
        t0 = segs.back().t1 - 0.01f;
        if (i >= 1 && i <= 3)
        {
            const float phi = (float) i * 2.4f + rnd() * 0.5f, t = 0.9f + 0.4f * rnd();
            grow (p, norm (V3 { std::sin (t) * std::cos (phi), -std::cos (t), std::sin (t) * std::sin (phi) }),
                  0.13f * H * (0.9f + 0.3f * rnd()), 1, 4, thick * 0.7f, false, t0);
        }
    }
    grow (p, norm (V3 { (rnd() - 0.5f) * 0.2f, -1.f, (rnd() - 0.5f) * 0.2f }), 0.13f * H, 1, 5, 4.2f, false, t0);

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
}

void SeedTree::paint (juce::Graphics& g)
{
    using namespace ui;
    const float w = (float) getWidth();
    if (segs.empty()) return;
    const float cx = w * 0.5f, bottom = topY + H;
    const float eg = 1.f - std::pow (1.f - growth, 3.f);
    const float nearK = 1.f / (2.f * rMax);

    // soil glow under the horizon
    g.setGradientFill (juce::ColourGradient (red.withAlpha (0.20f), cx, ground, red.withAlpha (0.f), cx, bottom, false));
    g.fillRect (juce::Rectangle<float> (3.f, ground, w - 6.f, bottom - ground));

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
            g.setColour (rad == 1.f ? red.withAlpha (0.8f) : mist.withAlpha (0.16f));
            g.strokePath (ring, juce::PathStrokeType (rad == 1.f ? 1.4f : 1.f));
        }
        g.setColour (mist.withAlpha (0.12f));
        for (int k = 0; k < 12; ++k)
        {
            const float a = (float) k / 12.f * juce::MathConstants<float>::twoPi;
            g.drawLine (juce::Line<float> (project ({ 0.f, ground, 0.f }, d, sc), project ({ std::cos (a) * rMax, ground, std::sin (a) * rMax }, d, sc)), 1.f);
        }
    }

    // floating spores (the Upside Down)
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

    // everything solid goes into one list and is painted far to near
    items.clear();
    for (size_t i = 0; i < segs.size(); ++i)
    {
        const auto& s = segs[i];
        const float p = juce::jlimit (0.f, 1.f, (eg - s.t0) / std::max (1e-3f, s.t1 - s.t0));
        prog[i] = p;
        if (p <= 0.f) continue;
        float d, sc;
        project (bez (s, p * 0.5f), d, sc);
        items.push_back ({ d, 0, (int) i });
    }
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& n = nodes[i];
        if (prog[(size_t) n.seg] - n.u <= 0.f) continue;
        float d, sc;
        project (bez (segs[(size_t) n.seg], n.u), d, sc);
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
                const auto q = project (bez (s, p * (float) k / 8.f), d, sc);
                if (k == 4) { dm = d; scm = sc; }
                if (k == 0) path.startNewSubPath (q); else path.lineTo (q);
            }
            const float depthK = 0.45f + 0.55f * juce::jlimit (0.f, 1.f, 0.5f - dm * nearK);   // far side fades
            const float dim = (s.zone == page ? 1.f : 0.5f) * depthK;
            const float th = s.thick * scm;
            const juce::Colour c = s.root ? red : (s.thick > 5.f ? mist.interpolatedWith (slate, 0.4f) : mist);
            g.setColour (red.withAlpha (0.16f * dim));
            g.strokePath (path, juce::PathStrokeType (th + 5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (c.withAlpha (0.3f + 0.65f * dim));
            g.strokePath (path, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (it.kind == 1)
        {
            const auto& n = nodes[(size_t) it.idx];
            const float sg = juce::jlimit (0.f, 1.f, (prog[(size_t) n.seg] - n.u) / 0.15f);
            float d, sc;
            const auto pos = project (bez (segs[(size_t) n.seg], n.u), d, sc);
            const float v = valueOf ? juce::jlimit (0.f, 1.f, valueOf (n.param)) : 0.5f;
            const bool act = kParams[n.param].page == page;
            const juce::Colour c = groupColour (kParams[n.param].group);
            const float flick = 0.92f + 0.08f * std::sin (timeS * 17.f + n.phase);
            const float depthK = 0.5f + 0.5f * juce::jlimit (0.f, 1.f, 0.5f - d * nearK);
            const float dim = (act ? 1.f : 0.45f) * flick * depthK;
            const float rad = (2.4f + 3.4f * v) * sg * sc;

            g.setGradientFill (juce::ColourGradient (c.withAlpha ((0.15f + 0.5f * v) * dim), pos.x, pos.y, c.withAlpha (0.f), pos.x + rad * 3.4f, pos.y, true));
            g.fillEllipse (pos.x - rad * 3.4f, pos.y - rad * 3.4f, rad * 6.8f, rad * 6.8f);
            g.setColour (ink);
            g.fillEllipse (pos.x - rad - 1.2f, pos.y - rad - 1.2f, (rad + 1.2f) * 2.f, (rad + 1.2f) * 2.f);
            g.setColour (c.interpolatedWith (paper, 0.5f * v).withAlpha (juce::jlimit (0.f, 1.f, dim)));
            g.fillEllipse (pos.x - rad, pos.y - rad, rad * 2.f, rad * 2.f);
        }
        else
        {
            // the seed, pulsing at the base of the trunk
            const juce::Point<float> sp { cx, ground + 16.f };
            const float pulse = 0.5f + 0.5f * std::sin (timeS * 2.2f);
            g.setGradientFill (juce::ColourGradient (red.withAlpha (0.45f + 0.2f * pulse), sp.x, sp.y, red.withAlpha (0.f), sp.x, sp.y + 22.f, true));
            g.fillEllipse (sp.x - 22.f, sp.y - 22.f, 44.f, 44.f);
            juce::Path seedShape;
            seedShape.addEllipse (-5.f, -7.5f, 10.f, 15.f);
            const auto tf = juce::AffineTransform::rotation (0.45f).translated (sp.x, sp.y);
            g.setColour (mist.interpolatedWith (slate, 0.25f));
            g.fillPath (seedShape, tf);
            g.setColour (paper);
            g.strokePath (seedShape, juce::PathStrokeType (1.2f), tf);
            g.setColour (ink);
            juce::Path crack; crack.startNewSubPath (-1.f, -6.f); crack.lineTo (1.f, -1.f); crack.lineTo (-1.f, 3.f);
            g.strokePath (crack, juce::PathStrokeType (1.f), tf);
        }
    }

    // page boxes + roman numerals (III on top, growing up from I in the soil)
    const float zoneTop[3] = { ground, yA, topY };
    const float zoneBot[3] = { bottom, ground, yA };
    const char* roman[3] = { "I.", "II.", "III." };
    for (int pg = 0; pg < 3; ++pg)
    {
        const juce::Rectangle<float> zone (3.f, zoneTop[pg] + 1.f, w - 6.f, zoneBot[pg] - zoneTop[pg] - 2.f);
        const bool active = pg == page;

        juce::Path box; box.addRectangle (zone);
        juce::Path dashed;
        const float dashes[] = { 4.f, 3.f };
        juce::PathStrokeType (1.f).createDashedStroke (dashed, box, dashes, 2);
        g.setColour (active ? red : slate.withAlpha (0.55f));
        g.fillPath (dashed);

        auto label = juce::Rectangle<int> ((int) zone.getX() + 5, (int) zone.getY() + 2, 40, 20);
        if (active) glowText (g, roman[pg], label, serif (17.f, false, true), red, juce::Justification::topLeft);
        else { g.setFont (serif (17.f)); g.setColour (slate.brighter (0.2f)); g.drawText (roman[pg], label, juce::Justification::topLeft); }
    }

    g.setFont (serif (12.f, true));
    g.setColour (mist.withAlpha (0.7f));
    g.drawText (caption ? caption() : juce::String(), 0, getHeight() - 30, getWidth(), 20, juce::Justification::centred);
}

//==============================================================================
void Scanlines::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (ui::ink.withAlpha (0.f), b.getCentreX(), b.getCentreY(),
                                             ui::ink.withAlpha (0.6f), 0.f, 0.f, true));
    g.fillRect (b);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    for (float y = 0.f; y < b.getHeight(); y += 3.f)
        g.fillRect (0.f, y, b.getWidth(), 1.f);
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
            addChildComponent (*k);
            knobs.push_back (std::move (k));
        }
        vals[i] = lastVals[i] = proc.getReal (i);
    }

    monoBtn.setTooltip ("Monophonic: one note at a time. Playing a new key while another is held glides legato to it (set the time with glide).");
    monoAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, kParams[mono].id, monoBtn);
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
    addAndMakeVisible (tree);
    tree.setSeed (proc.getSeed());

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

    skinBtn.onClick = [this] { showSkinMenu(); };
    skinBtn.setTooltip ("Change the colour skin (skins are .seedskin files in Documents/Seedling/Skins)");
    loadSkins();

    for (auto* c : std::initializer_list<juce::Component*> { &skinBtn, &randomizeBtn, &tuneBtn, &prevBtn, &nextBtn, &saveBtn, &loadBtn,
                                                              &seedBox, &chaosSlider, &autoTuneBtn, &presetLabel })
        addAndMakeVisible (c);

    addAndMakeVisible (scan);   // on top of everything
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
    tree.page = newPage;
    for (auto& k : knobs) k->setVisible (kParams[k->paramIndex].page == page);
    for (auto& f : figures) f.comp->setVisible (f.page == page);
    monoBtn.setVisible (page == 1);
    repaint();
}

void SeedEditor::resized()
{
    tree.setBounds (8, 8, 164, 624);
    scan.setBounds (getLocalBounds());

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
    skinBtn.setBounds (762, 29, 112, 17);
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
    tree.setSeed (proc.getSeed());
    if (proc.getSeed() != lastSeedShown || proc.getPresetName() != lastPresetShown) refreshLabels();
}

void SeedEditor::loadSkins()
{
    juce::StringArray problems;
    skinList = seed::skins::loadAll (&problems);
    skinName = seed::skins::loadCurrentName();

    const seed::Skin* pick = &skinList.front();
    for (auto& k : skinList) if (k.name == skinName) { pick = &k; break; }
    chooseSkin (pick->name);

    if (problems.size() > 0)
        skinBtn.setTooltip ("Skins that could not be read: " + problems.joinIntoString ("; "));
}

void SeedEditor::chooseSkin (const juce::String& name)
{
    for (auto& k : skinList)
        if (k.name == name)
        {
            skinName = name;
            ui::apply (k);
            lnf.applyPalette();
            skinBtn.setButtonText ("skin: " + name);
            seed::skins::saveCurrentName (name);
            sendLookAndFeelChange();
            scan.repaint();
            repaint();
            return;
        }
}

void SeedEditor::showSkinMenu()
{
    juce::PopupMenu m;
    for (size_t i = 0; i < skinList.size(); ++i)
        m.addItem (100 + (int) i, skinList[i].name, true, skinList[i].name == skinName);
    m.addSeparator();
    m.addItem (1, "Open skins folder...");
    m.addItem (2, "Reload skins");

    juce::Component::SafePointer<SeedEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&skinBtn), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) seed::skins::dir().revealToUser();
        else if (r == 2) safe->loadSkins();
        else if (r >= 100 && (size_t) (r - 100) < safe->skinList.size()) safe->chooseSkin (safe->skinList[(size_t) (r - 100)].name);
    });
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
    g.fillAll (ink);
    g.setGradientFill (juce::ColourGradient (ink.interpolatedWith (slate, 0.22f), (float) kWidth * 0.62f, 300.f,
                                             ink, 0.f, 0.f, true));
    g.fillRect (getLocalBounds());

    // panel behind the content
    const juce::Rectangle<float> panel (178.f, 6.f, (float) kWidth - 190.f, 626.f);
    g.setColour (ink.interpolatedWith (slate, 0.12f));
    g.fillRoundedRectangle (panel, 6.f);
    g.setColour (slate.withAlpha (0.55f));
    g.drawRoundedRectangle (panel.reduced (0.5f), 6.f, 1.f);

    // neon page title between two thin red rules
    const juce::String title = (juce::String (kRoman[page]) + "   " + kPageTitles[page]).toUpperCase();
    glowText (g, title, { kContentX, 10, 480, 36 }, serif (25.f, false, true).withExtraKerningFactor (0.14f), red, juce::Justification::centred);
    for (float y : { 9.f, 47.f })
    {
        g.setColour (red.withAlpha (0.3f));
        g.drawLine (230.f, y, 630.f, y, 3.f);
        g.setColour (red);
        g.drawLine (230.f, y, 630.f, y, 1.f);
    }

    // wordmark
    glowText (g, "SEEDLING", { 690, 8, 184, 26 }, serif (20.f, false, true).withExtraKerningFactor (0.2f), red, juce::Justification::centredRight);

    // signal-flow arrows on the oscillator page
    if (page == 1)
    {
        g.setColour (mist.withAlpha (0.55f));
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
    g.setColour (red.withAlpha (0.25f));
    g.drawLine (14.f, 640.f, (float) kWidth - 10.f, 640.f, 3.f);
    g.setColour (red.withAlpha (0.8f));
    g.drawLine (14.f, 640.f, (float) kWidth - 10.f, 640.f, 1.f);
    g.setColour (mist.withAlpha (0.7f));
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
    strokeCurve (g, pts, ui::mist, false, 0.f, 1.6f);
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
    strokeCurve (g, pts, ui::mist, true, r.getBottom(), 1.6f);
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
    g.setColour (ui::mist.withAlpha (0.55f));
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
