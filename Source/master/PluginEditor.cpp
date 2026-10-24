#include "PluginEditor.h"

namespace
{
    inline juce::Font font (float h, bool b = false) { return knockFont (h, b); }
    constexpr int editorW = 1064, editorH = 448;
}

//==============================================================================
// Editor
//==============================================================================
KnockboxEditor::KnockboxEditor (KnockboxProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      knock    (p.apvts, ids::knock,       "Knock", true),
      knockFreq(p.apvts, ids::knockFreq,   "Freq"),
      punch    (p.apvts, ids::punch,       "Punch", true),
      snap     (p.apvts, ids::snap,        "Snap"),
      input    (p.apvts, ids::input,       "Input"),
      tape     (p.apvts, ids::tape,        "Tape", true),
      thresh   (p.apvts, ids::glueThresh,  "Thresh"),
      ratio    (p.apvts, ids::glueRatio,   "Ratio"),
      attack   (p.apvts, ids::glueAttack,  "Attack"),
      release  (p.apvts, ids::glueRelease, "Release"),
      makeup   (p.apvts, ids::glueMakeup,  "Makeup"),
      mix      (p.apvts, ids::glueMix,     "Mix"),
      scHpf    (p.apvts, ids::glueScHpf,   "SC HPF"),
      dust     (p.apvts, ids::dust,        "Dust", true),
      vinyl    (p.apvts, ids::vinyl,       "Vinyl"),
      output   (p.apvts, ids::output,      "Output", true),
      ceiling  (p.apvts, ids::ceiling,     "Ceiling"),
      monoFreq (p.apvts, ids::monoFreq,    "Mono Hz")
{
    setLookAndFeel (&lnf);

    for (auto* k : { &knock, &knockFreq, &punch, &snap, &input, &tape, &thresh, &ratio, &attack,
                     &release, &makeup, &mix, &scHpf, &dust, &vinyl, &output, &ceiling, &monoFreq })
        addAndMakeVisible (*k);

    for (auto* b : { &clipButton, &monoButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    clipButton.setColour (juce::TextButton::buttonOnColourId, palette::hot);
    monoButton.setName ("MONO");
    clipAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::clipOn, clipButton);
    monoAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::monoBass, monoButton);

    const auto& list = KnockboxProcessor::presets();
    for (int i = 0; i < (int) list.size(); ++i)
        presetBox.addItem (list[(size_t) i].name, i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0)
            proc.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);

    setSize (editorW, editorH);
    startTimerHz (30);
}

KnockboxEditor::~KnockboxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KnockboxEditor::timerCallback()
{
    const float gr = proc.meterGrDb.load();
    grShown = gr < grShown ? gr : grShown + (gr - grShown) * 0.25f;

    const float pk = proc.meterPunchDb.load();
    punchShown = pk > punchShown ? pk : punchShown * 0.85f;

    auto fall = [] (float& shown, float v)
    {
        const float db = juce::Decibels::gainToDecibels (v, -60.0f);
        shown = db > shown ? db : juce::jmax (-60.0f, shown - 1.2f);
    };
    fall (outL, proc.meterOutL.exchange (0.0f));
    fall (outR, proc.meterOutR.exchange (0.0f));
    proc.meterInL.exchange (0.0f);
    proc.meterInR.exchange (0.0f);

    auto hold = [] (float& h, int& count, float v)
    {
        if (v >= h) { h = v; count = 45; }
        else if (--count <= 0) h = juce::jmax (-60.0f, h - 0.8f);
    };
    hold (holdL, holdCountL, outL);
    hold (holdR, holdCountR, outR);

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);

    repaint (grArea);
    repaint (secMeter);
    repaint (secKnock.withHeight (34));
}

void KnockboxEditor::drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& t, const juce::String& sub)
{
    using namespace palette;
    g.setColour (panel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (line);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);

    auto head = area.withHeight (34).reduced (14, 0);
    g.setColour (accent);
    g.setFont (font (15.0f, true));
    g.drawText (t, head, juce::Justification::centredLeft);
    g.setColour (muted.withAlpha (0.8f));
    g.setFont (font (11.0f));
    g.drawText (sub, head, juce::Justification::centredRight);
    g.setColour (line);
    g.drawHorizontalLine (area.getY() + 34, (float) area.getX() + 10, (float) area.getRight() - 10);
}

void KnockboxEditor::drawGrMeter (juce::Graphics& g, juce::Rectangle<int> area)
{
    using namespace palette;
    auto r = area.reduced (6, 0);
    g.setColour (muted);
    g.setFont (font (11.5f, true));
    g.drawText ("GR", r.removeFromTop (20), juce::Justification::centred);

    g.setColour (text);
    g.setFont (font (16.0f, true));
    g.drawText (juce::String (grShown, 1), r.removeFromTop (26), juce::Justification::centred);

    auto bar = r.removeFromTop (10).toFloat();
    g.setColour (bg);
    g.fillRoundedRectangle (bar, 3.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, -grShown / 20.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (bar.withLeft (bar.getRight() - bar.getWidth() * frac), 3.0f);

    g.setColour (muted.withAlpha (0.7f));
    g.setFont (font (10.0f));
    g.drawText ("dB", r.removeFromTop (16), juce::Justification::centred);
}

void KnockboxEditor::drawOutMeter (juce::Graphics& g, juce::Rectangle<int> area)
{
    using namespace palette;
    g.setColour (panel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (line);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);

    auto r = area.reduced (8, 10);
    g.setColour (muted);
    g.setFont (font (11.0f, true));
    g.drawText ("OUT", r.removeFromTop (16), juce::Justification::centred);
    auto labels = r.removeFromBottom (16);
    g.drawText ("L  R", labels, juce::Justification::centred);

    const float ceilDb = proc.apvts.getRawParameterValue (ids::ceiling)->load();
    auto toY = [&] (float db, juce::Rectangle<float> b)
    {
        return b.getBottom() - b.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    };

    auto bars = r.toFloat();
    const float bw = (bars.getWidth() - 4.0f) * 0.5f;
    int i = 0;
    for (auto [lvl, hld] : { std::pair { outL, holdL }, std::pair { outR, holdR } })
    {
        auto b = juce::Rectangle<float> (bars.getX() + (float) i * (bw + 4.0f), bars.getY(), bw, bars.getHeight());
        g.setColour (bg);
        g.fillRoundedRectangle (b, 2.0f);
        const float y = toY (lvl, b);
        juce::ColourGradient grad (accent, 0, b.getBottom(), hot, 0, toY (0.0f, b), false);
        grad.addColour (0.75, accent.brighter (0.2f));
        g.setGradientFill (grad);
        g.fillRect (b.withTop (y));
        g.setColour (hld >= ceilDb - 0.05f ? hot : text);
        g.fillRect (b.getX(), toY (hld, b) - 1.0f, b.getWidth(), 2.0f);
        ++i;
    }

    // ceiling marker
    g.setColour (hot.withAlpha (0.8f));
    const float cy = toY (ceilDb, bars);
    g.drawHorizontalLine ((int) cy, bars.getX() - 2, bars.getRight() + 2);
}

void KnockboxEditor::paint (juce::Graphics& g)
{
    using namespace palette;
    g.fillAll (bg);

    // header
    auto head = getLocalBounds().removeFromTop (60).reduced (20, 0);
    g.setColour (text);
    g.setFont (font (30.0f, true));
    g.drawText ("KNOCKBOX", head.removeFromLeft (190), juce::Justification::centredLeft);
    g.setColour (accent);
    g.setFont (font (12.0f, true));
    g.drawText ("90s NY BOOM BAP MASTER", head.removeFromLeft (220), juce::Justification::centredLeft);
    g.setColour (muted);
    g.setFont (font (12.0f));
    g.drawText ("8cee", head.removeFromRight (40), juce::Justification::centredRight);

    drawSection (g, secKnock, "KNOCK", "kick + snare");
    drawSection (g, secTape,  "TAPE",  "warmth");
    drawSection (g, secGlue,  "GLUE",  "parallel bus comp");
    drawSection (g, secDust,  "DUST",  "age");
    drawSection (g, secOut,   "OUT",   "loud");

    // punch activity dot in the knock header
    const float pa = juce::jlimit (0.0f, 1.0f, punchShown / 6.0f);
    g.setColour (accent.withAlpha (0.15f + 0.85f * pa));
    g.fillEllipse ((float) secKnock.getX() + 74.0f, (float) secKnock.getY() + 13.0f, 8.0f, 8.0f);

    drawGrMeter (g, grArea);
    drawOutMeter (g, secMeter);
}

void KnockboxEditor::resized()
{
    auto r = getLocalBounds().reduced (14, 0);
    auto header = r.removeFromTop (60);
    presetBox.setBounds (header.removeFromRight (290).withSizeKeepingCentre (230, 32).translated (-30, 0));
    r.removeFromBottom (14);

    const int gap = 8;
    secKnock = r.removeFromLeft (220); r.removeFromLeft (gap);
    secTape  = r.removeFromLeft (120); r.removeFromLeft (gap);
    secGlue  = r.removeFromLeft (340); r.removeFromLeft (gap);
    secDust  = r.removeFromLeft (120); r.removeFromLeft (gap);
    secMeter = r.removeFromRight (56); r.removeFromRight (gap);
    secOut   = r;

    auto body = [] (juce::Rectangle<int> s) { return s.withTrimmedTop (40).reduced (6, 4); };

    {   // KNOCK: 2 x 2
        auto b = body (secKnock);
        auto top = b.removeFromTop (b.getHeight() / 2);
        knock.setBounds (top.removeFromLeft (top.getWidth() / 2));
        knockFreq.setBounds (top);
        punch.setBounds (b.removeFromLeft (b.getWidth() / 2));
        snap.setBounds (b);
    }
    {   // TAPE: tape big, input small
        auto b = body (secTape);
        tape.setBounds (b.removeFromTop (b.getHeight() / 2));
        input.setBounds (b);
    }
    {   // GLUE: 4 x 2, last cell holds the GR meter
        auto b = body (secGlue);
        auto row1 = b.removeFromTop (b.getHeight() / 2);
        const int cw = row1.getWidth() / 4;
        thresh.setBounds  (row1.removeFromLeft (cw));
        ratio.setBounds   (row1.removeFromLeft (cw));
        attack.setBounds  (row1.removeFromLeft (cw));
        release.setBounds (row1);
        mix.setBounds     (b.removeFromLeft (cw));
        makeup.setBounds  (b.removeFromLeft (cw));
        scHpf.setBounds   (b.removeFromLeft (cw));
        grArea = b.withTrimmedTop (b.getHeight() / 4).withHeight (76);
    }
    {   // DUST
        auto b = body (secDust);
        dust.setBounds (b.removeFromTop (b.getHeight() / 2));
        vinyl.setBounds (b);
    }
    {   // OUT: output + ceiling on top, mono freq + buttons below
        auto b = body (secOut);
        auto top = b.removeFromTop (b.getHeight() / 2);
        output.setBounds (top.removeFromLeft (top.getWidth() / 2));
        ceiling.setBounds (top);
        monoFreq.setBounds (b.removeFromLeft (b.getWidth() / 2));
        auto btns = b.reduced (6, 0).withSizeKeepingCentre (b.getWidth() - 12, 76);
        clipButton.setBounds (btns.removeFromTop (34));
        btns.removeFromTop (8);
        monoButton.setBounds (btns.removeFromTop (34));
    }
}
