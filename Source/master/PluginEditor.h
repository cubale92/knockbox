#pragma once

#include "common/KnockUI.h"
#include "PluginProcessor.h"

class KnockboxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KnockboxEditor (KnockboxProcessor&);
    ~KnockboxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawSection (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title, const juce::String& sub);
    void drawGrMeter (juce::Graphics&, juce::Rectangle<int> area);
    void drawOutMeter (juce::Graphics&, juce::Rectangle<int> area);

    KnockboxProcessor& proc;
    KnockLookAndFeel lnf;

    Knob knock, knockFreq, punch, snap;
    Knob input, tape;
    Knob thresh, ratio, attack, release, makeup, mix, scHpf;
    Knob dust, vinyl;
    Knob output, ceiling, monoFreq;

    juce::TextButton clipButton { "CLIP" }, monoButton { "MONO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clipAttach, monoAttach;

    juce::ComboBox presetBox;

    juce::Rectangle<int> secKnock, secTape, secGlue, secDust, secOut, secMeter, grArea;

    float grShown = 0.0f, outL = 0.0f, outR = 0.0f, punchShown = 0.0f;
    float holdL = 0.0f, holdR = 0.0f;
    int holdCountL = 0, holdCountR = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnockboxEditor)
};
