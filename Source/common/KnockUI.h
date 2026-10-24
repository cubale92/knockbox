#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Shared look for the Knockbox plugins

namespace palette
{
    const juce::Colour bg       { 0xff16130f };
    const juce::Colour panel    { 0xff211c16 };
    const juce::Colour panelHi  { 0xff2a241c };
    const juce::Colour line     { 0xff3b3228 };
    const juce::Colour text     { 0xffefe3c8 };
    const juce::Colour muted    { 0xffa3937a };
    const juce::Colour accent   { 0xffe8a33d };
    const juce::Colour hot      { 0xffd9533b };
}

class KnockLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnockLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

// A labelled knob bound to one parameter
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title, bool big = false);
    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::String title;
    bool big;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

juce::Font knockFont (float height, bool bold = false);
