// Offline tests for TrackEngine (Knockbox Track)

#include "../Source/dsp/TrackEngine.h"

#include <cstdio>
#include <vector>

using namespace knock;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (! ok) ++failures;
}

static TrackParams neutral()
{
    TrackParams p;
    p.band[0].on = false;              // default low cut at 30 Hz off for transparency tests
    return p;
}

// Steady-state gain in dB of the engine for a sine at f
static double sineGainDb (const TrackParams& p, double sr, double f)
{
    TrackEngine e; e.prepare (sr, 2); e.setParams (p);
    const int n = (int) sr;            // 1 s, measure the last half
    std::vector<float> L (n), R (n);
    for (int i = 0; i < n; ++i) L[i] = R[i] = (float) (0.1 * std::sin (2 * kPi * f * i / sr));
    float* ch[2] = { L.data(), R.data() };
    e.process (ch, 2, n);
    double acc = 0;
    for (int i = n / 2; i < n; ++i) acc += (double) L[i] * L[i];
    const double rms = std::sqrt (acc / (n / 2));
    return gainToDb (rms / (0.1 / std::sqrt (2.0)));
}

static void drumLoop (double sr, std::vector<float>& L, std::vector<float>& R)
{
    const int n = (int) (sr * 2.0);
    L.assign (n, 0); R.assign (n, 0);
    XorShift rng;
    for (int hit = 0; hit < 4; ++hit)
    {
        const int s0 = (int) (hit * 0.5 * sr);
        double ph = 0, lp = 0;
        for (int i = 0; i < (int) (0.35 * sr); ++i)
        {
            const double t = (double) i / sr;
            ph += 2 * kPi * (50.0 + 90.0 * std::exp (-t * 30.0)) / sr;
            lp += 0.3 * (rng.bi() - lp);
            const double v = std::sin (ph) * std::exp (-t * 7.0) * 0.5 + (hit % 2 ? lp * std::exp (-t * 15) * 0.4 : 0.0);
            L[s0 + i] += (float) v; R[s0 + i] += (float) (v * 0.9);
        }
    }
}

int main()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("== %.0f Hz ==\n", sr);

        // transparency
        {
            std::vector<float> L, R, dL, dR; drumLoop (sr, L, R); dL = L; dR = R;
            TrackEngine e; e.prepare (sr, 2); e.setParams (neutral());
            float* ch[2] = { L.data(), R.data() };
            e.process (ch, 2, (int) L.size());
            double err = 0;
            for (size_t i = 0; i < L.size(); ++i) err = std::max (err, (double) std::abs (L[i] - dL[i]));
            std::printf ("  neutral max error %.2e\n", err);
            check (err < 1e-5, "neutral settings are transparent");
        }

        // EQ accuracy
        {
            TrackParams p = neutral();
            p.band[2] = { true, (int) FilterType::Bell, 1000, 6, 1.0f };
            const double g = sineGainDb (p, sr, 1000);
            std::printf ("  bell +6 dB @ 1 kHz measures %.2f dB\n", g);
            check (std::abs (g - 6.0) < 0.1, "bell gain is accurate");

            p = neutral();
            p.band[0] = { true, (int) FilterType::LowCut, 100, 0, 0.71f };
            const double lc = sineGainDb (p, sr, 30);
            std::printf ("  low cut 100 Hz at 30 Hz: %.1f dB\n", lc);
            check (lc < -18.0, "low cut removes rumble");

            p = neutral();
            p.band[3] = { true, (int) FilterType::HighShelf, 5000, -6, 0.71f };
            const double hs = sineGainDb (p, sr, std::min (15000.0, sr * 0.4));
            check (std::abs (hs + 6.0) < 0.6, "high shelf -6 dB is accurate");

            // display maths agrees with the measured response
            p = neutral();
            p.band[1] = { true, (int) FilterType::Bell, 200, -4, 2.0f };
            p.band[2] = { true, (int) FilterType::LowShelf, 80, 3, 0.71f };
            const double meas = sineGainDb (p, sr, 200);
            const double disp = TrackEngine::eqMagnitudeDb (p, sr, 200);
            std::printf ("  combined @200 Hz measured %.2f, display %.2f\n", meas, disp);
            check (std::abs (meas - disp) < 0.1, "EQ display matches the audio");

            // off band does nothing
            p = neutral();
            p.band[1] = { false, (int) FilterType::Bell, 1000, 12, 1.0f };
            check (std::abs (sineGainDb (p, sr, 1000)) < 0.01, "switched-off band is bypassed");
        }

        // punch increases crest factor
        {
            auto crest = [&] (float punch)
            {
                std::vector<float> L, R; drumLoop (sr, L, R);
                TrackParams p = neutral(); p.punch = punch;
                TrackEngine e; e.prepare (sr, 2); e.setParams (p);
                float* ch[2] = { L.data(), R.data() };
                e.process (ch, 2, (int) L.size());
                double pk = 0, acc = 0;
                for (float v : L) { pk = std::max (pk, (double) std::abs (v)); acc += (double) v * v; }
                return gainToDb (pk / std::sqrt (acc / L.size()));
            };
            const double c0 = crest (0), c1 = crest (1);
            std::printf ("  crest: punch 0 = %.2f dB, punch 1 = %.2f dB\n", c0, c1);
            check (c1 > c0 + 1.0, "punch makes hits harder");
        }

        // everything maxed: finite, block-size independent
        {
            TrackParams p;
            p.inputDb = 18; p.knock = 1; p.punch = 1; p.snap = 1; p.tape = 1; p.outputDb = 18;
            p.band[1] = { true, (int) FilterType::Bell, 60, 18, 0.1f };
            p.band[2] = { true, (int) FilterType::Bell, 3000, 18, 10.0f };
            p.band[3] = { true, (int) FilterType::HighCut, 25000, 0, 10.0f };

            std::vector<float> A, AR, B, BR; drumLoop (sr, A, AR); B = A; BR = AR;
            TrackEngine e1, e2;
            e1.prepare (sr, 2); e1.setParams (p);
            e2.prepare (sr, 2); e2.setParams (p);
            for (size_t i = 0; i < A.size(); ++i) { float* ch[2] = { &A[i], &AR[i] }; e1.process (ch, 2, 1); }
            for (size_t i = 0; i < B.size(); i += 1000)
            {
                const int n = (int) std::min<size_t> (1000, B.size() - i);
                float* ch[2] = { &B[i], &BR[i] }; e2.process (ch, 2, n);
            }
            bool finite = true; double d = 0;
            for (size_t i = 0; i < A.size(); ++i)
            {
                finite &= std::isfinite (A[i]) && std::isfinite (AR[i]);
                d = std::max (d, (double) std::abs (A[i] - B[i]));
            }
            check (finite, "finite with everything maxed (incl. high cut above Nyquist)");
            check (d < 1e-4, "independent of block size");
        }

        // mono + silence
        {
            std::vector<float> L, R; drumLoop (sr, L, R);
            TrackParams p; p.knock = 0.5f; p.punch = 0.5f; p.tape = 0.5f;
            TrackEngine e; e.prepare (sr, 1); e.setParams (p);
            float* ch[1] = { L.data() };
            e.process (ch, 1, (int) L.size());
            bool ok = true; for (float v : L) ok &= std::isfinite (v);
            check (ok, "mono is finite");

            std::vector<float> z (48000, 0.0f), z2 (48000, 0.0f);
            TrackEngine s; s.prepare (sr, 2); s.setParams (p);
            float* zc[2] = { z.data(), z2.data() };
            s.process (zc, 2, 48000);
            double pk = 0; for (float v : z) pk = std::max (pk, (double) std::abs (v));
            check (pk < 1e-9, "silence stays silent");
        }
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL TRACK TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
