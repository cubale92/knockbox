#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Order must match the index constants below
    const juce::String* const paramOrder[] = {
        &ids::input, &ids::knock, &ids::knockFreq, &ids::punch, &ids::snap, &ids::tape,
        &ids::glueThresh, &ids::glueRatio, &ids::glueAttack, &ids::glueRelease, &ids::glueMakeup,
        &ids::glueMix, &ids::glueScHpf, &ids::dust, &ids::vinyl, &ids::monoBass, &ids::monoFreq,
        &ids::output, &ids::clipOn, &ids::ceiling
    };

    enum Idx { iInput, iKnock, iKnockFreq, iPunch, iSnap, iTape, iThresh, iRatio, iAttack, iRelease,
               iMakeup, iMix, iScHpf, iDust, iVinyl, iMonoBass, iMonoFreq, iOutput, iClipOn, iCeiling, iCount };

    static_assert (iCount == 20, "parameter count mismatch");

    juce::NormalisableRange<float> logRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::String dbText (float v, int)   { return juce::String (v, 1) + " dB"; }
    juce::String pctText (float v, int)  { return juce::String (juce::roundToInt (v)) + "%"; }
    juce::String hzText (float v, int)   { return juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String msText (float v, int)   { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String ratioText (float v, int){ return juce::String (v, 1) + ":1"; }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout KnockboxProcessor::createLayout()
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

    addFloat (ids::input,       "Input",         { -18.0f, 18.0f, 0.1f },          0.0f,   "dB", dbText);
    addFloat (ids::knock,       "Knock",         { 0.0f, 100.0f, 0.1f },           50.0f,  "%",  pctText);
    addFloat (ids::knockFreq,   "Knock Freq",    logRange (45.0f, 160.0f, 90.0f),  95.0f,  "Hz", hzText);
    addFloat (ids::punch,       "Punch",         { 0.0f, 100.0f, 0.1f },           35.0f,  "%",  pctText);
    addFloat (ids::snap,        "Snap",          { 0.0f, 100.0f, 0.1f },           20.0f,  "%",  pctText);
    addFloat (ids::tape,        "Tape",          { 0.0f, 100.0f, 0.1f },           35.0f,  "%",  pctText);
    addFloat (ids::glueThresh,  "Glue Threshold",{ -36.0f, 0.0f, 0.1f },           -12.0f, "dB", dbText);
    addFloat (ids::glueRatio,   "Glue Ratio",    logRange (1.5f, 10.0f, 4.0f),     4.0f,   "",   ratioText);
    addFloat (ids::glueAttack,  "Glue Attack",   logRange (0.1f, 80.0f, 10.0f),    10.0f,  "ms", msText);
    addFloat (ids::glueRelease, "Glue Release",  logRange (30.0f, 1200.0f, 200.0f),150.0f, "ms", msText);
    addFloat (ids::glueMakeup,  "Glue Makeup",   { 0.0f, 18.0f, 0.1f },            2.0f,   "dB", dbText);
    addFloat (ids::glueMix,     "Glue Mix",      { 0.0f, 100.0f, 0.1f },           50.0f,  "%",  pctText);
    addFloat (ids::glueScHpf,   "Glue SC HPF",   logRange (20.0f, 300.0f, 80.0f),  60.0f,  "Hz", hzText);
    addFloat (ids::dust,        "Dust",          { 0.0f, 100.0f, 0.1f },           15.0f,  "%",  pctText);
    addFloat (ids::vinyl,       "Vinyl",         { 0.0f, 100.0f, 0.1f },           0.0f,   "%",  pctText);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::monoBass, 1 }, "Mono Bass", false));
    addFloat (ids::monoFreq,    "Mono Freq",     logRange (40.0f, 250.0f, 110.0f), 120.0f, "Hz", hzText);
    addFloat (ids::output,      "Output",        { -18.0f, 18.0f, 0.1f },          0.0f,   "dB", dbText);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::clipOn, 1 }, "Clip", true));
    addFloat (ids::ceiling,     "Ceiling",       { -12.0f, 0.0f, 0.1f },           -0.3f,  "dB", dbText);

    return layout;
}

//==============================================================================
const std::vector<Preset>& KnockboxProcessor::presets()
{
    static const std::vector<Preset> list {
        { "Default Knock", {} },
        { "Dusty Basement", { { ids::knock, 60 }, { ids::punch, 40 }, { ids::tape, 50 },
                              { ids::dust, 45 }, { ids::vinyl, 25 }, { ids::glueMix, 45 } } },
        { "Hard Snare Crack", { { ids::knock, 40 }, { ids::punch, 55 }, { ids::snap, 60 },
                                { ids::glueAttack, 20 }, { ids::glueRelease, 120 } } },
        { "Thick Low End", { { ids::knock, 85 }, { ids::knockFreq, 75 }, { ids::tape, 45 },
                             { ids::monoBass, 1 }, { ids::monoFreq, 120 }, { ids::glueScHpf, 100 } } },
        { "Parallel Squash", { { ids::glueThresh, -24 }, { ids::glueRatio, 8 }, { ids::glueAttack, 3 },
                               { ids::glueRelease, 120 }, { ids::glueMix, 45 }, { ids::glueMakeup, 6 } } },
        { "Loud & Clipped", { { ids::tape, 50 }, { ids::output, 5 }, { ids::ceiling, -0.3f },
                              { ids::glueMix, 50 }, { ids::punch, 45 } } },
        { "Gentle Polish", { { ids::knock, 25 }, { ids::punch, 20 }, { ids::snap, 10 }, { ids::tape, 20 },
                             { ids::glueMix, 30 }, { ids::dust, 5 } } },
    };
    return list;
}

//==============================================================================
KnockboxProcessor::KnockboxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KnockboxState", createLayout())
{
    for (int i = 0; i < iCount; ++i)
    {
        p[i] = apvts.getRawParameterValue (*paramOrder[i]);
        jassert (p[i] != nullptr);
    }
}

bool KnockboxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void KnockboxProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    engine.setParams (readParams());
}

knock::Params KnockboxProcessor::readParams() const
{
    knock::Params k;
    k.inputDb     = p[iInput]->load();
    k.knock       = p[iKnock]->load() * 0.01f;
    k.knockFreq   = p[iKnockFreq]->load();
    k.punch       = p[iPunch]->load() * 0.01f;
    k.snap        = p[iSnap]->load() * 0.01f;
    k.tape        = p[iTape]->load() * 0.01f;
    k.glueThresh  = p[iThresh]->load();
    k.glueRatio   = p[iRatio]->load();
    k.glueAttack  = p[iAttack]->load();
    k.glueRelease = p[iRelease]->load();
    k.glueMakeup  = p[iMakeup]->load();
    k.glueMix     = p[iMix]->load() * 0.01f;
    k.glueScHpf   = p[iScHpf]->load();
    k.dust        = p[iDust]->load() * 0.01f;
    k.vinyl       = p[iVinyl]->load() * 0.01f;
    k.monoBass    = p[iMonoBass]->load() > 0.5f;
    k.monoFreq    = p[iMonoFreq]->load();
    k.outputDb    = p[iOutput]->load();
    k.clipOn      = p[iClipOn]->load() > 0.5f;
    k.ceilingDb   = p[iCeiling]->load();
    return k;
}

static float peakOf (const float* d, int n)
{
    float m = 0.0f;
    for (int i = 0; i < n; ++i) m = juce::jmax (m, std::abs (d[i]));
    return m;
}

static void holdPeak (std::atomic<float>& meter, float v)
{
    // simple max-hold; the editor decays it when it reads
    float cur = meter.load();
    while (v > cur && ! meter.compare_exchange_weak (cur, v)) {}
}

void KnockboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n     = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    if (numCh == 0 || n == 0) return;

    holdPeak (meterInL, peakOf (buffer.getReadPointer (0), n));
    holdPeak (meterInR, peakOf (buffer.getReadPointer (numCh > 1 ? 1 : 0), n));

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), numCh, n);

    holdPeak (meterOutL, peakOf (buffer.getReadPointer (0), n));
    holdPeak (meterOutR, peakOf (buffer.getReadPointer (numCh > 1 ? 1 : 0), n));
    meterGrDb.store (engine.getGainReductionDb());
    meterPunchDb.store (engine.getPunchDb());
}

//==============================================================================
int KnockboxProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String KnockboxProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) presets().size()))
        return presets()[(size_t) index].name;
    return {};
}

void KnockboxProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size()))
        return;

    currentProgram = index;
    const auto& preset = presets()[(size_t) index];

    for (auto* param : getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr) continue;

        float value = ranged->convertFrom0to1 (ranged->getDefaultValue());
        for (auto& [id, v] : preset.values)
            if (id == ranged->getParameterID())
                value = v;

        ranged->beginChangeGesture();
        ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
        ranged->endChangeGesture();
    }
}

void KnockboxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KnockboxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* KnockboxProcessor::createEditor() { return new KnockboxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KnockboxProcessor(); }
