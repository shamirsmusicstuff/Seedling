// Offscreen check of the plugin: renders the three editor pages to PNG and exercises
// randomize / Correct Tune / preset save+load through the real processor.
//   cmake --build . --target seedling_snapshot && ./seedling_snapshot_artefacts/seedling_snapshot <outdir>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
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
    proc.randomize (777);
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
    auto* editor = dynamic_cast<SeedEditor*> (ed.get());
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

    std::printf (failures == 0 ? "ALL CHECKS PASSED\n" : "%d CHECK(S) FAILED\n", failures);
    ed.reset();
    return failures == 0 ? 0 : 1;
}
