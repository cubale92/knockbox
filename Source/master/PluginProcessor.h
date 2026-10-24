#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "dsp/KnockEngine.h"

namespace ids
{
    #define KB_ID(name) inline const juce::String name { #name };
    KB_ID (input)
    KB_ID (knock)
    KB_ID (knockFreq)
    KB_ID (punch)
    KB_ID (snap)
    KB_ID (tape)
    KB_ID (glueThresh)
    KB_ID (glueRatio)
    KB_ID (glueAttack)
    KB_ID (glueRelease)
    KB_ID (glueMakeup)
    KB_ID (glueMix)
    KB_ID (glueScHpf)
    KB_ID (dust)
    KB_ID (vinyl)
    KB_ID (monoBass)
    KB_ID (monoFreq)
    KB_ID (output)
    KB_ID (clipOn)
    KB_ID (ceiling)
    #undef KB_ID
}

struct Preset
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values;   // only the parameters that differ from default
};

class KnockboxProcessor : public juce::AudioProcessor
{
public:
    KnockboxProcessor();
    ~KnockboxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Knockbox"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // meters (read by the editor)
    std::atomic<float> meterGrDb   { 0.0f };
    std::atomic<float> meterPunchDb{ 0.0f };
    std::atomic<float> meterOutL   { 0.0f };
    std::atomic<float> meterOutR   { 0.0f };
    std::atomic<float> meterInL    { 0.0f };
    std::atomic<float> meterInR    { 0.0f };

    static const std::vector<Preset>& presets();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    knock::Params readParams() const;

    knock::KnockEngine engine;
    int currentProgram = 0;

    std::atomic<float>* p[20] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnockboxProcessor)
};
