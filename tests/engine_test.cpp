// Offline test for KnockEngine: renders a synthetic drum loop at several sample rates,
// checks for NaN/Inf, ceiling compliance, transparency at neutral settings, and
// writes WAV files for listening.

#include "../Source/dsp/KnockEngine.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace knock;

static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) L.size(), dataBytes = n * 2 * 2;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 (sr); w32 (sr * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float v : { L[i], R[i] })
        {
            int s = (int) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f);
            w16 ((uint16_t) (int16_t) s);
        }
    std::fclose (f);
}

// 90 BPM, 2 bars: kick, snare, hats, a bass note and a pad-ish chord
static void makeLoop (int sr, std::vector<float>& L, std::vector<float>& R)
{
    const double bpm = 90.0, beat = 60.0 / bpm;
    const int n = (int) (sr * beat * 8);
    L.assign (n, 0.0f); R.assign (n, 0.0f);
    XorShift rng;

    auto addKick = [&] (double t0)
    {
        const int s0 = (int) (t0 * sr);
        double ph = 0;
        for (int i = 0; i < (int) (0.45 * sr) && s0 + i < n; ++i)
        {
            const double t = (double) i / sr;
            const double f = 50.0 + 90.0 * std::exp (-t * 30.0);
            ph += 2 * kPi * f / sr;
            const double v = std::sin (ph) * std::exp (-t * 7.0) * 0.55;
            L[s0 + i] += (float) v; R[s0 + i] += (float) v;
        }
    };
    auto addSnare = [&] (double t0)
    {
        const int s0 = (int) (t0 * sr);
        double ph = 0, lp = 0;
        for (int i = 0; i < (int) (0.3 * sr) && s0 + i < n; ++i)
        {
            const double t = (double) i / sr;
            ph += 2 * kPi * 190.0 / sr;
            lp += 0.35 * (rng.bi() - lp);
            const double v = (std::sin (ph) * std::exp (-t * 25.0) * 0.3 + lp * std::exp (-t * 14.0) * 0.45);
            L[s0 + i] += (float) v; R[s0 + i] += (float) (v * 0.9);
        }
    };
    auto addHat = [&] (double t0, double pan)
    {
        const int s0 = (int) (t0 * sr);
        double prev = 0;
        for (int i = 0; i < (int) (0.06 * sr) && s0 + i < n; ++i)
        {
            const double t = (double) i / sr;
            const double w = rng.bi();
            const double v = (w - prev) * std::exp (-t * 60.0) * 0.12;
            prev = w;
            L[s0 + i] += (float) (v * (1 - pan)); R[s0 + i] += (float) (v * (1 + pan));
        }
    };

    for (int bar = 0; bar < 2; ++bar)
    {
        const double b0 = bar * 4 * beat;
        addKick (b0); addKick (b0 + 1.5 * beat); addKick (b0 + 2.75 * beat);
        addSnare (b0 + beat); addSnare (b0 + 3 * beat);
        for (int e = 0; e < 8; ++e) addHat (b0 + e * 0.5 * beat + (e % 2 ? 0.06 * beat : 0.0), e % 2 ? 0.3 : -0.3);
    }

    // bass + chord (stereo-ish)
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double bass = std::sin (2 * kPi * 55.0 * t) * 0.15;
        const double pad  = (std::sin (2 * kPi * 220.0 * t) + std::sin (2 * kPi * 277.18 * t + 0.4)
                             + std::sin (2 * kPi * 329.63 * t + 1.1)) * 0.03;
        L[i] += (float) (bass + pad * 1.1);
        R[i] += (float) (bass + pad * 0.9);
    }
}

struct Stats { double peak = 0, rms = 0; bool bad = false; };

static Stats stats (const std::vector<float>& L, const std::vector<float>& R)
{
    Stats s; double acc = 0;
    for (size_t i = 0; i < L.size(); ++i)
        for (float v : { L[i], R[i] })
        {
            if (! std::isfinite (v)) s.bad = true;
            s.peak = std::max (s.peak, (double) std::abs (v));
            acc += (double) v * v;
        }
    s.rms = std::sqrt (acc / (2.0 * L.size()));
    return s;
}

static Stats render (Params p, int sr, int block, std::vector<float>& L, std::vector<float>& R, float* grOut = nullptr)
{
    makeLoop (sr, L, R);
    KnockEngine e;
    e.prepare (sr, 2);
    e.setParams (p);
    float minGr = 0;
    for (size_t i = 0; i < L.size(); i += block)
    {
        const int n = (int) std::min ((size_t) block, L.size() - i);
        float* ch[2] = { L.data() + i, R.data() + i };
        e.process (ch, 2, n);
        minGr = std::min (minGr, e.getGainReductionDb());
    }
    if (grOut) *grOut = minGr;
    return stats (L, R);
}

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what); if (! ok) ++failures; };

    for (int sr : { 44100, 48000, 96000 })
    {
        std::printf ("== %d Hz ==\n", sr);
        std::vector<float> dL, dR; makeLoop (sr, dL, dR);
        const Stats dry = stats (dL, dR);

        // 1) neutral settings must be (nearly) transparent
        Params neutral = defaultParams();
        neutral.knock = neutral.punch = neutral.snap = neutral.tape = 0;
        neutral.glueMix = 0; neutral.dust = 0; neutral.vinyl = 0; neutral.clipOn = false;
        std::vector<float> L, R;
        render (neutral, sr, 512, L, R);
        double errMax = 0;
        // compare after the 18 Hz DC cut settles: high-pass the dry signal the same way
        {
            Biquad hp; hp.c = makeCoeffs (FilterType::LowCut, sr, 18.0, 0.71, 0);
            for (size_t i = 0; i < dL.size(); ++i)
            {
                errMax = std::max (errMax, std::abs (hp.process (dL[i], 0) - L[i]));
                errMax = std::max (errMax, std::abs (hp.process (dR[i], 1) - R[i]));
            }
        }
        std::printf ("  neutral max error vs. DC-cut dry: %.2e\n", errMax);
        check (errMax < 1e-5, "neutral settings are transparent");

        // 2) default "boom bap" settings
        Params p = defaultParams();
        float gr = 0;
        Stats s = render (p, sr, 512, L, R, &gr);
        std::printf ("  dry peak %.2f dBFS rms %.2f | wet peak %.2f dBFS rms %.2f | max GR %.1f dB\n",
                     gainToDb (dry.peak), gainToDb (dry.rms), gainToDb (s.peak), gainToDb (s.rms), gr);
        check (! s.bad, "no NaN/Inf (defaults)");
        check (s.peak <= dbToGain (-0.3) + 1e-3, "output stays under -0.3 dBFS ceiling");
        check (gr < -0.5, "glue compressor is working");
        if (sr == 44100) writeWav (outDir + "/default_wet.wav", L, R, sr);
        if (sr == 44100) writeWav (outDir + "/dry.wav", dL, dR, sr);

        // 3) everything maxed, odd block sizes, hot input
        Params hot = defaultParams();
        hot.inputDb = 18; hot.knock = 1; hot.punch = 1; hot.snap = 1; hot.tape = 1;
        hot.glueThresh = -36; hot.glueRatio = 10; hot.glueAttack = 0.1f; hot.glueRelease = 30;
        hot.glueMakeup = 18; hot.glueMix = 1; hot.dust = 1; hot.vinyl = 1; hot.monoBass = true;
        s = render (hot, sr, 37, L, R);
        check (! s.bad, "no NaN/Inf (everything maxed, block 37)");
        check (s.peak <= dbToGain (-0.3) + 1e-3, "ceiling holds when slammed");

        // 4) block-size independence
        std::vector<float> L1, R1, L2, R2;
        render (p, sr, 1, L1, R1);
        render (p, sr, 2048, L2, R2);
        double bd = 0;
        for (size_t i = 0; i < L1.size(); ++i) bd = std::max (bd, (double) std::abs (L1[i] - L2[i]));
        std::printf ("  block 1 vs 2048 max diff: %.2e\n", bd);
        check (bd < 1e-5, "output independent of host block size");

        // 5) mono input
        {
            std::vector<float> mL, mR; makeLoop (sr, mL, mR);
            KnockEngine e; e.prepare (sr, 1); e.setParams (p);
            float* ch[1] = { mL.data() };
            e.process (ch, 1, (int) mL.size());
            bool ok = true; for (float v : mL) if (! std::isfinite (v)) ok = false;
            check (ok, "mono processing is finite");
        }

        // 6) silence in -> silence out (no vinyl), and no denormal blow-up
        {
            std::vector<float> zL (sr, 0.0f), zR (sr, 0.0f);
            KnockEngine e; e.prepare (sr, 2); e.setParams (p);
            float* ch[2] = { zL.data(), zR.data() };
            e.process (ch, 2, sr);
            double pk = 0; for (float v : zL) pk = std::max (pk, (double) std::abs (v));
            check (pk < 1e-9, "silence stays silent");
        }
    }

    // Punch must raise the drum crest factor (peak/RMS) at 44.1 kHz
    {
        Params a = defaultParams();
        a.knock = 0; a.snap = 0; a.tape = 0; a.glueMix = 0; a.dust = 0; a.clipOn = false;
        Params b = a; b.punch = 1.0f;
        std::vector<float> L, R;
        Stats sa = render (a, 44100, 512, L, R);
        Stats sb = render (b, 44100, 512, L, R);
        const double ca = gainToDb (sa.peak / sa.rms), cb = gainToDb (sb.peak / sb.rms);
        std::printf ("crest factor: punch 0 = %.2f dB, punch 1 = %.2f dB\n", ca, cb);
        check (cb > ca + 1.0, "punch makes drums hit harder (crest up > 1 dB)");
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
