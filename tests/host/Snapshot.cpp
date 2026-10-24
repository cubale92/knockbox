// Renders the Knockbox editor to a PNG while audio runs, so the UI and meters can be checked.
// Usage: KnockboxSnapshot <out.png>

#ifdef SNAPSHOT_TRACK
 #include "track/TrackProcessor.h"
 using SnapProcessor = KnockboxTrackProcessor;
#else
 #include "master/PluginProcessor.h"
 using SnapProcessor = KnockboxProcessor;
#endif
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "knockbox.png");

    SnapProcessor proc;
#ifdef SNAPSHOT_TRACK
    proc.setCurrentProgram (1);   // show a preset with a shaped curve
#endif
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    int64_t pos = 0;
    juce::Random rng (7);
    for (int step = 0; step < 60; ++step)
    {
        for (int i = 0; i < 512; ++i, ++pos)
        {
            const double t  = pos / 48000.0;
            const double ph = std::fmod (t, 0.6667);
            const double v  = std::sin (2 * juce::MathConstants<double>::pi * (55.0 + 80.0 * std::exp (-ph * 30)) * ph)
                              * std::exp (-ph * 5.0) * 0.9 + (rng.nextDouble() * 2 - 1) * 0.08;
            buf.setSample (0, i, (float) v);
            buf.setSample (1, i, (float) (v * 0.95));
        }
        proc.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (16);
    }

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::FileOutputStream os (out);
    os.setPosition (0); os.truncate();
    juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::cout << (ok ? "wrote " : "FAILED writing ") << out.getFullPathName() << std::endl;
    ed.reset();
    return ok ? 0 : 1;
}
