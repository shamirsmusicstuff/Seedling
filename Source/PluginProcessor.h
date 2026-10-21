#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include "Engine.h"
#include "Randomizer.h"

class SeedProcessor : public juce::AudioProcessor
{
public:
    SeedProcessor();
    ~SeedProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Seedling"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Parameter helpers (message thread). "real" = the value shown on the knob.
    float getReal (int index) const;
    float getNormalised (int index) const;
    void  setReal (int index, float realValue);
    void  setAllReal (const float* values);

    // Randomize / Correct Tune
    void randomize (uint32_t seedValue);      // deterministic: same seed + same softness = same patch
    void randomizeNew();                      // picks a fresh 6-digit seed
    void correctTune();                       // snap A to octaves and B to perfect intervals
    uint32_t getSeed() const { return currentSeed.load(); }

    // Tempo / transport as last seen from the host (audio thread writes, UI reads). bpm = 0 when the host gives none.
    struct HostInfo { double bpm = 0.0, ppq = 0.0; bool playing = false, hasPpq = false; };
    HostInfo getHostInfo() const { return { hostBpm.load(), hostPpq.load(), hostPlaying.load(), hostHasPpq.load() }; }

    // Presets (*.seedling, small XML files)
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    juce::File getPresetDirectory() const;
    std::vector<juce::File> listPresets() const;
    void ensureFactoryPresets();
    juce::String getPresetName() const { return presetName; }
    void stepPreset (int direction);          // +1 next, -1 previous in the preset folder

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static bool writePresetFile (const juce::File& file, const float* values, uint32_t seedValue, const juce::String& name);
    void handleMidi (const juce::MidiMessage&);

    seed::Engine engine;
    std::atomic<float>* raw[seed::kNumParams] {};
    std::atomic<float>* chaosParam = nullptr;
    std::atomic<float>* autoTuneParam = nullptr;
    std::atomic<uint32_t> currentSeed { 0 };
    std::atomic<double> hostBpm { 0.0 }, hostPpq { 0.0 };
    std::atomic<bool> hostPlaying { false }, hostHasPpq { false };
    juce::String presetName { "init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeedProcessor)
};
