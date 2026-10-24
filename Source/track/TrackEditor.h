#pragma once

#include "common/KnockUI.h"
#include "TrackProcessor.h"

namespace bandColours
{
    const juce::Colour c[4] = { juce::Colour (0xffe8a33d), juce::Colour (0xffd9533b),
                                juce::Colour (0xff9fbf6a), juce::Colour (0xff6fa8d0) };
}

// Q4-style curve display with draggable band nodes
class EqGraph : public juce::Component
{
public:
    explicit EqGraph (KnockboxTrackProcessor&);
    void paint (juce::Graphics&) override;
    void refresh();                                   // called by the editor's timer

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plot() const;
    float xForFreq (double f) const;
    double freqForX (float x) const;
    float yForDb (double db) const;
    double dbForY (float y) const;
    juce::Point<float> nodePos (int band) const;
    int hitBand (juce::Point<float>) const;
    void setParam (const juce::String& id, float value);

    KnockboxTrackProcessor& proc;
    knock::TrackParams shown;
    double fs = 48000.0;
    int hover = -1, dragging = -1;
    static constexpr double dbRange = 20.0;
};

class BandStrip : public juce::Component
{
public:
    BandStrip (KnockboxTrackProcessor&, int band);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    KnockboxTrackProcessor& proc;
    int band;
    juce::TextButton onButton { "ON" };
    juce::ComboBox typeBox;
    Knob freq, gain, q;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttach;
};

class KnockboxTrackEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KnockboxTrackEditor (KnockboxTrackProcessor&);
    ~KnockboxTrackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String&, const juce::String&);

    KnockboxTrackProcessor& proc;
    KnockLookAndFeel lnf;

    EqGraph graph;
    std::unique_ptr<BandStrip> strips[4];
    Knob knock, knockFreq, punch, snap, snapFreq, input, tape, output;
    juce::ComboBox presetBox;

    juce::Rectangle<int> secEq, secKnock, secOut, outMeter;
    float punchShown = 0.0f, outShown = -60.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnockboxTrackEditor)
};
