#include "KnockUI.h"

juce::Font knockFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

namespace { inline juce::Font font (float h, bool b = false) { return knockFont (h, b); } }

//==============================================================================
// Look and feel
//==============================================================================
KnockLookAndFeel::KnockLookAndFeel()
{
    using namespace palette;
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, panelHi);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ComboBox::backgroundColourId, panelHi);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::highlightedTextColourId, bg);
    setColour (juce::TextButton::textColourOffId, muted);
    setColour (juce::TextButton::textColourOnId, bg);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
}

void KnockLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    using namespace palette;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto  centre = bounds.getCentre();
    const float radius = size * 0.5f;
    const float arcW   = juce::jmax (3.0f, radius * 0.11f);
    const float angle  = startAngle + pos * (endAngle - startAngle);
    const bool  over   = s.isMouseOverOrDragging();

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - arcW * 0.5f, radius - arcW * 0.5f, 0.0f, startAngle, endAngle, true);
    g.setColour (line);
    g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    if (pos > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (centre.x, centre.y, radius - arcW * 0.5f, radius - arcW * 0.5f, 0.0f, startAngle, angle, true);
        const auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);
        g.setColour (over ? fill.brighter (0.15f) : fill);
        g.strokePath (val, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    const float bodyR = radius - arcW - 4.0f;
    juce::ColourGradient grad (panelHi.brighter (0.18f), centre.x, centre.y - bodyR,
                               bg, centre.x, centre.y + bodyR, false);
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2, bodyR * 2);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2, bodyR * 2, 1.0f);

    // pointer
    const float pInner = bodyR * 0.35f, pOuter = bodyR * 0.88f;
    const juce::Point<float> a (centre.x + pInner * std::sin (angle), centre.y - pInner * std::cos (angle));
    const juce::Point<float> b (centre.x + pOuter * std::sin (angle), centre.y - pOuter * std::cos (angle));
    g.setColour (text);
    g.drawLine ({ a, b }, juce::jmax (2.0f, radius * 0.06f));
}

void KnockLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    using namespace palette;
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? b.findColour (juce::TextButton::buttonOnColourId) : panelHi);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? juce::Colours::transparentBlack : (over ? muted : line));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void KnockLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? palette::bg : palette::muted);
    g.setFont (font (12.0f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

juce::Label* KnockLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (12.5f));
    l->setColour (juce::Label::textColourId, palette::text);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void KnockLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace palette;
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (panelHi);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.isMouseOver (true) ? muted : line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 5, ay - 2.5f, ax + 5, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (accent);
    g.fillPath (arrow);
}

juce::Font KnockLookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (14.0f, true); }

void KnockLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 0, box.getWidth() - 34, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
// Knob
//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t, bool isBig)
    : title (t), big (isBig)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.setDoubleClickReturnValue (true, 0.0);   // replaced by the attachment's default below
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* param = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    slider.setPopupDisplayEnabled (false, false, nullptr);
}

void Knob::resized()
{
    // Label on top, knob as a square right under it, value text right under the knob.
    // Small knobs are drawn at ~78% of the big size and centred in their cell.
    auto r = getLocalBounds();
    r.removeFromTop (20);
    const int textH = 20;
    int side = juce::jmin (r.getWidth(), r.getHeight() - textH);
    if (! big) side = (int) (side * 0.78f);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (90, r.getWidth()), textH);
    slider.setBounds (r.withSizeKeepingCentre (juce::jmax (side, juce::jmin (90, r.getWidth())), side + textH)
                       .withY (r.getY() + (big ? 0 : (r.getHeight() - textH - side) / 3)));
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (palette::muted);
    g.setFont (font (big ? 13.5f : 11.5f, true));
    g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (20),
                juce::Justification::centred);
}

