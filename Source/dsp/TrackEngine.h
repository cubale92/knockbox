// Knockbox Track — per-channel strip for 90s NY boom bap
// Chain: Input -> 4-band EQ (Q4-style) -> Knock (bump, mud cut, snap, transient punch) -> Tape -> Output
// Pure C++ (no JUCE) so it can be tested offline. Shares filter helpers with KnockEngine.h.

#pragma once

#include "KnockEngine.h"
#include <array>

namespace knock
{

struct TrackBand
{
    bool   on     = true;
    int    type   = (int) FilterType::Bell;
    float  freq   = 1000.0f;   // 20 .. 20000 Hz
    float  gainDb = 0.0f;      // -18 .. +18 dB (ignored by cut types)
    float  q      = 0.71f;     // 0.1 .. 10
};

struct TrackParams
{
    float     inputDb   = 0.0f;
    TrackBand band[4];
    float     knock     = 0.0f;    // 0..1
    float     knockFreq = 90.0f;   // 40 .. 250 Hz (kick ~50-100, snare body ~150-220)
    float     punch     = 0.0f;    // 0..1
    float     snap      = 0.0f;    // 0..1
    float     snapFreq  = 2500.0f; // 1 .. 6 kHz
    float     tape      = 0.0f;    // 0..1
    float     outputDb  = 0.0f;

    TrackParams()
    {
        band[0] = { true, (int) FilterType::LowCut,    30.0f,   0.0f, 0.71f };
        band[1] = { true, (int) FilterType::Bell,      200.0f,  0.0f, 1.0f  };
        band[2] = { true, (int) FilterType::Bell,      2000.0f, 0.0f, 1.0f  };
        band[3] = { true, (int) FilterType::HighShelf, 8000.0f, 0.0f, 0.71f };
    }
};

class TrackEngine
{
public:
    static constexpr int chunk = 8;     // control-rate update interval (small = no zipper steps)

    void prepare (double sampleRate, int numChannels)
    {
        fs  = sampleRate;
        nch = std::clamp (numChannels, 1, 2);
        const double cr = fs / chunk;
        for (auto* s : allSmoothers()) s->setup (cr, 0.03);

        mudCut.c = makeCoeffs (FilterType::Bell, fs, 320.0, 0.9, 0);
        detHp.c  = makeCoeffs (FilterType::LowCut, fs, 30.0, 0.71, 0);

        fastAtt = std::exp (-1.0 / (0.0005 * fs));
        fastRel = std::exp (-1.0 / (0.020  * fs));
        slowAtt = std::exp (-1.0 / (0.025  * fs));
        slowRel = std::exp (-1.0 / (0.120  * fs));
        tgSmooth = std::exp (-1.0 / (0.001 * fs));
        fadeStep = 1.0 / (0.005 * fs);
        warmUpSamples = (int) (0.010 * fs);

        prepared = true;
        reset();
    }

    void reset()
    {
        for (auto& b : eq) b.reset();
        for (auto* f : { &knockBump, &mudCut, &snapEq, &detHp, &headBump, &tapeHf }) f->reset();
        fastEnv = slowEnv = 0; tGain = 1;
        satX1[0] = satX1[1] = 0;
        satF1[0] = satF1[1] = logCosh (0.12);
        first = true;
    }

    void setParams (const TrackParams& np) { p = np; }

    void process (float* const* ch, int numChannels, int numSamples)
    {
        if (! prepared) return;
        const int nc = std::min (numChannels, nch);
        if (first)
        {
            setTargets();
            for (auto* s : allSmoothers()) s->snap (s->target);
            for (int b = 0; b < 4; ++b)
            {
                bandType[b] = (FilterType) p.band[b].type;
                bandFade[b] = bandFadeTarget[b] = p.band[b].on ? 1.0 : 0.0;
            }
            first = false;
        }

        for (int start = 0; start < numSamples; start += chunk)
        {
            const int n = std::min (chunk, numSamples - start);
            updateControl();
            processChunk (ch[0] + start, nc > 1 ? ch[1] + start : nullptr, n);
        }
        punchDbMeter = gainToDb (tGain);
    }

    float getPunchDb() const { return (float) punchDbMeter; }

    // Total EQ magnitude (for the display) at frequency f, using the target settings
    static double eqMagnitudeDb (const TrackParams& tp, double fs, double f)
    {
        double sum = 0;
        for (const auto& b : tp.band)
            if (b.on)
                sum += magnitudeDb (makeCoeffs ((FilterType) b.type, fs, b.freq, b.q, b.gainDb), fs, f);
        return sum;
    }

private:
    // Fixed-size: this is called on the audio thread, so it must not allocate
    std::array<Smoothed*, 20> allSmoothers()
    {
        return { &sIn, &sOut, &sKnock, &sKnockF, &sPunch, &sSnap, &sSnapF, &sTape,
                 &sF[0], &sG[0], &sQ[0], &sF[1], &sG[1], &sQ[1],
                 &sF[2], &sG[2], &sQ[2], &sF[3], &sG[3], &sQ[3] };
    }

    void setTargets()
    {
        sIn.target     = dbToGain (p.inputDb);
        sOut.target    = dbToGain (p.outputDb);
        sKnock.target  = p.knock;
        sKnockF.target = std::log (std::clamp ((double) p.knockFreq, 20.0, 1000.0));
        sPunch.target  = p.punch;
        sSnap.target   = p.snap;
        sSnapF.target  = std::log (std::clamp ((double) p.snapFreq, 500.0, 12000.0));
        sTape.target   = p.tape;
        for (int b = 0; b < 4; ++b)
        {
            sF[b].target = std::log (std::clamp ((double) p.band[b].freq, 20.0, 20000.0));
            sG[b].target = p.band[b].gainDb;
            sQ[b].target = std::log (std::clamp ((double) p.band[b].q, 0.1, 18.0));
        }
    }

    void updateControl()
    {
        setTargets();
        inA = sIn.cur;  inB = sIn.step();
        outA = sOut.cur; outB = sOut.step();

        for (int b = 0; b < 4; ++b)
        {
            const double f = std::exp (sF[b].step());
            const double g = sG[b].step();
            const double q = std::exp (sQ[b].step());
            // Switching a band on/off or changing its type fades it out, swaps, and fades back in
            // (5 ms), so there is no click on sustained sounds.
            // Bands keep running while switched off, so they are "warm" when switched back on.
            // After a type change the new filter runs silently for 10 ms before fading in.
            const auto wanted = (FilterType) p.band[b].type;
            if (bandFade[b] <= 0.0 && wanted != bandType[b])
            {
                bandType[b] = wanted;
                eq[b].clearState();
                warmUp[b] = warmUpSamples;
            }
            bandFadeTarget[b] = (p.band[b].on && wanted == bandType[b] && warmUp[b] <= 0) ? 1.0 : 0.0;
            eq[b].design (bandType[b], fs, f, q, g);
        }

        const double k = sKnock.step();
        knockBump.design (FilterType::Bell, fs, std::exp (sKnockF.step()), 1.1, k * 6.0);
        mudCut.design (FilterType::Bell, fs, 320.0, 0.9, -k * 2.5);
        punchAmt    = sPunch.step();
        const double sn = sSnap.step();
        snapEq.design (FilterType::Bell, fs, std::exp (sSnapF.step()), 0.9, sn * 6.0);

        tapeA     = sTape.cur;
        tapeAmt   = sTape.step();
        tapeDrive = dbToGain (tapeAmt * 15.0) / 2.0;
        headBump.design (FilterType::LowShelf, fs, 90.0, 0.71, tapeAmt * 1.2);
        tapeHf.design (FilterType::HighShelf, fs, std::min (12000.0, fs * 0.45), 0.71, -tapeAmt * 1.5);
    }

    // ADAA (first order) asymmetric tanh: f(x) = tanh(x+b) - tanh(b); antiderivative cached per channel
    inline double tapeSat (double x, int ch)
    {
        constexpr double b  = 0.12;
        constexpr double tb = 0.11942729853438588;   // tanh(0.12)
        const double F  = logCosh (x + b) - tb * x;
        const double x1 = satX1[ch], F1 = satF1[ch];
        satX1[ch] = x; satF1[ch] = F;
        const double dx = x - x1;
        if (std::abs (dx) < 1.0e-6) return std::tanh (0.5 * (x + x1) + b) - tb;
        return (F - F1) / dx;
    }

    void processChunk (float* L, float* R, int n)
    {
        const bool stereo = R != nullptr;
        const int nc = stereo ? 2 : 1;
        const double inv = 1.0 / n;

        for (int i = 0; i < n; ++i)
        {
            const double t = (i + 1) * inv;
            const double gIn  = inA + (inB - inA) * t;
            const double gOut = outA + (outB - outA) * t;
            double s[2] = { L[i] * gIn, stereo ? R[i] * gIn : 0.0 };

            for (int b = 0; b < 4; ++b)
            {
                double& fd = bandFade[b];
                if (fd != bandFadeTarget[b])
                    fd = bandFadeTarget[b] > fd ? std::min (1.0, fd + fadeStep) : std::max (0.0, fd - fadeStep);
                if (warmUp[b] > 0) --warmUp[b];
                for (int c = 0; c < nc; ++c)
                {
                    const double wet = eq[b].process (s[c], c);       // always run, keeps the state warm
                    s[c] = fd >= 1.0 ? wet : s[c] + fd * fd * (3.0 - 2.0 * fd) * (wet - s[c]);   // S-curve fade
                }
            }

            for (int c = 0; c < nc; ++c)
                s[c] = snapEq.process (mudCut.process (knockBump.process (s[c], c), c), c);

            // transient punch (linked)
            {
                const double det = detHp.process (stereo ? 0.5 * (s[0] + s[1]) : s[0], 0);
                const double a = std::abs (det);
                fastEnv = a > fastEnv ? fastAtt * fastEnv + (1 - fastAtt) * a : fastRel * fastEnv + (1 - fastRel) * a;
                slowEnv = a > slowEnv ? slowAtt * slowEnv + (1 - slowAtt) * a : slowRel * slowEnv + (1 - slowRel) * a;
                // transient ratio fast/slow (clamped to 0..12 dB), raised to the punch amount
                double target = 1.0;
                if (punchAmt > 0.0 && fastEnv > 1.0e-5)
                {
                    const double ratio = std::clamp (fastEnv / std::max (slowEnv, 1.0e-6), 1.0, 3.981071705534973);
                    target = std::exp (std::log (ratio) * punchAmt * 0.8);
                }
                tGain = tgSmooth * tGain + (1 - tgSmooth) * target;
                if (punchAmt > 0.0001 || tGain > 1.0001)
                    for (int c = 0; c < nc; ++c) s[c] *= tGain;
            }

            for (int c = 0; c < nc; ++c)
            {
                double v = headBump.process (s[c], c);
                const double tNow = tapeA + (tapeAmt - tapeA) * t;
                if (tNow > 0.0)
                {
                    // blend the saturation in over the first 5% of the knob, so it never switches on with a jump
                    const double wetAmt = std::min (1.0, tNow * 20.0);
                    v += wetAmt * (tapeSat (v * tapeDrive, c) / tapeDrive - v);
                }
                s[c] = tapeHf.process (v, c);
            }

            L[i] = (float) (s[0] * gOut);
            if (stereo) R[i] = (float) (s[1] * gOut);
        }
    }

    double fs = 44100;
    int nch = 2;
    bool prepared = false, first = true;
    TrackParams p;

    Smoothed sIn, sOut, sKnock, sKnockF, sPunch, sSnap, sSnapF, sTape, sF[4], sG[4], sQ[4];
    double inA = 1, inB = 1, outA = 1, outB = 1;

    Biquad eq[4];
    double bandFade[4] { 0, 0, 0, 0 }, bandFadeTarget[4] { 0, 0, 0, 0 }, fadeStep = 0.01;
    int    warmUp[4] { 0, 0, 0, 0 }, warmUpSamples = 441;
    FilterType bandType[4] { FilterType::Bell, FilterType::Bell, FilterType::Bell, FilterType::Bell };

    Biquad knockBump, mudCut, snapEq, detHp;
    double punchAmt = 0, fastEnv = 0, slowEnv = 0, tGain = 1, punchDbMeter = 0;
    double fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0, tgSmooth = 0;

    Biquad headBump, tapeHf;
    double tapeA = 0, tapeAmt = 0, tapeDrive = 1, satX1[2] { 0, 0 }, satF1[2] { 0, 0 };
};

} // namespace knock
