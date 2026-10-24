#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "dsp/TrackEngine.h"

namespace tids
{
    inline juce::String bandOn   (int b) { return "b" + juce::String (b + 1) + "On"; }
    inline juce::String bandType (int b) { return "b" + juce::String (b + 1) + "Type"; }
    inline juce::String bandFreq (int b) { return "b" + juce::String (b + 1) + "Freq"; }
    inline juce::String bandGain (int b) { return "b" + juce::String (b + 1) + "Gain"; }
    inline juce::String bandQ    (int b) { return "b" + juce::String (b + 1) + "Q"; }

    inline const juce::String input     { "input" };
    inline const juce::String knock     { "knock" };
    inline const juce::String knockFreq { "knockFreq" };
    inline const juce::String punch     { "punch" };
    inline const juce::String snap      { "snap" };
    inline const juce::String snapFreq  { "snapFreq" };
    inline const juce::String tape      { "tape" };
    inline const juce::String output    { "output" };
}

struct TrackPreset
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values;   // differences from default
};

class KnockboxTrackProcessor : public juce::AudioProcessor
{
public:
    KnockboxTrackProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Knockbox Track"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // current settings as engine parameters (also used by the EQ display)
    knock::TrackParams readParams() const;
    double getCurrentSampleRate() const { return sampleRateForDisplay.load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> meterPunchDb { 0.0f };
    std::atomic<float> meterOut     { 0.0f };

    static const std::vector<TrackPreset>& presets();
    static const juce::StringArray& typeNames();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    knock::TrackEngine engine;
    int currentProgram = 0;
    std::atomic<double> sampleRateForDisplay { 48000.0 };

    struct BandPtrs { std::atomic<float>* on; std::atomic<float>* type; std::atomic<float>* freq; std::atomic<float>* gain; std::atomic<float>* q; };
    BandPtrs bp[4] {};
    std::atomic<float>* pInput = nullptr, *pKnock = nullptr, *pKnockFreq = nullptr, *pPunch = nullptr,
                       *pSnap = nullptr, *pSnapFreq = nullptr, *pTape = nullptr, *pOutput = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnockboxTrackProcessor)
};
