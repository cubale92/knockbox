#include "TrackEditor.h"

using knock::FilterType;

namespace
{
    inline juce::Font font (float h, bool b = false) { return knockFont (h, b); }
    constexpr int editorW = 1064, editorH = 610;

    bool isCut (int type) { return type == (int) FilterType::LowCut || type == (int) FilterType::HighCut; }
}

//==============================================================================
// EqGraph
//==============================================================================
EqGraph::EqGraph (KnockboxTrackProcessor& p) : proc (p)
{
    shown = proc.readParams();
    setRepaintsOnMouseActivity (false);
}

juce::Rectangle<float> EqGraph::plot() const { return getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedBottom (8.0f); }

float EqGraph::xForFreq (double f) const
{
    const auto r = plot();
    return r.getX() + r.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
}

double EqGraph::freqForX (float x) const
{
    const auto r = plot();
    return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / r.getWidth())));
}

float EqGraph::yForDb (double db) const
{
    const auto r = plot();
    return r.getCentreY() - (float) (db / dbRange) * r.getHeight() * 0.5f;
}

double EqGraph::dbForY (float y) const
{
    const auto r = plot();
    return (r.getCentreY() - y) / (r.getHeight() * 0.5f) * dbRange;
}

juce::Point<float> EqGraph::nodePos (int b) const
{
    const auto& band = shown.band[b];
    const double y = isCut (band.type) ? 0.0 : band.gainDb;
    return { xForFreq (band.freq), yForDb (y) };
}

int EqGraph::hitBand (juce::Point<float> pt) const
{
    int best = -1; float bestD = 16.0f;
    for (int b = 0; b < 4; ++b)
    {
        const float d = nodePos (b).getDistanceFrom (pt);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqGraph::setParam (const juce::String& id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void EqGraph::refresh()
{
    const auto now = proc.readParams();
    bool changed = std::abs (proc.getCurrentSampleRate() - fs) > 1.0;
    for (int b = 0; b < 4 && ! changed; ++b)
    {
        const auto& a = now.band[b]; const auto& o = shown.band[b];
        changed = a.on != o.on || a.type != o.type || a.freq != o.freq || a.gainDb != o.gainDb || a.q != o.q;
    }
    if (changed)
    {
        shown = now;
        fs = proc.getCurrentSampleRate();
        repaint();
    }
}

void EqGraph::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto r = plot();

    g.setColour (bg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // grid
    g.setFont (font (10.0f));
    for (double f : { 30.0, 50.0, 100.0, 200.0, 300.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 10000.0 })
    {
        const float x = xForFreq (f);
        const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
        g.setColour (line.withAlpha (major ? 0.9f : 0.45f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (muted.withAlpha (0.8f));
        const juce::String label = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, r.getBottom() + 1.0f, 40.0f, 12.0f), juce::Justification::centred);
    }
    for (double db : { -18.0, -12.0, -6.0, 0.0, 6.0, 12.0, 18.0 })
    {
        const float y = yForDb (db);
        g.setColour (line.withAlpha (db == 0.0 ? 1.0f : 0.45f));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        if (db != 0.0)
        {
            g.setColour (muted.withAlpha (0.7f));
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getX() + 2.0f, y - 12.0f, 30.0f, 12.0f),
                        juce::Justification::centredLeft);
        }
    }

    // per-band faint curves
    const int steps = juce::jmax (64, (int) r.getWidth());
    for (int b = 0; b < 4; ++b)
    {
        if (! shown.band[b].on) continue;
        const auto& band = shown.band[b];
        const auto c = knock::makeCoeffs ((FilterType) band.type, fs, band.freq, band.q, band.gainDb);
        juce::Path pth;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
            const float y = juce::jlimit (r.getY(), r.getBottom(), yForDb (knock::magnitudeDb (c, fs, freqForX (x))));
            if (i == 0) pth.startNewSubPath (x, y); else pth.lineTo (x, y);
        }
        g.setColour (bandColours::c[b].withAlpha (0.35f));
        g.strokePath (pth, juce::PathStrokeType (1.2f));
    }

    // total curve
    juce::Path curve;
    for (int i = 0; i <= steps; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) steps;
        const double db = knock::TrackEngine::eqMagnitudeDb (shown, fs, freqForX (x));
        const float y = juce::jlimit (r.getY(), r.getBottom(), yForDb (db));
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    juce::Path fill (curve);
    fill.lineTo (r.getRight(), yForDb (0.0));
    fill.lineTo (r.getX(), yForDb (0.0));
    fill.closeSubPath();
    g.setColour (accent.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (text);
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // nodes
    for (int b = 0; b < 4; ++b)
    {
        const auto pt = nodePos (b);
        const bool on = shown.band[b].on;
        const float rad = (b == hover || b == dragging) ? 10.0f : 8.0f;
        g.setColour (on ? bandColours::c[b] : muted.withAlpha (0.4f));
        g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2);
        g.setColour (bg);
        g.setFont (font (11.0f, true));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (pt.x - rad, pt.y - rad, rad * 2, rad * 2), juce::Justification::centred);
    }

    // readout while hovering / dragging
    const int active = dragging >= 0 ? dragging : hover;
    if (active >= 0)
    {
        const auto& band = shown.band[active];
        juce::String s = juce::String (active + 1) + "  " + KnockboxTrackProcessor::typeNames()[band.type] + "  "
                       + (band.freq >= 1000.0f ? juce::String (band.freq / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (band.freq)) + " Hz");
        if (! isCut (band.type)) s << "  " << (band.gainDb > 0 ? "+" : "") << juce::String (band.gainDb, 1) << " dB";
        s << "  Q " << juce::String (band.q, 2);
        g.setColour (text);
        g.setFont (font (12.0f, true));
        g.drawText (s, r.withHeight (18.0f).translated (0, 2), juce::Justification::centredRight);
    }
    else
    {
        g.setColour (muted.withAlpha (0.8f));
        g.setFont (font (11.0f));
        g.drawText ("drag a point  |  scroll = width  |  double-click = 0 dB", r.withHeight (18.0f).translated (0, 2),
                    juce::Justification::centredRight);
    }
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitBand (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void EqGraph::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitBand (e.position);
    if (dragging < 0) return;
    if (! shown.band[dragging].on)
        setParam (tids::bandOn (dragging), 1.0f);
    for (auto id : { tids::bandFreq (dragging), tids::bandGain (dragging) })
        if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture();
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    setParam (tids::bandFreq (dragging), (float) juce::jlimit (20.0, 20000.0, freqForX (e.position.x)));
    if (! isCut (shown.band[dragging].type))
        setParam (tids::bandGain (dragging), (float) juce::jlimit (-18.0, 18.0, dbForY (e.position.y)));
    refresh();
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (auto id : { tids::bandFreq (dragging), tids::bandGain (dragging) })
            if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture();
    dragging = -1;
    repaint();
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitBand (e.position);
    if (b < 0) return;
    if (auto* p = proc.apvts.getParameter (tids::bandGain (b)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
        p->endChangeGesture();
    }
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = dragging >= 0 ? dragging : hitBand (e.position);
    if (b < 0) return;
    if (auto* p = proc.apvts.getParameter (tids::bandQ (b)))
    {
        const float q = juce::jlimit (0.1f, 10.0f, shown.band[b].q * std::exp (w.deltaY * 1.5f));
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (q));
        p->endChangeGesture();
    }
}

//==============================================================================
// BandStrip
//==============================================================================
BandStrip::BandStrip (KnockboxTrackProcessor& p, int b)
    : proc (p), band (b),
      freq (p.apvts, tids::bandFreq (b), "Freq"),
      gain (p.apvts, tids::bandGain (b), "Gain"),
      q    (p.apvts, tids::bandQ (b),    "Q")
{
    onButton.setClickingTogglesState (true);
    onButton.setColour (juce::TextButton::buttonOnColourId, bandColours::c[b]);
    addAndMakeVisible (onButton);
    onAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, tids::bandOn (b), onButton);

    typeBox.addItemList (KnockboxTrackProcessor::typeNames(), 1);
    addAndMakeVisible (typeBox);
    typeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, tids::bandType (b), typeBox);

    for (auto* k : { &freq, &gain, &q })
    {
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, bandColours::c[b]);
        addAndMakeVisible (*k);
    }
}

void BandStrip::refresh()
{
    const int type = (int) std::lround (proc.apvts.getRawParameterValue (tids::bandType (band))->load());
    const bool on  = proc.apvts.getRawParameterValue (tids::bandOn (band))->load() > 0.5f;
    gain.setAlpha (isCut (type) ? 0.3f : 1.0f);
    gain.setEnabled (! isCut (type));
    const float a = on ? 1.0f : 0.5f;
    freq.setAlpha (a); q.setAlpha (a);
    if (! isCut (type)) gain.setAlpha (a);
}

void BandStrip::paint (juce::Graphics& g)
{
    using namespace palette;
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    g.setColour (line);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);

    auto head = getLocalBounds().removeFromTop (34).reduced (12, 0);
    g.setColour (bandColours::c[band]);
    g.fillEllipse ((float) head.getX(), (float) head.getCentreY() - 9.0f, 18.0f, 18.0f);
    g.setColour (bg);
    g.setFont (font (12.0f, true));
    g.drawText (juce::String (band + 1), juce::Rectangle<int> (head.getX(), head.getCentreY() - 9, 18, 18), juce::Justification::centred);
    g.setColour (muted);
    g.setFont (font (12.0f, true));
    g.drawText ("BAND", head.withTrimmedLeft (26), juce::Justification::centredLeft);
}

void BandStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    auto head = r.removeFromTop (34);
    onButton.setBounds (head.removeFromRight (46).reduced (0, 6));
    r.removeFromTop (2);
    typeBox.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    auto top = r.removeFromTop (r.getHeight() / 2);
    freq.setBounds (top.removeFromLeft (top.getWidth() / 2));
    gain.setBounds (top);
    q.setBounds (r.withSizeKeepingCentre (r.getWidth() / 2 + 10, r.getHeight()));
}

//==============================================================================
// Editor
//==============================================================================
KnockboxTrackEditor::KnockboxTrackEditor (KnockboxTrackProcessor& p)
    : AudioProcessorEditor (p), proc (p), graph (p),
      knock    (p.apvts, tids::knock,     "Knock", true),
      knockFreq(p.apvts, tids::knockFreq, "Freq"),
      punch    (p.apvts, tids::punch,     "Punch", true),
      snap     (p.apvts, tids::snap,      "Snap"),
      snapFreq (p.apvts, tids::snapFreq,  "Snap Hz"),
      input    (p.apvts, tids::input,     "Input"),
      tape     (p.apvts, tids::tape,      "Tape", true),
      output   (p.apvts, tids::output,    "Output")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (graph);
    for (int b = 0; b < 4; ++b)
    {
        strips[b] = std::make_unique<BandStrip> (p, b);
        addAndMakeVisible (*strips[b]);
    }
    for (auto* k : { &knock, &knockFreq, &punch, &snap, &snapFreq, &input, &tape, &output })
        addAndMakeVisible (*k);

    const auto& list = KnockboxTrackProcessor::presets();
    for (int i = 0; i < (int) list.size(); ++i)
        presetBox.addItem (list[(size_t) i].name, i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0) proc.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);

    setSize (editorW, editorH);
    startTimerHz (30);
}

KnockboxTrackEditor::~KnockboxTrackEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KnockboxTrackEditor::timerCallback()
{
    graph.refresh();
    for (auto& s : strips) s->refresh();

    const float pk = proc.meterPunchDb.load();
    punchShown = pk > punchShown ? pk : punchShown * 0.85f;
    const float out = juce::Decibels::gainToDecibels (proc.meterOut.exchange (0.0f), -60.0f);
    outShown = out > outShown ? out : juce::jmax (-60.0f, outShown - 1.2f);

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);

    repaint (outMeter.expanded (2));
    repaint (secKnock.withHeight (34));
}

void KnockboxTrackEditor::drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& t, const juce::String& sub)
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

void KnockboxTrackEditor::paint (juce::Graphics& g)
{
    using namespace palette;
    g.fillAll (bg);

    auto head = getLocalBounds().removeFromTop (60).reduced (20, 0);
    g.setColour (text);
    g.setFont (font (30.0f, true));
    g.drawText ("KNOCKBOX", head.removeFromLeft (190), juce::Justification::centredLeft);
    g.setColour (accent);
    g.setFont (font (12.0f, true));
    g.drawText ("TRACK  |  EQ + KNOCK + TAPE", head.removeFromLeft (260), juce::Justification::centredLeft);
    g.setColour (muted);
    g.setFont (font (12.0f));
    g.drawText ("8cee", head.removeFromRight (40), juce::Justification::centredRight);

    drawSection (g, secEq, "EQ", "4 bands");
    drawSection (g, secKnock, "KNOCK", "punch the hit");
    drawSection (g, secOut, "TAPE / OUT", "");

    const float pa = juce::jlimit (0.0f, 1.0f, punchShown / 6.0f);
    g.setColour (accent.withAlpha (0.15f + 0.85f * pa));
    g.fillEllipse ((float) secKnock.getX() + 74.0f, (float) secKnock.getY() + 13.0f, 8.0f, 8.0f);

    // output meter
    auto m = outMeter.toFloat();
    g.setColour (bg);
    g.fillRoundedRectangle (m, 3.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (outShown + 48.0f) / 48.0f);
    g.setColour (outShown > -0.1f ? hot : accent);
    g.fillRoundedRectangle (m.withWidth (m.getWidth() * frac), 3.0f);
    g.setColour (muted);
    g.setFont (font (10.0f, true));
    g.drawText ("OUT", outMeter.translated (0, -14).withHeight (12), juce::Justification::centredLeft);
}

void KnockboxTrackEditor::resized()
{
    auto r = getLocalBounds().reduced (14, 0);
    auto header = r.removeFromTop (60);
    presetBox.setBounds (header.removeFromRight (290).withSizeKeepingCentre (230, 32).translated (-30, 0));
    r.removeFromBottom (14);

    const int gap = 8;
    auto left = r.removeFromLeft (690);
    r.removeFromLeft (gap);

    secEq = left.removeFromTop (250);
    graph.setBounds (secEq.withTrimmedTop (40).reduced (8, 0).withTrimmedBottom (8));
    left.removeFromTop (gap);
    const int sw = (left.getWidth() - 3 * gap) / 4;
    for (int b = 0; b < 4; ++b)
    {
        strips[b]->setBounds (left.removeFromLeft (sw));
        left.removeFromLeft (gap);
    }

    secKnock = r.removeFromTop (330);
    r.removeFromTop (gap);
    secOut = r;

    {
        auto b = secKnock.withTrimmedTop (40).reduced (6, 4);
        auto top = b.removeFromTop (b.getHeight() / 2);
        knock.setBounds (top.removeFromLeft (top.getWidth() / 2));
        knockFreq.setBounds (top);
        punch.setBounds (b.removeFromLeft (b.getWidth() / 2));
        auto sn = b;
        snap.setBounds (sn.removeFromLeft (sn.getWidth() / 2).withTrimmedLeft (-8));
        snapFreq.setBounds (sn.withTrimmedRight (-8));
    }
    {
        auto b = secOut.withTrimmedTop (40).reduced (6, 4);
        outMeter = b.removeFromBottom (12).reduced (10, 0);
        b.removeFromBottom (18);
        const int cw = b.getWidth() / 3;
        input.setBounds (b.removeFromLeft (cw));
        tape.setBounds (b.removeFromLeft (cw));
        output.setBounds (b);
    }
}
