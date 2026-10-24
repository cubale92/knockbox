// Click test: flips every on/off switch and band type while a steady tone plays,
// and checks the output has no sudden jumps (clicks/pops).
// A click shows up as a spike in the second difference of the signal.

#include "../Source/dsp/TrackEngine.h"

#include <cstdio>
#include <functional>
#include <vector>

using namespace knock;

static int failures = 0;
static void check (bool ok, const char* what, double ratio)
{
    std::printf ("  [%s] %-46s spike ratio %.2f\n", ok ? "ok" : "FAIL", what, ratio);
    if (! ok) ++failures;
}

// Runs `seconds` of a 220 Hz tone at -6 dBFS; at the halfway block calls `flip`.
// Returns: worst second-difference in the 60 ms right after the flip, divided by the worst of
// the steady state before the flip and the new steady state well after it. A changed tone
// (more harmonics, more gain) raises both, a click only raises the transient window.
template <typename Engine, typename P>
static double spikeRatio (double sr, P params, std::function<void (P&)> flip)
{
    Engine e;
    e.prepare (sr, 2);
    e.setParams (params);

    const int block = 256, total = (int) sr;              // 1 s
    std::vector<float> L (total), R (total);
    for (int i = 0; i < total; ++i)
        L[i] = R[i] = (float) (0.5 * std::sin (2 * kPi * 220.0 * i / sr));

    const int flipAt = (total / 2 / block) * block;
    for (int i = 0; i < total; i += block)
    {
        if (i == flipAt) { flip (params); e.setParams (params); }
        float* ch[2] = { L.data() + i, R.data() + i };
        e.process (ch, 2, std::min (block, total - i));
    }

    auto worstD2 = [&] (int a, int b)
    {
        double w = 0;
        for (int i = a + 2; i < b; ++i)
            w = std::max (w, (double) std::abs (L[i] - 2 * L[i - 1] + L[i - 2]));
        return w;
    };
    const double before = worstD2 (flipAt - (int) (0.2 * sr), flipAt);
    const double trans  = worstD2 (flipAt, flipAt + (int) (0.06 * sr));
    const double steady = worstD2 (flipAt + (int) (0.25 * sr), flipAt + (int) (0.45 * sr));
    return trans / std::max (std::max (before, steady), 1e-9);
}

int main()
{
    constexpr double limit = 1.5;     // a hard reset of a filter gives ratios in the tens to hundreds

    for (double sr : { 44100.0, 48000.0 })
    {
        std::printf ("== %.0f Hz ==\n", sr);

        // ---- Knockbox Track ----
        using TE = TrackEngine; using TP = TrackParams;
        TP base;
        base.band[1] = { true, (int) FilterType::Bell, 220, 9, 1.0f };   // a band that really shapes the tone

        double r;
        r = spikeRatio<TE, TP> (sr, base, [] (TP& p) { p.band[1].on = false; });
        check (r < limit, "track: band switched off", r);

        TP off = base; off.band[1].on = false;
        r = spikeRatio<TE, TP> (sr, off, [] (TP& p) { p.band[1].on = true; });
        check (r < limit, "track: band switched on", r);

        r = spikeRatio<TE, TP> (sr, base, [] (TP& p) { p.band[1].type = (int) FilterType::LowShelf; });
        check (r < limit, "track: band type bell -> low shelf", r);

        r = spikeRatio<TE, TP> (sr, base, [] (TP& p) { p.band[1].type = (int) FilterType::HighCut; });
        check (r < limit, "track: band type bell -> high cut", r);

        r = spikeRatio<TE, TP> (sr, base, [] (TP& p) { p.tape = 0.02f; });
        check (r < limit, "track: tape 0 -> 2%", r);

        r = spikeRatio<TE, TP> (sr, base, [] (TP& p) { p.knock = 1.0f; p.punch = 1.0f; p.snap = 1.0f; });
        check (r < limit, "track: knock/punch/snap jump to max", r);

        // ---- Knockbox master ----
        using ME = KnockEngine; using MP = Params;
        MP m;                                     // defaults
        m.vinyl = 0;

        r = spikeRatio<ME, MP> (sr, m, [] (MP& p) { p.monoBass = true; });
        check (r < limit, "master: mono bass on", r);

        MP z = m; z.tape = 0; z.dust = 0;
        r = spikeRatio<ME, MP> (sr, z, [] (MP& p) { p.tape = 0.02f; p.dust = 0.02f; });
        check (r < limit, "master: tape and dust 0 -> 2%", r);

        // a compressor clamping down on a tone changes its envelope on purpose (that's its attack),
        // so this one gets a looser limit; it still catches real clicks, which measure 10x and more
        r = spikeRatio<ME, MP> (sr, m, [] (MP& p) { p.glueMix = 1.0f; p.glueThresh = -30; p.glueRatio = 10; });
        check (r < 2.0, "master: glue slammed", r);

        r = spikeRatio<ME, MP> (sr, m, [] (MP& p) { p.knock = 1.0f; p.knockFreq = 150; p.inputDb = 6; });
        check (r < limit, "master: knock + input jump", r);
    }

    std::printf (failures ? "\n%d FAILURE(S)\n" : "\nALL CLICK TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
