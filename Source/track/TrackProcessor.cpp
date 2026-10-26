#include "TrackProcessor.h"
#include "TrackEditor.h"

using knock::FilterType;

namespace
{
    juce::NormalisableRange<float> logRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::String dbText (float v, int)  { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }
    juce::String qText (float v, int)   { return juce::String (v, 2); }
    juce::String hzText (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }

    struct BandDefault { bool on; int type; float freq, gain, q; };
    constexpr BandDefault bandDefaults[4] = {
        { true, (int) FilterType::LowCut,    30.0f,   0.0f, 0.71f },
        { true, (int) FilterType::Bell,      200.0f,  0.0f, 1.0f  },
        { true, (int) FilterType::Bell,      2000.0f, 0.0f, 1.0f  },
        { true, (int) FilterType::HighShelf, 8000.0f, 0.0f, 0.71f },
    };
}

const juce::StringArray& KnockboxTrackProcessor::typeNames()
{
    static const juce::StringArray names { "Low Cut", "Low Shelf", "Bell", "High Shelf", "High Cut" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout KnockboxTrackProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                         const String& unit, String (*toText) (float, int))
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (toText)));
    };

    addFloat (tids::input, "Input", { -18.0f, 18.0f, 0.1f }, 0.0f, "dB", dbText);

    for (int b = 0; b < 4; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";
        const auto& d = bandDefaults[b];
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { tids::bandOn (b), 1 }, n + "On", d.on));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { tids::bandType (b), 1 }, n + "Type", typeNames(), d.type));
        addFloat (tids::bandFreq (b), n + "Freq", logRange (20.0f, 20000.0f, 640.0f), d.freq, "Hz", hzText);
        addFloat (tids::bandGain (b), n + "Gain", { -18.0f, 18.0f, 0.1f }, d.gain, "dB", dbText);
        addFloat (tids::bandQ (b),    n + "Q",    logRange (0.1f, 10.0f, 1.0f), d.q, "", qText);
    }

    addFloat (tids::knock,     "Knock",     { 0.0f, 100.0f, 0.1f }, 0.0f, "%", pctText);
    addFloat (tids::knockFreq, "Knock Freq", logRange (40.0f, 250.0f, 100.0f), 90.0f, "Hz", hzText);
    addFloat (tids::punch,     "Punch",     { 0.0f, 100.0f, 0.1f }, 0.0f, "%", pctText);
    addFloat (tids::snap,      "Snap",      { 0.0f, 100.0f, 0.1f }, 0.0f, "%", pctText);
    addFloat (tids::snapFreq,  "Snap Freq", logRange (1000.0f, 6000.0f, 2500.0f), 2500.0f, "Hz", hzText);
    addFloat (tids::tape,      "Tape",      { 0.0f, 100.0f, 0.1f }, 0.0f, "%", pctText);
    addFloat (tids::output,    "Output",    { -18.0f, 18.0f, 0.1f }, 0.0f, "dB", dbText);

    return layout;
}

//==============================================================================
const std::vector<TrackPreset>& KnockboxTrackProcessor::presets()
{
    auto band = [] (int b, int type, float f, float g, float q)
    {
        return std::vector<std::pair<juce::String, float>> {
            { tids::bandType (b), (float) type }, { tids::bandFreq (b), f }, { tids::bandGain (b), g }, { tids::bandQ (b), q } };
    };
    auto join = [] (std::initializer_list<std::vector<std::pair<juce::String, float>>> parts)
    {
        std::vector<std::pair<juce::String, float>> out;
        for (auto& p : parts) out.insert (out.end(), p.begin(), p.end());
        return out;
    };
    constexpr int LC = 0, LS = 1, BL = 2, HS = 3, HC = 4;

    static const std::vector<TrackPreset> list {
        { "Clean", {} },
        { "Kick Knock", join ({ band (0, LC, 30, 0, 0.71f), band (1, BL, 60, 3, 1.2f), band (2, BL, 350, -4, 1.4f),
                                band (3, HS, 5000, 2, 0.71f),
                                { { tids::knock, 60 }, { tids::knockFreq, 70 }, { tids::punch, 60 }, { tids::tape, 30 }, { tids::output, -4.5f } } }) },
        { "Snare Crack", join ({ band (0, LC, 100, 0, 0.71f), band (1, BL, 200, 3, 1.5f), band (2, BL, 800, -3, 1.5f),
                                 band (3, HS, 6000, 3, 0.71f),
                                 { { tids::knock, 30 }, { tids::knockFreq, 190 }, { tids::punch, 50 }, { tids::snap, 60 }, { tids::snapFreq, 3000 }, { tids::tape, 35 } } }) },
        { "Dusty Sample", join ({ band (0, LC, 120, 0, 0.71f), band (1, BL, 400, -2, 1.0f), band (2, BL, 3000, 1.5f, 0.8f),
                                  band (3, HC, 8000, 0, 0.71f), { { tids::tape, 50 } } }) },
        { "Bass Weight", join ({ band (0, LC, 30, 0, 0.71f), band (1, LS, 90, 3, 0.71f), band (2, BL, 250, -3, 1.2f),
                                 band (3, HC, 4000, 0, 0.71f), { { tids::tape, 40 } } }) },
        { "Hats Tame", join ({ band (0, LC, 300, 0, 0.71f), band (1, BL, 600, -2, 1.0f), band (2, BL, 4500, -2, 2.0f),
                               band (3, HS, 10000, -3, 0.71f), { { tids::tape, 20 } } }) },
        { "Kick Sub Boom", join ({ band (0, LC, 25, 0, 0.71f), band (1, LS, 55, 4, 0.71f), band (2, BL, 300, -5, 1.5f),
                                   band (3, HC, 9000, 0, 0.71f),
                                   { { tids::knock, 50 }, { tids::knockFreq, 55 }, { tids::punch, 40 }, { tids::tape, 35 }, { tids::output, -4.0f } } }) },
        { "Snare Fat Body", join ({ band (0, LC, 80, 0, 0.71f), band (1, BL, 180, 4, 1.2f), band (2, BL, 500, -3, 1.2f),
                                    band (3, HS, 7000, 2, 0.71f),
                                    { { tids::knock, 40 }, { tids::knockFreq, 180 }, { tids::punch, 40 }, { tids::snap, 35 },
                                      { tids::snapFreq, 2500 }, { tids::tape, 40 } } }) },
        { "Rimshot Click", join ({ band (0, LC, 200, 0, 0.71f), band (1, BL, 800, -3, 1.2f), band (2, BL, 4000, 3, 1.5f),
                                   band (3, HS, 9000, 1, 0.71f),
                                   { { tids::punch, 55 }, { tids::snap, 50 }, { tids::snapFreq, 4000 }, { tids::tape, 20 } } }) },
        { "Breakbeat Loop", join ({ band (0, LC, 40, 0, 0.71f), band (1, LS, 90, 2, 0.71f), band (2, BL, 400, -2.5f, 1.0f),
                                    band (3, HS, 6000, 2, 0.71f),
                                    { { tids::knock, 45 }, { tids::knockFreq, 85 }, { tids::punch, 50 }, { tids::snap, 30 },
                                      { tids::tape, 45 }, { tids::output, -2.0f } } }) },
        { "Open Hat Air", join ({ band (0, LC, 400, 0, 0.71f), band (1, BL, 1000, -2, 1.0f), band (2, BL, 8000, 2, 1.0f),
                                  band (3, HS, 12000, 2, 0.71f), { { tids::tape, 10 } } }) },
        { "Filtered Bassline", join ({ band (0, LC, 35, 0, 0.71f), band (1, LS, 80, 2, 0.71f), band (2, BL, 300, -2, 1.0f),
                                       band (3, HC, 900, 0, 0.71f), { { tids::tape, 45 } } }) },
        { "Horn Stabs", join ({ band (0, LC, 150, 0, 0.71f), band (1, BL, 350, -3, 1.0f), band (2, BL, 2200, 3, 1.0f),
                                band (3, HC, 9000, 0, 0.71f), { { tids::punch, 30 }, { tids::tape, 45 } } }) },
        { "Jazz Keys Loop", join ({ band (0, LC, 90, 0, 0.71f), band (1, BL, 250, -2, 1.0f), band (2, BL, 1800, 1.5f, 0.8f),
                                    band (3, HC, 7000, 0, 0.71f), { { tids::tape, 55 } } }) },
        { "Vocal Chops", join ({ band (0, LC, 180, 0, 0.71f), band (1, BL, 300, -3, 1.0f), band (2, BL, 3500, 2, 1.0f),
                                 band (3, HS, 10000, 1.5f, 0.71f), { { tids::tape, 30 } } }) },
        { "Telephone Vocal", join ({ band (0, LC, 450, 0, 1.0f), band (1, BL, 1200, 3, 0.8f), band (2, BL, 3000, 2, 1.0f),
                                     band (3, HC, 3500, 0, 1.0f), { { tids::tape, 40 } } }) },
    };
    return list;
}

//==============================================================================
KnockboxTrackProcessor::KnockboxTrackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KnockboxTrackState", createLayout())
{
    for (int b = 0; b < 4; ++b)
        bp[b] = { apvts.getRawParameterValue (tids::bandOn (b)),   apvts.getRawParameterValue (tids::bandType (b)),
                  apvts.getRawParameterValue (tids::bandFreq (b)), apvts.getRawParameterValue (tids::bandGain (b)),
                  apvts.getRawParameterValue (tids::bandQ (b)) };
    pInput     = apvts.getRawParameterValue (tids::input);
    pKnock     = apvts.getRawParameterValue (tids::knock);
    pKnockFreq = apvts.getRawParameterValue (tids::knockFreq);
    pPunch     = apvts.getRawParameterValue (tids::punch);
    pSnap      = apvts.getRawParameterValue (tids::snap);
    pSnapFreq  = apvts.getRawParameterValue (tids::snapFreq);
    pTape      = apvts.getRawParameterValue (tids::tape);
    pOutput    = apvts.getRawParameterValue (tids::output);
}

bool KnockboxTrackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void KnockboxTrackProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateForDisplay.store (sampleRate);
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    engine.setParams (readParams());
}

knock::TrackParams KnockboxTrackProcessor::readParams() const
{
    knock::TrackParams t;
    t.inputDb = pInput->load();
    for (int b = 0; b < 4; ++b)
    {
        t.band[b].on     = bp[b].on->load() > 0.5f;
        t.band[b].type   = juce::jlimit (0, 4, (int) std::lround (bp[b].type->load()));
        t.band[b].freq   = bp[b].freq->load();
        t.band[b].gainDb = bp[b].gain->load();
        t.band[b].q      = bp[b].q->load();
    }
    t.knock     = pKnock->load() * 0.01f;
    t.knockFreq = pKnockFreq->load();
    t.punch     = pPunch->load() * 0.01f;
    t.snap      = pSnap->load() * 0.01f;
    t.snapFreq  = pSnapFreq->load();
    t.tape      = pTape->load() * 0.01f;
    t.outputDb  = pOutput->load();
    return t;
}

void KnockboxTrackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    if (numCh == 0 || n == 0) return;

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), numCh, n);

    float pk = 0.0f;
    for (int c = 0; c < numCh; ++c) pk = juce::jmax (pk, buffer.getMagnitude (c, 0, n));
    float cur = meterOut.load();
    while (pk > cur && ! meterOut.compare_exchange_weak (cur, pk)) {}
    meterPunchDb.store (engine.getPunchDb());
}

//==============================================================================
int KnockboxTrackProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String KnockboxTrackProcessor::getProgramName (int i)
{
    return juce::isPositiveAndBelow (i, (int) presets().size()) ? juce::String (presets()[(size_t) i].name) : juce::String();
}

void KnockboxTrackProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size())) return;
    currentProgram = index;
    const auto& preset = presets()[(size_t) index];

    for (auto* param : getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (r == nullptr) continue;
        float value = r->convertFrom0to1 (r->getDefaultValue());
        for (auto& [id, v] : preset.values)
            if (id == r->getParameterID()) value = v;
        r->beginChangeGesture();
        r->setValueNotifyingHost (r->convertTo0to1 (value));
        r->endChangeGesture();
    }
}

void KnockboxTrackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void KnockboxTrackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* KnockboxTrackProcessor::createEditor() { return new KnockboxTrackEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KnockboxTrackProcessor(); }
