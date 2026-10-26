// Loads the built Knockbox.vst3 the way a DAW does and exercises it:
// scanning, instantiation, processing (stereo + mono), automation, presets,
// state save/restore and opening the editor.
//
// Usage: KnockboxHostTest <plugin.vst3> <expected name> <expected preset count> <has ceiling 0|1>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <atomic>
#include <cstdlib>
#include <new>

//==============================================================================
// Allocation counter (Linux): replaces global operator new so we can count any memory
// allocation the plugin makes inside processBlock. Allocating on the audio thread can cause
// dropouts in a DAW. The executable exports these (-rdynamic), so the loaded plugin uses them.
#if JUCE_LINUX && ! defined (__SANITIZE_ADDRESS__)
 #define KB_COUNT_ALLOCS 1
 #include <execinfo.h>
static thread_local bool countAllocs = false;   // only the thread calling processBlock counts
static std::atomic<int>  allocCount  { 0 };
static void* countedAlloc (std::size_t n)
{
    if (countAllocs)
    {
        allocCount.fetch_add (1, std::memory_order_relaxed);
        if (std::getenv ("KB_ALLOC_TRACE") != nullptr)
        {
            countAllocs = false;
            void* frames[32];
            const int n = backtrace (frames, 32);
            backtrace_symbols_fd (frames, n, 2);
        }
    }
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new (std::size_t n)                               { return countedAlloc (n); }
void* operator new[] (std::size_t n)                             { return countedAlloc (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept   { try { return countedAlloc (n); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { try { return countedAlloc (n); } catch (...) { return nullptr; } }
void operator delete (void* p) noexcept                          { std::free (p); }
void operator delete[] (void* p) noexcept                        { std::free (p); }
void operator delete (void* p, std::size_t) noexcept             { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept           { std::free (p); }
#else
 #define KB_COUNT_ALLOCS 0
#endif

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << "  [" << (ok ? "ok" : "FAIL") << "] " << what << std::endl;
    if (! ok) ++failures;
}

static void fillTestSignal (juce::AudioBuffer<float>& b, double sr, int64_t& pos, juce::Random& rng)
{
    for (int i = 0; i < b.getNumSamples(); ++i, ++pos)
    {
        const double t   = (double) pos / sr;
        const double ph  = std::fmod (t, 60.0 / 90.0);                  // one beat at 90 bpm
        const double kick = std::sin (2 * juce::MathConstants<double>::pi * (55.0 + 80.0 * std::exp (-ph * 30)) * ph)
                            * std::exp (-ph * 8.0) * 0.7;
        const double hiss = (rng.nextDouble() * 2 - 1) * 0.05;
        for (int c = 0; c < b.getNumChannels(); ++c)
            b.setSample (c, i, (float) (kick + hiss * (c == 0 ? 1.0 : 0.8)));
    }
}

static bool allFinite (const juce::AudioBuffer<float>& b, float& peak)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (c, i);
            if (! std::isfinite (v)) return false;
            peak = juce::jmax (peak, std::abs (v));
        }
    return true;
}

int main (int argc, char** argv)
{
    if (argc < 5) { std::cout << "usage: KnockboxHostTest <plugin.vst3> <name> <presets> <hasCeiling>\n"; return 2; }
    const juce::String expectedName (argv[2]);
    const int expectedPrograms = juce::String (argv[3]).getIntValue();
    const bool hasCeiling = juce::String (argv[4]).getIntValue() != 0;
    juce::ScopedJuceInitialiser_GUI gui;

   #if KB_COUNT_ALLOCS
    {   // make sure the counter really works
        countAllocs = true;
        auto* probe = new int (1);
        countAllocs = false;
        check (allocCount.load() == 1, "allocation counter self-test");
        delete probe;
        allocCount = 0;
    }
   #endif

    const juce::String path = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName();
    std::cout << "Plugin: " << path << std::endl;

    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    auto* format = fm.getFormat (0);

    juce::OwnedArray<juce::PluginDescription> types;
    format->findAllTypesForFile (types, path);
    check (types.size() == 1, "scan finds exactly one plugin");
    if (types.isEmpty()) return 1;

    const auto& desc = *types[0];
    std::cout << "  name=" << desc.name << " vendor=" << desc.manufacturerName
              << " category=" << desc.category << std::endl;
    check (desc.name == expectedName, "name is " + expectedName);
    check (desc.manufacturerName == "8cee", "vendor is 8cee");

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::cout << "== " << sr << " Hz ==" << std::endl;
        juce::String err;
        auto plugin = fm.createPluginInstance (desc, sr, 512, err);
        check (plugin != nullptr, "instantiates (" + err + ")");
        if (! plugin) continue;

        check (plugin->getParameters().size() >= 20, "has its parameters ("
               + juce::String (plugin->getParameters().size()) + ")");
        check (plugin->getNumPrograms() == expectedPrograms, "has " + juce::String (expectedPrograms) + " factory presets");

        plugin->setPlayConfigDetails (2, 2, sr, 512);
        plugin->prepareToPlay (sr, 512);

        juce::Random rng (1234);
        juce::MidiBuffer midi;
        int64_t pos = 0;
        float peak = 0.0f;
        bool finite = true;

        // ~10 s of audio at default settings (allocations counted after a short warm-up)
        juce::AudioBuffer<float> buf (2, 512);
       #if KB_COUNT_ALLOCS
        allocCount = 0;
       #endif
        for (int blk = 0; blk < (int) (sr * 10 / 512); ++blk)
        {
            fillTestSignal (buf, sr, pos, rng);
           #if KB_COUNT_ALLOCS
            countAllocs = blk >= 4;
           #endif
            plugin->processBlock (buf, midi);
           #if KB_COUNT_ALLOCS
            countAllocs = false;
           #endif
            finite &= allFinite (buf, peak);
        }
        check (finite, "default settings: output is finite");
       #if KB_COUNT_ALLOCS
        check (allocCount.load() == 0, "no memory allocation on the audio thread ("
               + juce::String (allocCount.load()) + " allocations)");
       #endif
        if (hasCeiling)
        check (peak <= juce::Decibels::decibelsToGain (-0.3f) + 0.002f, "default settings: peak under ceiling ("
               + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");

        // random automation on every parameter, odd block sizes
        peak = 0.0f; finite = true;
        for (int blk = 0; blk < 400; ++blk)
        {
            for (auto* p : plugin->getParameters())
                if (p != plugin->getBypassParameter() && rng.nextInt (4) == 0)   // never bypass the plugin
                    p->setValue (rng.nextFloat());
            const int n = 1 + rng.nextInt (512);
            juce::AudioBuffer<float> b (2, n);
            fillTestSignal (b, sr, pos, rng);
            plugin->processBlock (b, midi);
            finite &= allFinite (b, peak);
        }
        check (finite, "random automation: output is finite");

        // make sure the plugin is not bypassed before measuring presets
        if (auto* bp = plugin->getBypassParameter())
            check (bp->getValue() < 0.5f, "plugin is not bypassed");

        // presets: each one runs 3 s of audio; level change vs. the input is measured after 0.5 s
        bool namesOk = true, levelsOk = true, ceilingOk = true, presetFinite = true;
        juce::StringArray seen;
        const bool printTable = sr == 48000.0;
        if (printTable) std::cout << "  preset levels (RMS change vs input, output peak):" << std::endl;
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
        {
            plugin->setCurrentProgram (i);
            const auto name = plugin->getProgramName (i);
            namesOk &= name.isNotEmpty() && ! seen.contains (name);
            seen.add (name);

            double inSq = 0, outSq = 0; float pPeak = 0.0f;
            const int blocks = (int) (sr * 3.0 / 512), warm = (int) (sr * 0.5 / 512);
            for (int blk = 0; blk < blocks; ++blk)
            {
                fillTestSignal (buf, sr, pos, rng);
                if (blk >= warm)
                    for (int c = 0; c < 2; ++c)
                        for (int k = 0; k < buf.getNumSamples(); ++k)
                            inSq += (double) buf.getSample (c, k) * buf.getSample (c, k);
                plugin->processBlock (buf, midi);
                float blkPeak = 0.0f;
                presetFinite &= allFinite (buf, blkPeak);
                if (blk >= warm)
                {
                    pPeak = juce::jmax (pPeak, blkPeak);
                    for (int c = 0; c < 2; ++c)
                        for (int k = 0; k < buf.getNumSamples(); ++k)
                            outSq += (double) buf.getSample (c, k) * buf.getSample (c, k);
                }
            }
            const double changeDb = 10.0 * std::log10 (juce::jmax (outSq, 1e-20) / juce::jmax (inSq, 1e-20));
            const double peakDb   = juce::Decibels::gainToDecibels ((double) pPeak, -120.0);
            // between -12 and +12 dB, and the preset must actually do something (not identical to input),
            // except presets that are meant to be (almost) transparent
            const bool meantClean = name == "Clean";
            const bool levelOk = changeDb > -24.0 && changeDb < 12.0
                                 && (meantClean || std::abs (changeDb) > 0.05 || pPeak > 0.0f);
            levelsOk &= levelOk;
            if (hasCeiling) ceilingOk &= pPeak <= juce::Decibels::decibelsToGain (-0.3f) + 0.002f;
            if (printTable)
                std::cout << "    " << juce::String (i + 1).paddedLeft (' ', 2) << "  " << name.paddedRight (' ', 20)
                          << (changeDb >= 0 ? " +" : " ") << juce::String (changeDb, 1) << " dB   peak "
                          << juce::String (peakDb, 1) << " dBFS" << (levelOk ? "" : "   <-- out of range") << std::endl;
        }
        check (namesOk, "every preset has a unique name");
        check (presetFinite, "every preset processes without NaN/Inf");
        check (levelsOk, "every preset stays within -24/+12 dB of the input level");
        if (hasCeiling)
            check (ceilingOk, "every preset stays under the -0.3 dBFS ceiling");

        // state round trip
        plugin->setCurrentProgram (3);
        // pick the first continuous parameter (bools and choices snap to steps)
        juce::AudioProcessorParameter* p0 = nullptr;
        for (auto* prm : plugin->getParameters())
            if (! prm->isBoolean() && ! prm->isDiscrete() && prm->getNumSteps() > 100) { p0 = prm; break; }
        if (p0 == nullptr) { check (false, "found a continuous parameter"); continue; }
        p0->setValue (0.123f);
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        p0->setValue (0.9f);
        plugin->setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (p0->getValue() - 0.123f) < 0.002f, "state save/restore round trip");

        plugin->releaseResources();

        // mono
        plugin->setPlayConfigDetails (1, 1, sr, 256);
        plugin->prepareToPlay (sr, 256);
        juce::AudioBuffer<float> mono (1, 256);
        finite = true;
        for (int blk = 0; blk < 100; ++blk)
        {
            fillTestSignal (mono, sr, pos, rng);
            plugin->processBlock (mono, midi);
            finite &= allFinite (mono, peak);
        }
        check (finite, "mono processing is finite");
        plugin->releaseResources();

        // editor
        if (sr == 48000.0)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
            check (ed != nullptr, "editor opens");
            if (ed)
                check (ed->getWidth() > 400 && ed->getHeight() > 200, "editor has a sensible size ("
                       + juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()) + ")");
            ed.reset();
        }
    }

    std::cout << (failures ? juce::String (failures) + " FAILURE(S)" : juce::String ("ALL HOST TESTS PASSED")) << std::endl;
    return failures ? 1 : 0;
}
