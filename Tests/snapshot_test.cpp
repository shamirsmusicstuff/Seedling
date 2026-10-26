// Offscreen check of the plugin: renders the three editor pages to PNG and exercises
// randomize / Correct Tune / preset save+load through the real processor.
//   cmake --build . --target seedling_snapshot && ./seedling_snapshot_artefacts/seedling_snapshot <outdir>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Skins.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    SeedProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    // --- randomize + Correct Tune -------------------------------------------------------
    proc.randomize (29);
    const float a29 = proc.getReal (seed::a_freq), b29 = proc.getReal (seed::b_freq);
    CHECK (std::fmod (a29, 12.f) == 0.f, "a_freq %.3f not octave after randomize", a29);
    float snapshot[seed::kNumParams];
    for (int i = 0; i < seed::kNumParams; ++i) snapshot[i] = proc.getReal (i);

    proc.randomize (29);
    for (int i = 0; i < seed::kNumParams; ++i)
        CHECK (std::abs (proc.getReal (i) - snapshot[i]) < 1e-4f, "seed 29 not reproducible at %s", seed::kParams[i].id);

    // detune by hand, then Correct Tune
    proc.setReal (seed::a_freq, 3.37f);
    proc.setReal (seed::b_freq, -9.2f);
    proc.correctTune();
    CHECK (std::abs (proc.getReal (seed::a_freq)) < 1e-3f, "A not snapped to octave: %.3f", proc.getReal (seed::a_freq));
    CHECK (std::abs (proc.getReal (seed::b_freq) - (-7.f)) < 1e-3f, "B not snapped to perfect interval: %.3f", proc.getReal (seed::b_freq));
    std::printf ("randomize/correct tune: ok (seed 29: a_freq %.1f, b_freq %.1f)\n", a29, b29);

    // --- preset save / load ---------------------------------------------------------------
    proc.setReal (seed::mono, 1.f);                       // Randomize must leave the play mode alone
    proc.randomize (777);
    CHECK (proc.getReal (seed::mono) == 1.f, "Randomize switched mono off");
    CHECK (proc.getReal (seed::glide) == 0.f, "Randomize changed glide");
    for (int i = 0; i < seed::kNumParams; ++i) snapshot[i] = proc.getReal (i);
    const auto file = outDir.getChildFile ("roundtrip.seedling");
    CHECK (proc.savePreset (file), "savePreset failed");
    proc.randomize (5);
    CHECK (proc.loadPreset (file), "loadPreset failed");
    for (int i = 0; i < seed::kNumParams; ++i)
        CHECK (std::abs (proc.getReal (i) - snapshot[i]) < 1e-4f, "preset mismatch at %s: %f vs %f", seed::kParams[i].id, proc.getReal (i), snapshot[i]);
    CHECK (proc.getSeed() == 777, "seed not restored from preset (%u)", proc.getSeed());
    std::printf ("preset round trip: ok (%s)\n", file.getFullPathName().toRawUTF8());

    // --- host state save / load -----------------------------------------------------------
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    proc.randomize (99);
    proc.setStateInformation (state.getData(), (int) state.getSize());
    for (int i = 0; i < seed::kNumParams; ++i)
        CHECK (std::abs (proc.getReal (i) - snapshot[i]) < 1e-4f, "host state mismatch at %s", seed::kParams[i].id);
    std::printf ("host state round trip: ok (%d bytes)\n", (int) state.getSize());

    // --- compatibility with files / sessions saved by the previous version (no glide / mono) ----------
    {
        // a host session without the two new parameters still loads, and they come up off
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        CHECK (xml != nullptr, "state is not XML");
        if (xml != nullptr)
        {
            for (int i = xml->getNumChildElements() - 1; i >= 0; --i)
            {
                auto* c = xml->getChildElement (i);
                const auto id = c->getStringAttribute ("id");
                if (id == "glide" || id == "mono") xml->removeChildElement (c, true);
            }
            juce::MemoryBlock oldState;
            juce::AudioProcessor::copyXmlToBinary (*xml, oldState);
            SeedProcessor fresh;
            fresh.setStateInformation (oldState.getData(), (int) oldState.getSize());
            for (int i = 0; i < seed::kNumParams; ++i)
            {
                if (i == seed::glide || i == seed::mono) continue;
                CHECK (std::abs (fresh.getReal (i) - snapshot[i]) < 1e-4f, "old session mismatch at %s", seed::kParams[i].id);
            }
            CHECK (fresh.getReal (seed::glide) == 0.f && fresh.getReal (seed::mono) == 0.f, "glide/mono should default to off for old sessions");
            CHECK (fresh.getSeed() == 777, "old session lost its seed");
            std::printf ("old host session (no glide/mono): ok\n");
        }

        // a preset file written by the previous version (argv[2]) loads exactly
        if (argc > 2)
        {
            const juce::File oldPreset (argv[2]);
            auto px = juce::XmlDocument::parse (oldPreset);
            CHECK (px != nullptr, "cannot read %s", argv[2]);
            if (px != nullptr)
            {
                SeedProcessor fresh;
                CHECK (fresh.loadPreset (oldPreset), "old preset failed to load");
                int checked = 0;
                for (auto* e : px->getChildWithTagNameIterator ("P"))
                    for (int i = 0; i < seed::kNumParams; ++i)
                        if (e->getStringAttribute ("id") == seed::kParams[i].id)
                        {
                            CHECK (std::abs (fresh.getReal (i) - (float) e->getDoubleAttribute ("v")) < 1e-4f, "old preset value differs at %s", seed::kParams[i].id);
                            ++checked;
                        }
                CHECK (checked == seed::kNumParams - 2, "expected %d old parameters, matched %d", seed::kNumParams - 2, checked);
                CHECK (fresh.getReal (seed::glide) == 0.f && fresh.getReal (seed::mono) == 0.f, "old preset should leave glide/mono off");
                CHECK (fresh.getSeed() == (uint32_t) px->getIntAttribute ("seed"), "old preset seed not restored");
                std::printf ("old preset file (%d params, seed %d): ok\n", checked, px->getIntAttribute ("seed"));
            }
        }
    }

    // --- audio through the processor ------------------------------------------------------
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        float peak = 0.f;
        for (int b = 0; b < 40; ++b)
        {
            proc.processBlock (buf, midi);
            midi.clear();
            peak = std::max (peak, buf.getMagnitude (0, 512));
        }
        CHECK (peak > 0.01f && peak <= 1.0f, "unexpected peak %.4f", peak);
        std::printf ("processBlock: peak %.3f\n", peak);
    }

    // --- editor snapshots -----------------------------------------------------------------
    proc.randomize (29);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* frame = dynamic_cast<SeedEditor*> (ed.get());
    auto* editor = &frame->view;
    editor->setGrowthForTest (1.f);
    for (int page = 0; page < 3; ++page)
    {
        editor->showPageForTest (page);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto png = outDir.getChildFile ("page" + juce::String (page + 1) + ".png");
        png.deleteFile();
        juce::FileOutputStream fo (png);
        juce::PNGImageFormat().writeImageToStream (img, fo);
        std::printf ("wrote %s\n", png.getFullPathName().toRawUTF8());
    }

    // a frame from the middle of the growth animation, and a different seed's tree
    editor->showPageForTest (1);
    editor->setGrowthForTest (0.2f);
    {
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto png = outDir.getChildFile ("growing.png");
        png.deleteFile();
        juce::FileOutputStream fo (png);
        juce::PNGImageFormat().writeImageToStream (img, fo);
    }
    proc.randomize (4242);
    editor->syncForTest();             // the editor timer would do this: the tree regrows for the new seed
    editor->setGrowthForTest (1.f);
    {
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto png = outDir.getChildFile ("seed4242.png");
        png.deleteFile();
        juce::FileOutputStream fo (png);
        juce::PNGImageFormat().writeImageToStream (img, fo);
    }

    // --- skins: parsing, roles, files ---------------------------------------------------------
    {
        using namespace seed;
        Skin k; juce::String err;
        const juce::Colour ink (0xff0c120c), red (0xffc20114), slate (0xff6d7275), mist (0xffc7d6d5), paper (0xffecebf3);

        CHECK (skins::parse ("https://coolors.co/0c120c-c20114-6d7275-c7d6d5-ecebf3", "url", k, err), "coolors URL: %s", err.toRawUTF8());
        CHECK (k.ink == ink && k.accent == red && k.slate == slate && k.mist == mist && k.paper == paper, "coolors URL roles wrong");

        CHECK (skins::parse ("{ \"name\": \"J\", \"colors\": [\"ECEBF3\",\"C20114\",\"0C120C\",\"C7D6D5\",\"6D7275\"] }", "x", k, err), "json: %s", err.toRawUTF8());
        CHECK (k.name == "J" && k.ink == ink && k.accent == red && k.slate == slate && k.mist == mist && k.paper == paper, "shuffled colours must give the same roles");

        CHECK (skins::parse ("name: Loose\n// comment\n#202030, #39304A  635C51 7D7461\nB0A990", "x", k, err), "plain text: %s", err.toRawUTF8());
        CHECK (k.name == "Loose" && k.ink == juce::Colour (0xff202030) && k.paper == juce::Colour (0xffb0a990), "plain text skin wrong");

        CHECK (skins::parse ("{ \"colors\": [\"000000\",\"111111\",\"222222\",\"333333\",\"444444\"], \"roles\": { \"ink\": \"010101\", \"accent\": \"ff0000\", \"slate\": \"020202\", \"mist\": \"030303\", \"paper\": \"fefefe\" } }", "R", k, err), "roles: %s", err.toRawUTF8());
        CHECK (k.name == "R" && k.accent == juce::Colour (0xffff0000), "explicit roles ignored");

        CHECK (! skins::parse ("{ \"colors\": [\"123\", \"zzzzzz\"] }", "bad", k, err) && err.isNotEmpty(), "bad skin accepted");
        CHECK (! skins::parse ("{ not json", "bad", k, err), "broken json accepted");
        CHECK (! skins::parse ("hello world", "bad", k, err), "text without colours accepted");

        // the original palette must keep its exact roles when it is auto-assigned; the three new palettes must be readable
        const auto all = skins::builtins();
        CHECK (all.size() == 4, "expected 4 built-in skins, got %d", (int) all.size());
        CHECK (all[0].ink == ink && all[0].accent == red && all[0].slate == slate && all[0].mist == mist && all[0].paper == paper, "default skin roles changed");
        for (auto& sk : all)
        {
            const float text = skins::contrast (sk.paper, sk.ink), accent = skins::contrast (sk.accent, sk.ink);
            std::printf ("skin %-9s text contrast %.1f  accent contrast %.1f  (ink %s accent %s)\n", sk.name.toRawUTF8(), text, accent,
                         sk.ink.toDisplayString (false).toRawUTF8(), sk.accent.toDisplayString (false).toRawUTF8());
            CHECK (text >= 4.5f, "skin %s: text contrast only %.1f", sk.name.toRawUTF8(), text);
            CHECK (accent >= 2.5f, "skin %s: accent contrast only %.1f", sk.name.toRawUTF8(), accent);
        }

        juce::StringArray problems;
        const auto loaded = skins::loadAll (&problems);
        CHECK (loaded.size() >= 4 && problems.isEmpty(), "skins folder: %d skins, %d problems", (int) loaded.size(), problems.size());
        CHECK (skins::dir().getChildFile ("README.txt").existsAsFile(), "skins README not written");
        std::printf ("skins folder: %s (%d skins)\n", skins::dir().getFullPathName().toRawUTF8(), (int) loaded.size());

        // render every skin, and the tree at two spin angles
        editor->setGrowthForTest (1.f);
        editor->showPageForTest (1);
        for (auto& sk : loaded)
        {
            editor->setSkinForTest (sk);
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            const auto png = outDir.getChildFile ("skin_" + sk.name.replaceCharacter (' ', '_') + ".png");
            png.deleteFile();
            juce::FileOutputStream fo (png);
            juce::PNGImageFormat().writeImageToStream (img, fo);
        }
        editor->setSkinForTest (loaded.front());
        editor->showPageForTest (0);
        for (float a : { 0.f, 1.2f, 2.4f })
        {
            editor->setSpinForTest (a);
            auto img = editor->createComponentSnapshot (editor->getLocalBounds().removeFromLeft (180), true, 1.0f);
            const auto png = outDir.getChildFile ("spin_" + juce::String ((int) (a * 10)) + ".png");
            png.deleteFile();
            juce::FileOutputStream fo (png);
            juce::PNGImageFormat().writeImageToStream (img, fo);
        }
    }

    // --- branches follow the values; the one being edited lights up ---------------------------------
    {
        using namespace seed;
        editor->setGrowthForTest (1.f);
        editor->showPageForTest (1);
        editor->setSpinForTest (0.6f);
        auto strip = [&] (const char* name)
        {
            auto img = editor->createComponentSnapshot (editor->getLocalBounds().removeFromLeft (180), true, 1.0f);
            const auto png = outDir.getChildFile (name);
            png.deleteFile();
            juce::FileOutputStream fo (png);
            juce::PNGImageFormat().writeImageToStream (img, fo);
            return img;
        };
        auto diffPixels = [] (const juce::Image& a, const juce::Image& b)
        {
            int n = 0;
            for (int y = 0; y < a.getHeight(); ++y)
                for (int x = 0; x < a.getWidth(); ++x)
                    if (a.getPixelAt (x, y).getARGB() != b.getPixelAt (x, y).getARGB()) ++n;
            return n;
        };
        auto setPage2 = [&] (float level)
        {
            for (int i = 0; i < kNumParams; ++i)
                if (kParams[i].page == 1 && ! (kParams[i].flags & fSwitch))
                    proc.setReal (i, kParams[i].lo + (kParams[i].hi - kParams[i].lo) * level);
            editor->interactForTest (-1, 0.f);       // settle the eased values
        };

        setPage2 (0.f);  const auto low  = strip ("branch_low.png");
        setPage2 (1.f);  const auto high = strip ("branch_high.png");
        setPage2 (0.7f); const auto base = strip ("branch_plain.png");
        editor->interactForTest (b_form, 1.f);
        const auto lit = strip ("branch_highlight.png");
        const int dLowHigh = diffPixels (low, high), dLit = diffPixels (base, lit);
        std::printf ("branches: low-vs-high differ in %d px, highlight changes %d px\n", dLowHigh, dLit);
        CHECK (dLowHigh > 2000, "branches do not respond to the values (%d px)", dLowHigh);
        CHECK (dLit > 300, "highlight not visible (%d px)", dLit);
        editor->interactForTest (b_form, 0.f);
        CHECK (diffPixels (base, strip ("branch_plain2.png")) == 0, "highlight did not switch off again");
    }

    // --- the two views, in every built-in skin (these are the README screenshots) ----------------------------
    {
        using namespace seed;
        std::unique_ptr<juce::AudioProcessorEditor> fresh (proc.createEditor());
        auto* fe = dynamic_cast<SeedEditor*> (fresh.get());
        CHECK (fe->view.isHomeView(), "the editor must open on the home view (spinning tree)");
        fresh.reset();

        proc.randomize (29);
        editor->syncForTest();
        editor->setGrowthForTest (1.f);
        editor->setSpinForTest (0.9f);
        editor->interactForTest (-1, 0.f);
        const auto dir = outDir.getChildFile ("readme");
        dir.createDirectory();
        auto save = [] (const juce::Image& img, const juce::File& f)
        {
            f.deleteFile();
            juce::FileOutputStream fo (f);
            juce::PNGImageFormat().writeImageToStream (img, fo);
        };
        juce::Image homeFirst;
        for (auto& sk : skins::builtins())
        {
            const auto n = sk.name.replaceCharacter (' ', '_');
            editor->setSkinForTest (sk);
            editor->showHomeForTest (true);
            auto home = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            save (home, dir.getChildFile ("home_" + n + ".png"));
            editor->showPageForTest (1);
            auto edit = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            save (edit, dir.getChildFile ("edit_" + n + ".png"));
            if (! homeFirst.isValid()) homeFirst = home;
            int d = 0;
            for (int y = 0; y < home.getHeight(); y += 3)
                for (int x = 0; x < home.getWidth(); x += 3)
                    if (home.getPixelAt (x, y) != edit.getPixelAt (x, y)) ++d;
            CHECK (d > 3000, "home and edit views look the same in skin %s", sk.name.toRawUTF8());
        }
        editor->showHomeForTest (true);
        // seed colours: every seed grows its own palette (the "Seed colours" choice)
        for (uint32_t sd : { 0u, 7u, 29u, 4242u })
        {
            editor->setSeedColoursForTest (sd);
            editor->showHomeForTest (true);
            auto home = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            save (home, dir.getChildFile ("home_seedcolours" + juce::String ((int) sd) + ".png"));
            editor->showPageForTest (1);
            auto edit = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            save (edit, dir.getChildFile ("edit_seedcolours" + juce::String ((int) sd) + ".png"));
        }
        {
            auto diffPx = [] (const juce::Image& a, const juce::Image& b)
            {
                int d = 0;
                for (int y = 0; y < a.getHeight(); y += 2)
                    for (int x = 0; x < a.getWidth(); x += 2)
                        if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) ++d;
                return d;
            };
            editor->showHomeForTest (true);
            editor->setSeedColoursForTest (7);
            auto a = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            editor->setSeedColoursForTest (4242);
            auto b = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            CHECK (diffPx (a, b) > 5000, "different seeds should look different");

            // following the seed: randomize, let the timer run (the glide takes ~10 ticks), same picture as that seed's palette
            proc.randomize (31);
            for (int i = 0; i < 14; ++i) editor->syncForTest();
            auto followed = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            editor->setSeedColoursForTest (31);
            auto direct = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            CHECK (diffPx (followed, direct) < 200, "seed mode did not follow the new seed (%d px differ)", diffPx (followed, direct));

            // a fixed skin does NOT follow the seed
            editor->setSkinForTest (skins::builtins()[1]);
            auto fixed1 = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            proc.randomize (32);
            for (int i = 0; i < 14; ++i) editor->syncForTest();
            auto fixed2 = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            CHECK (diffPx (fixed1, fixed2) < 3000, "a chosen skin must not change with the seed");
            proc.randomize (29);
            editor->syncForTest();
        }
        editor->setSkinForTest (skins::builtins().front());
        std::printf ("wrote README screenshots to %s\n", dir.getFullPathName().toRawUTF8());
    }

    // --- the background gradient drifts slowly, and is repeatable for a given moment ------------------------
    {
        editor->showHomeForTest (true);
        editor->setSkinForTest (seed::skins::builtins()[2]);
        auto shot = [&] (double t) { editor->setDriftForTest (t); return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f); };
        auto diff = [] (const juce::Image& a, const juce::Image& b)
        {
            int d = 0;
            for (int y = 0; y < a.getHeight(); y += 4)
                for (int x = 0; x < a.getWidth(); x += 4)
                    if (a.getPixelAt (x, y).getRed() != b.getPixelAt (x, y).getRed()) ++d;
            return d;
        };
        const auto t0 = shot (0.0), t0b = shot (0.0), t5 = shot (5.0), t30 = shot (30.0);
        CHECK (diff (t0, t0b) == 0, "same drift time must give the same picture");
        CHECK (diff (t0, t5) > 500, "gradient did not move in 5 beats");
        CHECK (diff (t0, t30) > diff (t0, t5), "gradient should keep travelling");
        std::printf ("drift: 5 beats move %d px, 30 beats move %d px (sampled)\n", diff (t0, t5), diff (t0, t30));
        editor->setDriftForTest (0.0);
        editor->setSkinForTest (seed::skins::builtins().front());
    }

    // --- the window resizes with a drag and everything scales to fit ------------------------------------------
    {
        CHECK (frame->isResizable(), "editor must be resizable");
        const int w0 = frame->getWidth(), h0 = frame->getHeight();
        CHECK (w0 == SeedEditor::kBaseW && h0 == SeedEditor::kBaseH, "default size %dx%d", w0, h0);
        frame->setSize (1350, 1116);
        const float sx = frame->view.getTransform().getScaleFactor();
        CHECK (std::abs (sx - 1.5f) < 0.001f, "content scale %.3f at 1350 px wide", sx);
        auto big = frame->createComponentSnapshot (frame->getLocalBounds(), true, 1.0f);
        CHECK (big.getWidth() == 1350 && big.getHeight() == 1116, "big snapshot %dx%d", big.getWidth(), big.getHeight());
        // a mouse position in the big window must land on the same control as the matching one in the small window
        const auto p = frame->view.getLocalPoint (frame, juce::Point<int> (1350 - 10, 1116 - 10));
        CHECK (std::abs (p.x - (SeedEditor::kBaseW - 7)) <= 1 && std::abs (p.y - (SeedEditor::kBaseH - 7)) <= 1, "mouse mapping off: %d,%d", p.x, p.y);
        frame->setSize (450, 372);
        CHECK (std::abs (frame->view.getTransform().getScaleFactor() - 0.5f) < 0.001f, "half-size scale wrong");
        frame->setSize (w0, h0);
        std::printf ("resize: 0.5x, 1x, 1.5x ok\n");
    }

    std::printf (failures == 0 ? "ALL CHECKS PASSED\n" : "%d CHECK(S) FAILED\n", failures);
    ed.reset();
    return failures == 0 ? 0 : 1;
}
