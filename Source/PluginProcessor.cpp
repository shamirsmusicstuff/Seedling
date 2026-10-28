#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

using namespace seed;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SeedProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& d : kParams)
    {
        if (d.flags & fSwitch)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { d.id, 1 }, d.name, d.def > 0.5f));
            continue;
        }

        if (d.flags & fStep)
        {
            juce::StringArray names;
            if (juce::String (d.id) == "src_type") for (auto* n : kSourceNames) names.add (n);
            else                                    for (auto* n : kReverbNames) names.add (n);
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { d.id, 1 }, d.name, names, (int) d.def));
            continue;
        }

        const bool semi = (d.flags & fSemitone) != 0;
        auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [semi] (float v, int) { return juce::String (v, 2) + (semi ? " st" : ""); });

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { d.id, 1 }, d.name,
            juce::NormalisableRange<float> (d.lo, d.hi), d.def, attrs));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kChaosId, 1 }, "Randomize Softness (soft-wild)",
        juce::NormalisableRange<float> (0.f, 1.f), 0.2f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kAutoTuneId, 1 }, "Auto Correct Tune", true));

    return layout;
}

SeedProcessor::SeedProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SEEDLING", createLayout())
{
    for (int i = 0; i < kNumParams; ++i)
        raw[i] = apvts.getRawParameterValue (kParams[i].id);

    chaosParam = apvts.getRawParameterValue (kChaosId);
    autoTuneParam = apvts.getRawParameterValue (kAutoTuneId);
}

//==============================================================================
void SeedProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
}

bool SeedProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SeedProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                         engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())                   engine.noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    else if (m.isPitchWheel())                engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.f * 2.f);
    else if (m.isControllerOfType (64))       engine.setSustain (m.getControllerValue() >= 64);
}

void SeedProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) hostBpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { hostPpq = *ppq; hostHasPpq = true; }
            else hostHasPpq = false;
            hostPlaying = pos->getIsPlaying();
        }

    float values[kNumParams];
    for (int i = 0; i < kNumParams; ++i) values[i] = raw[i]->load();
    engine.setParams (values);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos) { engine.render (L + pos, R + pos, t - pos); pos = t; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) engine.render (L + pos, R + pos, n - pos);

    midi.clear();
}

juce::AudioProcessorEditor* SeedProcessor::createEditor() { return new SeedEditor (*this); }

//==============================================================================
static juce::String tableToBase64 (const seed::Wavetable& t)
{
    const auto bytes = seed::serializeWavetable (t);
    return juce::Base64::toBase64 (bytes.data(), bytes.size());
}

static std::shared_ptr<seed::Wavetable> tableFromBase64 (const juce::String& text)
{
    if (text.isEmpty()) return nullptr;
    juce::MemoryOutputStream out;
    if (! juce::Base64::convertFromBase64 (out, text)) return nullptr;
    return seed::deserializeWavetable ((const uint8_t*) out.getData(), out.getDataSize());
}

void SeedProcessor::installWavetable (std::shared_ptr<seed::Wavetable> t, const juce::String& info)
{
    auto old = userTable;
    userTable = std::move (t);
    wavetableInfo = userTable ? info : juce::String();
    engine.setUserWavetable (userTable.get());
    if (old)
    {
        retiredTables.push_back (std::move (old));
        if (retiredTables.size() > 4) retiredTables.erase (retiredTables.begin());
    }
}

juce::String SeedProcessor::loadWavetableFile (const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data) || data.getSize() == 0) return "Could not read that file.";
    auto r = seed::importWavBytes ((const uint8_t*) data.getData(), data.getSize(),
                                   file.getFileNameWithoutExtension().toStdString());
    if (! r.table) return juce::String (r.message);
    installWavetable (r.table, juce::String (r.message));
    return wavetableInfo;
}

void SeedProcessor::clearWavetable() { installWavetable (nullptr, {}); }

juce::File SeedProcessor::getWavetableDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Seedling").getChildFile ("Wavetables");
    dir.createDirectory();
    return dir;
}

std::vector<juce::File> SeedProcessor::listWavetables() const
{
    std::vector<juce::File> out;
    for (const auto& f : getWavetableDirectory().findChildFiles (juce::File::findFiles, true, "*.wav;*.WAV"))
        out.push_back (f);
    std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return out;
}

void SeedProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (userTable)
    {
        state.setProperty ("wavetable", tableToBase64 (*userTable), nullptr);
        state.setProperty ("wavetableInfo", wavetableInfo, nullptr);
    }
    state.setProperty ("seed", (juce::int64) currentSeed.load(), nullptr);
    state.setProperty ("preset", presetName, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void SeedProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentSeed = (uint32_t) (juce::int64) tree.getProperty ("seed", 0);
            presetName = tree.getProperty ("preset", "init").toString();
            if (auto t = tableFromBase64 (tree.getProperty ("wavetable", "").toString()))
                installWavetable (t, tree.getProperty ("wavetableInfo", "").toString());
            else
                installWavetable (nullptr, {});
            tree.removeProperty ("wavetable", nullptr);
            tree.removeProperty ("wavetableInfo", nullptr);
            apvts.replaceState (tree);
        }
}

//==============================================================================
float SeedProcessor::getReal (int index) const       { return raw[index]->load(); }

float SeedProcessor::getNormalised (int index) const
{
    const auto& d = kParams[index];
    return (raw[index]->load() - d.lo) / (d.hi - d.lo);
}

void SeedProcessor::setReal (int index, float v)
{
    if (auto* p = apvts.getParameter (kParams[index].id))
    {
        const auto& d = kParams[index];
        v = juce::jlimit (d.lo, d.hi, v);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void SeedProcessor::setAllReal (const float* values)
{
    for (int i = 0; i < kNumParams; ++i) setReal (i, values[i]);
}

//==============================================================================
void SeedProcessor::randomize (uint32_t seedValue)
{
    float v[kNumParams];
    randomizeValues (seedValue, chaosParam->load(), v);
    for (int i = 0; i < kNumParams; ++i)
        if (kParams[i].flags & fNoRandom) v[i] = getReal (i);        // Randomize changes the sound, not the play mode
    if (autoTuneParam->load() > 0.5f) seed::correctTune (v);

    setAllReal (v);
    currentSeed = seedValue;
    presetName = "seed " + juce::String (seedValue);
    if (const char* an = seed::archetype::nameForSeed (seedValue)) presetName += juce::String (" - ") + an;
}

void SeedProcessor::randomizeNew()
{
    randomize (kFirstArchetypeSeed + (uint32_t) juce::Random::getSystemRandom().nextInt ((int) kSeedRange));
}

void SeedProcessor::correctTune()
{
    float v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = getReal (i);
    seed::correctTune (v);
    setReal (a_freq, v[a_freq]);
    setReal (b_freq, v[b_freq]);
}

//==============================================================================
juce::File SeedProcessor::getPresetDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Seedling").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

std::vector<juce::File> SeedProcessor::listPresets() const
{
    std::vector<juce::File> out;
    for (const auto& f : getPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.seedling"))
        out.push_back (f);
    std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return out;
}

bool SeedProcessor::writePresetFile (const juce::File& file, const float* values, uint32_t seedValue, const juce::String& name,
                                   const seed::Wavetable* table, const juce::String& info)
{
    juce::XmlElement root ("SeedlingPreset");
    root.setAttribute ("version", 1);
    root.setAttribute ("name", name);
    root.setAttribute ("seed", (int) seedValue);
    for (int i = 0; i < kNumParams; ++i)
    {
        auto* e = root.createNewChildElement ("P");
        e->setAttribute ("id", kParams[i].id);
        e->setAttribute ("v", (double) values[i]);
    }
    if (table)
    {
        auto* w = root.createNewChildElement ("Wavetable");
        w->setAttribute ("info", info);
        w->addTextElement (tableToBase64 (*table));
    }
    return root.writeTo (file);
}

bool SeedProcessor::savePreset (const juce::File& file)
{
    float v[kNumParams];
    for (int i = 0; i < kNumParams; ++i) v[i] = getReal (i);
    const auto name = file.getFileNameWithoutExtension();
    if (! writePresetFile (file, v, currentSeed.load(), name, userTable.get(), wavetableInfo)) return false;
    presetName = name;
    return true;
}

bool SeedProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("SeedlingPreset")) return false;

    float v[kNumParams];
    for (int i = 0; i < kNumParams; ++i)       // anything missing falls back to default - except play-mode settings (mono, glide),
        v[i] = (kParams[i].flags & fNoRandom) ? getReal (i) : kParams[i].def;   // which older presets never had: those stay as the player set them

    for (auto* e : xml->getChildWithTagNameIterator ("P"))
    {
        const auto id = e->getStringAttribute ("id");
        for (int i = 0; i < kNumParams; ++i)
            if (id == kParams[i].id)
                v[i] = juce::jlimit (kParams[i].lo, kParams[i].hi, (float) e->getDoubleAttribute ("v", kParams[i].def));
    }

    if (auto* w = xml->getChildByName ("Wavetable"))
    {
        if (auto t = tableFromBase64 (w->getAllSubText().trim()))
            installWavetable (t, w->getStringAttribute ("info"));
        else
            installWavetable (nullptr, {});
    }
    else
        installWavetable (nullptr, {});

    setAllReal (v);
    currentSeed = (uint32_t) xml->getIntAttribute ("seed", 0);
    presetName = file.getFileNameWithoutExtension();
    return true;
}

void SeedProcessor::stepPreset (int direction)
{
    const auto files = listPresets();
    if (files.empty()) return;

    int idx = -1;
    for (int i = 0; i < (int) files.size(); ++i)
        if (files[(size_t) i].getFileNameWithoutExtension() == presetName) { idx = i; break; }

    const int n = (int) files.size();
    idx = idx < 0 ? (direction > 0 ? 0 : n - 1) : (idx + direction + n) % n;
    loadPreset (files[(size_t) idx]);
}

void SeedProcessor::ensureFactoryPresets()
{
    if (! listPresets().empty()) return;

    // A starter bank of soft, light patches grown from fixed seeds (so they are the same everywhere).
    const uint32_t seeds[] = { 29, 101, 214, 377, 512, 640, 733, 858, 909, 1066, 1234, 2024 };
    int n = 1;
    for (uint32_t s : seeds)
    {
        float v[kNumParams];
        randomizeValues (s, 0.15f, v);
        seed::correctTune (v);
        const auto name = "Soft " + juce::String (n++).paddedLeft ('0', 2);
        writePresetFile (getPresetDirectory().getChildFile (name + ".seedling"), v, s, name, nullptr, {});
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SeedProcessor(); }
