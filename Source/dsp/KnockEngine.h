// Knockbox — 90s NY boom bap master-bus processor
// Pure C++ DSP engine (no JUCE dependency) so it can be tested offline.
//
// Chain:  Input -> Knock (low bump, mud cut, snap, transient punch)
//         -> Tape (ADAA asymmetric saturation, head bump) -> Glue (parallel bus comp)
//         -> Dust (top roll-off, vinyl crackle/hiss) -> Mono bass -> Output -> Clipper

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace knock
{

constexpr double kPi = 3.14159265358979323846;

inline double dbToGain (double db)  { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g)   { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

//==============================================================================
// RBJ biquad, transposed direct form II, double-precision state, 2 channels
//==============================================================================
struct Coeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

enum class FilterType
{
    LowCut = 0,
    LowShelf,
    Bell,
    HighShelf,
    HighCut
};

inline Coeffs makeCoeffs (FilterType type, double fs, double freq, double q, double gainDb)
{
    freq = std::clamp (freq, 10.0, fs * 0.49);
    q    = std::max (q, 0.05);

    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = 2.0 * kPi * freq / fs;
    const double cw    = std::cos (w0);
    const double sw    = std::sin (w0);
    const double alpha = sw / (2.0 * q);

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case FilterType::LowCut:
            b0 = (1 + cw) / 2;  b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha;     a1 = -2 * cw;   a2 = 1 - alpha;
            break;

        case FilterType::HighCut:
            b0 = (1 - cw) / 2;  b1 = 1 - cw;    b2 = (1 - cw) / 2;
            a0 = 1 + alpha;     a1 = -2 * cw;   a2 = 1 - alpha;
            break;

        case FilterType::Bell:
            b0 = 1 + alpha * A; b1 = -2 * cw;   b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw;   a2 = 1 - alpha / A;
            break;

        case FilterType::LowShelf:
        {
            const double sq = 2.0 * std::sqrt (A) * alpha;
            b0 =      A * ((A + 1) - (A - 1) * cw + sq);
            b1=  2 * A * ((A - 1) - (A + 1) * cw);
            b2 =      A * ((A + 1) - (A - 1) * cw - sq);
            a0 =           (A + 1) + (A - 1) * cw + sq;
            a1 =     -2 * ((A - 1) + (A + 1) * cw);
            a2 =           (A + 1) + (A - 1) * cw - sq;
            break;
        }

        case FilterType::HighShelf:
        {
            const double sq = 2.0 * std::sqrt (A) * alpha;
            b0 =      A * ((A + 1) + (A - 1) * cw + sq);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 =      A * ((A + 1) + (A - 1) * cw - sq);
            a0 =           (A + 1) - (A - 1) * cw + sq;
            a1 =      2 * ((A - 1) - (A + 1) * cw);
            a2 =           (A + 1) - (A - 1) * cw - sq;
            break;
        }
    }

    Coeffs c;
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
    c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

// Magnitude response in dB of one biquad at frequency f (used by the EQ display)
inline double magnitudeDb (const Coeffs& c, double fs, double f)
{
    const double w = 2.0 * kPi * f / fs;
    const double cr1 = std::cos (w),  ci1 = -std::sin (w);
    const double cr2 = std::cos (2 * w), ci2 = -std::sin (2 * w);

    const double nr = c.b0 + c.b1 * cr1 + c.b2 * cr2;
    const double ni =        c.b1 * ci1 + c.b2 * ci2;
    const double dr = 1.0  + c.a1 * cr1 + c.a2 * cr2;
    const double di =        c.a1 * ci1 + c.a2 * ci2;

    const double num = nr * nr + ni * ni;
    const double den = dr * dr + di * di;
    return 10.0 * std::log10 (std::max (num, 1.0e-24) / std::max (den, 1.0e-24));
}

struct Biquad
{
    Coeffs c;
    double z1[2] { 0, 0 }, z2[2] { 0, 0 };

    void clearState() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    void reset()      { clearState(); lastType = -1; }   // also forces the next design() to recompute

    // Recompute coefficients only when a setting actually changed (saves CPU when knobs are still)
    void design (FilterType t, double fs, double f, double q, double g)
    {
        if ((int) t == lastType && fs == lastFs && f == lastF && q == lastQ && g == lastG) return;
        lastType = (int) t; lastFs = fs; lastF = f; lastQ = q; lastG = g;
        c = makeCoeffs (t, fs, f, q, g);
    }
    int lastType = -1;
    double lastFs = 0, lastF = 0, lastQ = 0, lastG = 0;

    inline double process (double x, int ch)
    {
        const double y = c.b0 * x + z1[ch];
        z1[ch] = c.b1 * x - c.a1 * y + z2[ch];
        z2[ch] = c.b2 * x - c.a2 * y;
        return y;
    }
};

//==============================================================================
// Small helpers
//==============================================================================
struct XorShift
{
    uint32_t s = 0x9E3779B9u;
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni()     { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    inline float bi()      { return uni() * 2.0f - 1.0f; }                              // [-1,1)
};

inline double logCosh (double x)
{
    const double ax = std::abs (x);
    return ax + std::log1p (std::exp (-2.0 * ax)) - 0.69314718055994530942;
}

// One-pole smoother evaluated once per control chunk
struct Smoothed
{
    double cur = 0, target = 0, coeff = 0;
    void setup (double chunkRate, double timeSec) { coeff = std::exp (-1.0 / (timeSec * chunkRate)); }
    void snap (double v)  { cur = target = v; }
    double step()
    {
        cur = target + (cur - target) * coeff;
        if (std::abs (cur - target) < 1.0e-7 * (1.0 + std::abs (target))) cur = target;   // settle exactly
        return cur;
    }
};

//==============================================================================
// Parameters (plain values, set by the host wrapper)
//==============================================================================
struct Params
{
    float  inputDb     = 0.0f;     // -18 .. +18

    float  knock       = 0.5f;     // 0..1  low bump + mud cut
    float  knockFreq   = 95.0f;    // 45 .. 160 Hz
    float  punch       = 0.35f;     // 0..1  transient emphasis
    float  snap        = 0.2f;     // 0..1  2.5 kHz crack

    float  tape        = 0.35f;    // 0..1

    float  glueThresh  = -12.0f;   // -36 .. 0 dB
    float  glueRatio   = 4.0f;     // 1.5 .. 10
    float  glueAttack  = 10.0f;    // 0.1 .. 80 ms
    float  glueRelease = 150.0f;   // 30 .. 1200 ms
    float  glueMakeup  = 2.0f;     // 0 .. 18 dB
    float  glueMix     = 0.5f;     // 0..1  (parallel / NY compression)
    float  glueScHpf   = 60.0f;    // 20 .. 300 Hz

    float  dust        = 0.15f;    // 0..1  top roll-off
    float  vinyl       = 0.0f;     // 0..1  crackle + hiss

    bool   monoBass    = false;
    float  monoFreq    = 120.0f;   // 40 .. 250 Hz

    float  outputDb    = 0.0f;     // -18 .. +18
    bool   clipOn      = true;
    float  ceilingDb   = -0.3f;    // -12 .. 0
};

inline Params defaultParams() { return Params{}; }

//==============================================================================
// The engine
//==============================================================================
class KnockEngine
{
public:
    static constexpr int chunk = 8;    // control-rate update interval in samples (small = no zipper steps)

    void prepare (double sampleRate, int numChannels)
    {
        fs  = sampleRate;
        nch = std::clamp (numChannels, 1, 2);

        const double chunkRate = fs / chunk;
        for (auto* s : { &sIn, &sOut, &sGlueMix, &sMakeup, &sKnock, &sPunch, &sSnap, &sTape,
                         &sDust, &sVinyl, &sCeil, &sKnockFreq })
            s->setup (chunkRate, 0.03);

        // fixed filters
        dcCut.c   = makeCoeffs (FilterType::LowCut, fs, 18.0, 0.71, 0);
        mudCut.c  = makeCoeffs (FilterType::Bell, fs, 320.0, 0.9, 0);
        detHp.c   = makeCoeffs (FilterType::LowCut, fs, 40.0, 0.71, 0);
        detLp.c   = makeCoeffs (FilterType::HighCut, fs, std::min (5000.0, fs * 0.45), 0.71, 0);
        crackHp.c = makeCoeffs (FilterType::LowCut, fs, 700.0, 0.71, 0);
        hissLp.c  = makeCoeffs (FilterType::HighCut, fs, std::min (6000.0, fs * 0.45), 0.71, 0);

        // envelope coefficients
        fastAtt = std::exp (-1.0 / (0.0005 * fs));
        fastRel = std::exp (-1.0 / (0.020  * fs));
        slowAtt = std::exp (-1.0 / (0.025  * fs));
        slowRel = std::exp (-1.0 / (0.120  * fs));
        tgSmooth = std::exp (-1.0 / (0.001 * fs));
        crackDecay = std::exp (-1.0 / (0.00025 * fs));
        fadeStep   = 1.0 / (0.005 * fs);

        prepared = true;
        reset();
    }

    void reset()
    {
        for (auto* f : { &dcCut, &knockBump, &mudCut, &snapEq, &detHp, &detLp, &headBump, &tapeHf,
                         &scHp, &dustLp1, &dustLp2, &monoHp1, &monoHp2, &crackHp, &hissLp })
            f->reset();

        fastEnv = slowEnv = 0; tGain = 1.0;
        glueEnvDb = 0;
        satX1[0] = satX1[1] = 0;
        satF1[0] = satF1[1] = logCosh (0.12);
        clipX1[0] = clipX1[1] = 0;
        crack[0] = crack[1] = 0;
        grDb = 0;
        firstBlock = true;
    }

    void setParams (const Params& np) { p = np; }

    // In-place processing. ch[0] = left, ch[1] = right (if present)
    void process (float* const* ch, int numChannels, int numSamples)
    {
        if (! prepared) return;
        const int nc = std::min (numChannels, nch);

        if (firstBlock) { snapAll(); monoFade = p.monoBass ? 1.0 : 0.0; firstBlock = false; }

        for (int start = 0; start < numSamples; start += chunk)
        {
            const int n = std::min (chunk, numSamples - start);
            updateControl();
            float* L = ch[0] + start;
            float* R = nc > 1 ? ch[1] + start : nullptr;
            processChunk (L, R, n);
        }
        punchDbMeter = gainToDb (tGain);
    }

    float getGainReductionDb() const { return (float) grDb; }
    float getPunchDb() const         { return (float) punchDbMeter; }


private:
    //--------------------------------------------------------------------------
    void setTargets()
    {
        sIn.target        = dbToGain (p.inputDb);
        sOut.target       = dbToGain (p.outputDb);
        sGlueMix.target   = p.glueMix;
        sMakeup.target    = dbToGain (p.glueMakeup);
        sKnock.target     = p.knock;
        sKnockFreq.target = p.knockFreq;
        sPunch.target     = p.punch;
        sSnap.target      = p.snap;
        sTape.target      = p.tape;
        sDust.target      = p.dust;
        sVinyl.target     = p.vinyl;
        sCeil.target      = dbToGain (p.ceilingDb);
    }

    void snapAll()
    {
        setTargets();
        for (auto* s : { &sIn, &sOut, &sGlueMix, &sMakeup, &sKnock, &sPunch, &sSnap, &sTape,
                         &sDust, &sVinyl, &sCeil, &sKnockFreq })
            s->snap (s->target);
    }

    void updateControl()
    {
        setTargets();

        // gains: remember start of chunk for per-sample ramps
        inA  = sIn.cur;      inB  = sIn.step();
        outA = sOut.cur;     outB = sOut.step();
        mixA = sGlueMix.cur; mixB = sGlueMix.step();
        mkA  = sMakeup.cur;  mkB  = sMakeup.step();
        ceil = sCeil.step();

        // Knock
        const double k  = sKnock.step();
        const double kf = sKnockFreq.step();
        knockBump.design (FilterType::Bell, fs, kf, 1.1, k * 5.0);
        mudCut.design (FilterType::Bell, fs, 320.0, 0.9, -k * 2.5);
        punchAmt    = sPunch.step();
        snapEq.design (FilterType::Bell, fs, 2500.0, 0.9, sSnap.step() * 4.0);

        // Tape
        tapeA     = sTape.cur;
        tapeAmt   = sTape.step();
        tapeDrive = dbToGain (tapeAmt * 12.0) / tapeHeadroom;   // saturation starts ~+6 dBFS at low drive
        headBump.design (FilterType::LowShelf, fs, 90.0, 0.71, tapeAmt * 1.2);
        tapeHf.design (FilterType::HighShelf, fs, std::min (12000.0, fs * 0.45), 0.71, -tapeAmt * 1.5);

        // Glue
        scHp.design (FilterType::LowCut, fs, p.glueScHpf, 0.71, 0);
        glueAtt  = std::exp (-1.0 / (std::max (0.0001, p.glueAttack * 0.001) * fs));
        glueRel  = std::exp (-1.0 / (std::max (0.01,   p.glueRelease * 0.001) * fs));

        // Dust
        dustA = sDust.cur;
        const double d  = sDust.step();
        const double lp = std::min (20000.0 * std::pow (6500.0 / 20000.0, d), fs * 0.45);
        dustAmt = d;
        dustLp1.design (FilterType::HighCut, fs, lp, 0.54, 0);
        dustLp2.design (FilterType::HighCut, fs, lp, 1.31, 0);
        vinylAmt  = sVinyl.step();

        // Mono bass (LR4 high-pass on the side signal)
        monoHp1.design (FilterType::LowCut, fs, p.monoFreq, 0.7071, 0);
        monoHp2.c = monoHp1.c;
    }

    //--------------------------------------------------------------------------
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

    // ADAA (first order) hard clip at +/-1
    static inline double clipF (double u)  { return std::abs (u) <= 1.0 ? 0.5 * u * u : std::abs (u) - 0.5; }
    inline double hardClip (double u, int ch)
    {
        const double u1 = clipX1[ch];
        clipX1[ch] = u;
        const double du = u - u1;
        if (std::abs (du) < 1.0e-6)
            return std::clamp (0.5 * (u + u1), -1.0, 1.0);
        return (clipF (u) - clipF (u1)) / du;
    }

    //--------------------------------------------------------------------------
    void processChunk (float* L, float* R, int n)
    {
        const bool stereo = R != nullptr;
        const double inv = 1.0 / n;

        for (int i = 0; i < n; ++i)
        {
            const double t = (i + 1) * inv;
            const double gIn  = inA  + (inB  - inA)  * t;
            const double gOut = outA + (outB - outA) * t;
            const double mix  = mixA + (mixB - mixA) * t;
            const double mk   = mkA  + (mkB  - mkA)  * t;

            double s[2] = { (double) L[i], stereo ? (double) R[i] : 0.0 };
            const int nc = stereo ? 2 : 1;

            // ---- Input + DC/sub cut ------------------------------------------------
            for (int c = 0; c < nc; ++c)
                s[c] = dcCut.process (s[c] * gIn, c);

            // ---- Knock: tone ---------------------------------------------------------
            for (int c = 0; c < nc; ++c)
            {
                s[c] = knockBump.process (s[c], c);
                s[c] = mudCut.process (s[c], c);
                s[c] = snapEq.process (s[c], c);
            }

            // ---- Knock: transient punch (detector on drum band, linked) ---------------
            {
                double det = stereo ? 0.5 * (s[0] + s[1]) : s[0];
                det = detLp.process (detHp.process (det, 0), 0);
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

                for (int c = 0; c < nc; ++c)
                    s[c] *= tGain;
            }

            // ---- Tape -----------------------------------------------------------------
            // filters always run (flat at 0); saturation blends in over the first 5% of the knob
            for (int c = 0; c < nc; ++c)
            {
                double v = headBump.process (s[c], c);
                const double tNow = tapeA + (tapeAmt - tapeA) * t;
                if (tNow > 0.0)
                {
                    const double wetAmt = std::min (1.0, tNow * 20.0);
                    v += wetAmt * (tapeSat (v * tapeDrive, c) / tapeDrive - v);   // unity gain at low levels
                }
                s[c] = tapeHf.process (v, c);
            }

            // ---- Glue (stereo-linked, parallel mix) ------------------------------------
            {
                double scL = scHp.process (s[0], 0);
                double scR = stereo ? scHp.process (s[1], 1) : scL;
                const double peak = std::max (std::abs (scL), std::abs (scR));
                const double xdB  = gainToDb (peak);

                const double T = p.glueThresh, Rt = p.glueRatio, W = 6.0;
                const double over = xdB - T;
                double g = 0;
                if (2 * over <= -W)               g = 0;
                else if (2 * std::abs (over) < W) g = (1.0 / Rt - 1.0) * (over + W / 2) * (over + W / 2) / (2 * W);
                else                              g = (1.0 / Rt - 1.0) * over;

                glueEnvDb = g < glueEnvDb ? glueAtt * glueEnvDb + (1 - glueAtt) * g
                                          : glueRel * glueEnvDb + (1 - glueRel) * g;
                grDb = glueEnvDb;

                const double comp = dbToGain (glueEnvDb) * mk;
                for (int c = 0; c < nc; ++c)
                    s[c] = s[c] * (1.0 - mix) + s[c] * comp * mix;
            }

            // ---- Dust: top roll-off + vinyl -------------------------------------------
            // filter always runs; blended in over the first 5% so switching it on never clicks
            {
                const double wetAmt = std::min (1.0, (dustA + (dustAmt - dustA) * t) * 20.0);
                for (int c = 0; c < nc; ++c)
                {
                    const double wet = dustLp2.process (dustLp1.process (s[c], c), c);
                    s[c] += wetAmt * (wet - s[c]);
                }
            }

            if (vinylAmt > 0.001)
            {
                const double density = 2.0 + vinylAmt * 28.0;          // clicks per second
                const double crackLevel = 0.06 * vinylAmt;              // ~ -24 dBFS peak at max
                const double hissLevel  = 0.0012 * vinylAmt;            // ~ -58 dBFS
                const float shared = rng.bi();

                for (int c = 0; c < nc; ++c)
                {
                    if (rng.uni() < density / fs)
                    {
                        const float u = rng.uni();
                        crack[c] += (rng.uni() < 0.5f ? -1.0 : 1.0) * u * u * u * crackLevel;
                    }
                    crack[c] *= crackDecay;
                    const double cr = crackHp.process (crack[c], c);
                    const double hs = hissLp.process ((0.5 * shared + 0.5 * rng.bi()) * hissLevel, c);
                    s[c] += cr + hs;
                }
            }

            // ---- Mono bass -----------------------------------------------------------
            if (stereo)
            {
                // Mono bass fades in/out over 5 ms when switched
                const double target = p.monoBass ? 1.0 : 0.0;
                if (monoFade != target)
                    monoFade = target > monoFade ? std::min (1.0, monoFade + fadeStep) : std::max (0.0, monoFade - fadeStep);
                if (monoFade > 0.0)
                {
                    const double mid  = 0.5 * (s[0] + s[1]);
                    const double side = 0.5 * (s[0] - s[1]);
                    const double hp   = monoHp2.process (monoHp1.process (side, 0), 0);
                    const double sd   = side + monoFade * monoFade * (3.0 - 2.0 * monoFade) * (hp - side);
                    s[0] = mid + sd;
                    s[1] = mid - sd;
                }
                else
                {
                    monoHp1.clearState(); monoHp2.clearState();
                }
            }

            // ---- Output + clipper ------------------------------------------------------
            for (int c = 0; c < nc; ++c)
            {
                double v = s[c] * gOut;
                if (p.clipOn)
                    v = hardClip (v / ceil, c) * ceil;
                s[c] = v;
            }

            L[i] = (float) s[0];
            if (stereo) R[i] = (float) s[1];
        }
    }

    //--------------------------------------------------------------------------
    double fs = 44100.0;
    int    nch = 2;
    bool   prepared = false, firstBlock = true;
    Params p = defaultParams();

    Smoothed sIn, sOut, sGlueMix, sMakeup, sKnock, sKnockFreq, sPunch, sSnap, sTape, sDust, sVinyl, sCeil;

    double inA = 1, inB = 1, outA = 1, outB = 1, mixA = 0, mixB = 0, mkA = 1, mkB = 1, ceil = 1;

    Biquad dcCut, knockBump, mudCut, snapEq, detHp, detLp;
    double punchAmt = 0, fastEnv = 0, slowEnv = 0, tGain = 1, punchDbMeter = 0;
    double fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0, tgSmooth = 0;

    Biquad headBump, tapeHf;
    static constexpr double tapeHeadroom = 2.0;
    double tapeA = 0, tapeAmt = 0, tapeDrive = 1, satX1[2] { 0, 0 }, satF1[2] { 0, 0 };

    Biquad scHp;
    double glueAtt = 0, glueRel = 0, glueEnvDb = 0, grDb = 0;

    Biquad dustLp1, dustLp2, crackHp, hissLp;
    double dustA = 0, dustAmt = 0;
    double vinylAmt = 0, crack[2] { 0, 0 }, crackDecay = 0;
    XorShift rng;

    Biquad monoHp1, monoHp2;
    double monoFade = 0, fadeStep = 0.01;
    double clipX1[2] { 0, 0 };
};

} // namespace knock
