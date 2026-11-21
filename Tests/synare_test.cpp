// synare_test.cpp
// Checks the Synare 3 model against the repair manual's figures and the panel's
// behaviour, then renders demo WAVs.
//
//   synare_test [render dir]

#include "../Source/DSP/Synare.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

using namespace tbsynare;

static const int kSr = 44100;
static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static bool allFinite (const std::vector<float>& x, double limit)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (! (x[i] == x[i]) || std::fabs (x[i]) > limit) return false;
    return true;
}

static double rmsRange (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return to > from ? std::sqrt (s / (to - from)) : 0.0;
}

static double peakRange (const std::vector<float>& x, size_t from, size_t to)
{
    double p = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i) p = std::max (p, (double) std::fabs (x[i]));
    return p;
}

// Frequency from upward zero crossings (after a light DC removal) over [from, to)
static double zcFreq (const std::vector<float>& x, size_t from, size_t to)
{
    to = std::min (to, x.size());
    double mean = 0.0;
    for (size_t i = from; i < to; ++i) mean += x[i];
    mean /= (double) (to - from);
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = from + 1; i < to; ++i)
    {
        const double a = x[i - 1] - mean, b = x[i] - mean;
        if (a < 0.0 && b >= 0.0)
        {
            const double t = (double) (i - 1) + a / (a - b);
            if (first < 0.0) first = t; else ++count;
            last = t;
        }
    }
    return (count > 0) ? count * (double) kSr / (last - first) : 0.0;
}

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const unsigned dataBytes = (unsigned) x.size() * 2, riff = 36 + dataBytes;
    const unsigned short fmt = 1, ch = 1, bits = 16, align = 2;
    const unsigned rate = (unsigned) sr, bps = rate * 2, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f); std::fwrite (&align, 2, 1, f);
    std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i];
        v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
        const short s = (short) std::floor (v * 32767.0 + 0.5);
        std::fwrite (&s, 2, 1, f);
    }
    std::fclose (f);
}

struct Patch
{
    double o1, o2, ft, fo2, sw, res, fd, ad, vol;
    int s1, s2;
};

static void apply (Synare& s, const Patch& p)
{
    s.setOsc1Tune (p.o1); s.setOsc1Select (p.s1);
    s.setOsc2Tune (p.o2); s.setOsc2Select (p.s2);
    s.setFilterTune (p.ft); s.setFilterOsc2 (p.fo2); s.setSweep (p.sw);
    s.setResonance (p.res); s.setFilterDecay (p.fd); s.setAmpDecay (p.ad); s.setVolume (p.vol);
}

// Render: hits at the given times (seconds) with velocities, total length sec
static std::vector<float> render (Synare& s, double sec, const double* hits, const double* vels, int nh)
{
    std::vector<float> out ((size_t) (sec * kSr));
    size_t pos = 0;
    int h = 0;
    while (pos < out.size())
    {
        size_t next = out.size();
        if (h < nh) next = std::min (next, (size_t) (hits[h] * kSr));
        if (next <= pos && h < nh) { s.strike (vels[h]); ++h; continue; }
        const size_t n = std::min ((size_t) 256, next - pos);
        s.process (&out[pos], (int) n);
        pos += n;
    }
    return out;
}

static std::vector<float> oneHit (const Patch& p, double sec, double vel = 1.0, double at = 0.05)
{
    Synare s; apply (s, p); s.prepare (kSr);
    return render (s, sec, &at, &vel, 1);
}

int main (int argc, char** argv)
{
    const std::string renderDir = argc > 1 ? argv[1] : "";
    std::printf ("TB Synare 3 DSP tests\n");

    // A tom: filter self-oscillating, swept down
    const Patch tom    = { 0.5, 0.5, 0.25, 0.0, 0.80, 1.00, 0.55, 0.55, 0.85, Synare::kOff, Synare::kOff };
    const Patch snare  = { 0.5, 0.5, 0.85, 0.0, 0.30, 0.20, 0.30, 0.35, 0.85, Synare::kMid, Synare::kOff };
    const Patch osc1p  = { 0.5, 0.5, 1.00, 0.0, 0.00, 0.00, 0.50, 0.95, 0.85, Synare::kOn,  Synare::kOff };

    std::printf ("Oscillators (repair manual: 40-1500 Hz)\n");
    {
        const double lo = Synare::oscHz (0.0, false), hi = Synare::oscHz (1.0, false);
        std::printf ("    TUNE range %.1f - %.1f Hz, SLO ratio %.1f\n", lo, hi, hi / Synare::oscHz (1.0, true));
        check (lo > 25.0 && lo < 45.0, "Osc TUNE fully anticlockwise near 40 Hz");
        check (hi > 1300.0 && hi < 1700.0, "Osc TUNE fully clockwise near 1500 Hz");
        const double r = Synare::oscHz (0.5, false) / Synare::oscHz (0.5, true);
        check (r > 18.0 && r < 25.0, "SLO divides the pitch by about 20");
        bool mono = true;
        for (int i = 1; i <= 20; ++i) if (Synare::oscHz (i / 20.0, false) <= Synare::oscHz ((i - 1) / 20.0, false)) mono = false;
        check (mono, "Osc TUNE raises the pitch clockwise");

        // Measured: Osc 1 square through an open filter, long amplifier decay
        for (int t = 0; t < 3; ++t)
        {
            Patch p = osc1p; p.o1 = 0.25 + 0.25 * t;
            std::vector<float> o = oneHit (p, 0.6);
            const double f = zcFreq (o, (size_t) (0.15 * kSr), (size_t) (0.55 * kSr));
            const double want = Synare::oscHz (p.o1, false);
            char msg[128]; std::snprintf (msg, sizeof (msg), "Osc 1 at TUNE %.2f sounds at %.1f Hz (expect %.1f)", p.o1, f, want);
            check (std::fabs (f / want - 1.0) < 0.02, msg);
        }
        // Osc 2 square selected: the same timing
        {
            Patch p = osc1p; p.s1 = Synare::kOff; p.s2 = Synare::kOn; p.o2 = 0.6;
            std::vector<float> o = oneHit (p, 0.6);
            const double f = zcFreq (o, (size_t) (0.15 * kSr), (size_t) (0.55 * kSr));
            const double want = Synare::oscHz (0.6, false);
            check (std::fabs (f / want - 1.0) < 0.02, "Osc 2 at OSC2 sounds at its TUNE pitch");
        }
    }

    std::printf ("Filter (repair manual: TUNE 30 Hz - 4 kHz)\n");
    {
        const double lo = Synare::filterHz (0.0), hi = Synare::filterHz (1.0);
        std::printf ("    filter TUNE range %.1f Hz - %.0f Hz\n", lo, hi);
        check (lo > 20.0 && lo < 45.0, "filter TUNE bottom near 30 Hz");
        check (hi > 3200.0 && hi < 5000.0, "filter TUNE top near 4 kHz");
        bool mono = true;
        for (int i = 1; i <= 20; ++i) if (Synare::filterHz (i / 20.0) <= Synare::filterHz ((i - 1) / 20.0)) mono = false;
        check (mono, "filter TUNE rises clockwise");

        // Low-pass: Osc 1 at 300 Hz through filter at TUNE low vs high
        Patch p = osc1p; p.o1 = 0.6; p.ft = 1.0;
        const double open = rmsRange (oneHit (p, 0.5), (size_t) (0.1 * kSr), (size_t) (0.45 * kSr));
        p.ft = 0.05;
        const double shut = rmsRange (oneHit (p, 0.5), (size_t) (0.1 * kSr), (size_t) (0.45 * kSr));
        std::printf ("    open %.4f, nearly shut %.5f\n", open, shut);
        check (shut < open * 0.1, "closing the filter cuts Osc 1 by more than 20 dB");

        // Self-oscillation at full RES with both selectors off: a tone near the cutoff
        Patch q = tom; q.sw = 0.0; q.fo2 = 0.0; q.ft = 0.45; q.ad = 1.0;
        std::vector<float> o = oneHit (q, 1.5);
        const double f = zcFreq (o, (size_t) (0.6 * kSr), (size_t) (1.4 * kSr));
        const double fc = Synare::filterHz (0.45);
        std::printf ("    RES full: oscillates at %.1f Hz, cutoff %.1f Hz, level %.3f\n", f, fc, rmsRange (o, (size_t) (0.6 * kSr), (size_t) (0.8 * kSr)));
        check (rmsRange (o, (size_t) (0.6 * kSr), (size_t) (0.8 * kSr)) > 0.01, "RES fully up makes the filter oscillate (sine drum)");
        check (f > fc * 0.6 && f < fc * 1.4, "the oscillation is near the filter cutoff");
        // ... and stays clean with RES low
        q.res = 0.3;
        std::vector<float> o2 = oneHit (q, 1.0);
        check (rmsRange (o2, (size_t) (0.6 * kSr), (size_t) (0.9 * kSr)) < 1e-3, "RES low with both sources off is silent");
        // Higher TUNE, higher pitch
        q.res = 1.0; q.ft = 0.6;
        const double f2 = zcFreq (oneHit (q, 1.5), (size_t) (0.6 * kSr), (size_t) (1.4 * kSr));
        check (f2 > f * 1.1, "filter TUNE raises the oscillation pitch");
    }

    std::printf ("Strike, envelopes, VCA\n");
    {
        std::vector<float> o = oneHit (tom, 2.0, 1.0, 0.25);
        const double before = peakRange (o, 0, (size_t) (0.24 * kSr));
        const double hit = peakRange (o, (size_t) (0.25 * kSr), (size_t) (0.45 * kSr));
        const double late = peakRange (o, (size_t) (1.7 * kSr), (size_t) (2.0 * kSr));
        std::printf ("    before %.5f, hit peak %.3f, after 1.5 s %.4f\n", before, hit, late);
        check (before < 1e-3, "silent until the pad is hit");
        check (hit > 0.1 && hit < 1.0, "a hard hit peaks between -20 and 0 dBFS at VOL 8.5");
        check (late < hit * 0.1, "the tom decays away");

        // Amplifier DECAY lengthens the sound
        Patch a = tom; a.ad = 0.2;
        const double shortTail = rmsRange (oneHit (a, 1.0), (size_t) (0.3 * kSr), (size_t) (0.6 * kSr));
        a.ad = 0.9;
        const double longTail = rmsRange (oneHit (a, 1.0), (size_t) (0.3 * kSr), (size_t) (0.6 * kSr));
        check (longTail > shortTail * 3.0, "AMPLIFIER DECAY clockwise holds the sound longer");

        // SWEEP: the filter jumps up on the hit and glides down
        Synare s; apply (s, tom); s.prepare (kSr);
        std::vector<float> buf (512);
        s.process (&buf[0], 512);
        const double rest = s.debugFilterHz();
        s.strike (1.0);
        s.process (&buf[0], 256);
        const double up = s.debugFilterHz();
        for (int i = 0; i < 400; ++i) s.process (&buf[0], 512);
        const double after = s.debugFilterHz();
        std::printf ("    sweep: rest %.0f Hz, 6 ms after the hit %.0f Hz, 4.6 s later %.0f Hz\n", rest, up, after);
        check (up > rest * 1.5, "SWEEP lifts the filter on the hit");
        check (after < up * 0.7, "and it glides back down");

        // Velocity: a soft hit is quieter; with velocity off it is not
        const double hard = peakRange (oneHit (tom, 0.6, 1.0), 0, (size_t) (0.6 * kSr));
        const double soft = peakRange (oneHit (tom, 0.6, 0.3), 0, (size_t) (0.6 * kSr));
        Synare s2; apply (s2, tom); s2.setVelocitySense (false); s2.prepare (kSr);
        const double at = 0.05, v = 0.3;
        const double softOff = peakRange (render (s2, 0.6, &at, &v, 1), 0, (size_t) (0.6 * kSr));
        std::printf ("    hard %.3f, soft %.3f, soft with velocity off %.3f\n", hard, soft, softOff);
        check (soft < hard * 0.8, "a soft hit (velocity 38) is quieter than a hard one");
        check (std::fabs (softOff / hard - 1.0) < 0.05, "with VELOCITY off every hit is full strength");

        // Snare: noise through the filter
        std::vector<float> sn = oneHit (snare, 0.6);
        check (peakRange (sn, 0, (size_t) (0.6 * kSr)) > 0.05, "noise snare sounds");

        // Retriggering fast: no blow-up
        double hits[40], vels[40];
        for (int i = 0; i < 40; ++i) { hits[i] = 0.05 + i * 0.03; vels[i] = (i % 5) / 4.0; }
        Synare s3; apply (s3, tom); s3.prepare (kSr);
        check (allFinite (render (s3, 2.0, hits, vels, 40), 2.0), "fast retriggers stay finite and bounded");
    }

    std::printf ("Stability: random patches and hits\n");
    {
        std::srand (12345);
        bool ok = true;
        double worst = 0.0;
        for (int t = 0; t < 120 && ok; ++t)
        {
            Patch p;
            p.o1 = std::rand() / (double) RAND_MAX; p.o2 = std::rand() / (double) RAND_MAX;
            p.ft = std::rand() / (double) RAND_MAX; p.fo2 = std::rand() / (double) RAND_MAX;
            p.sw = std::rand() / (double) RAND_MAX; p.res = std::rand() / (double) RAND_MAX;
            p.fd = std::rand() / (double) RAND_MAX; p.ad = std::rand() / (double) RAND_MAX;
            p.vol = std::rand() / (double) RAND_MAX;
            p.s1 = std::rand() % 3; p.s2 = std::rand() % 3;
            if (t < 8) { p.ft = (t & 1) ? 1.0 : 0.0; p.res = (t & 2) ? 1.0 : 0.0; p.sw = (t & 4) ? 1.0 : 0.0; p.vol = 1.0; }
            Synare s; apply (s, p); s.prepare (t % 3 == 0 ? 96000 : kSr);
            double hits[3] = { 0.02, 0.1, 0.35 }, vels[3] = { 1.0, 0.5, 1.0 };
            std::vector<float> o = render (s, 0.6, hits, vels, 3);
            if (! allFinite (o, 4.0)) ok = false;
            worst = std::max (worst, peakRange (o, 0, o.size()));
        }
        std::printf ("    loudest peak %.3f\n", worst);
        check (ok, "120 random patches: finite, below 4.0");
        check (worst < 1.5, "nothing much louder than full scale");
    }

    std::printf ("Automation while playing, CPU\n");
    {
        Synare s; apply (s, tom); s.prepare (kSr);
        std::vector<float> o ((size_t) (4 * kSr));
        const std::clock_t c0 = std::clock();
        for (size_t pos = 0; pos < o.size(); pos += 64)
        {
            const double ph = (double) pos / o.size();
            if ((pos / 64) % 300 == 0) s.strike (0.8);
            s.setOsc1Tune (ph); s.setOsc2Tune (1.0 - ph); s.setFilterTune (0.5 + 0.5 * std::sin (ph * 20.0));
            s.setFilterOsc2 (ph); s.setSweep (1.0 - ph); s.setResonance (0.5 + 0.5 * std::sin (ph * 7.0));
            s.setFilterDecay (ph); s.setAmpDecay (1.0 - ph); s.setVolume (0.9);
            s.setOsc1Select ((int) (ph * 9) % 3); s.setOsc2Select ((int) (ph * 7) % 3);
            s.process (&o[pos], (int) std::min ((size_t) 64, o.size() - pos));
        }
        const double cpu = (double) (std::clock() - c0) / CLOCKS_PER_SEC / 4.0;
        std::printf ("    %.1f ms of CPU per second of audio\n", cpu * 1000.0);
        check (allFinite (o, 2.0), "every control automated while playing: finite");
        check (cpu < 0.25, "under 25 % of one core");
    }

    if (! renderDir.empty())
    {
        std::printf ("Demo renders in %s\n", renderDir.c_str());
        struct Demo { const char* name; Patch p; };
        const Demo demos[] = {
            { "tom_dive",       tom },
            { "noise_snare",    snare },
            { "osc1_blip",      { 0.75, 0.5, 0.55, 0.0, 0.6, 0.6, 0.35, 0.3, 0.85, Synare::kOn,  Synare::kOff } },
            { "metal_two_oscs", { 0.80, 0.62, 0.8, 0.2, 0.3, 0.5, 0.4, 0.45, 0.85, Synare::kOn, Synare::kOn } },
            { "slo_wobble_tom", { 0.5, 0.75, 0.35, 0.6, 0.4, 1.0, 0.6, 0.8, 0.85, Synare::kOff, Synare::kMid } },
            { "laser_zap",      { 0.5, 0.5, 0.15, 0.0, 1.0, 1.0, 0.45, 0.6, 0.85, Synare::kOff, Synare::kOff } },
            { "clap_noise",     { 0.5, 0.5, 0.7, 0.0, 0.0, 0.5, 0.2, 0.2, 0.85, Synare::kMid, Synare::kOff } },
        };
        const double hits[8] = { 0.1, 0.6, 1.1, 1.35, 1.6, 2.1, 2.35, 2.6 };
        const double vels[8] = { 1.0, 0.7, 1.0, 0.5, 0.9, 1.0, 0.6, 1.0 };
        for (size_t d = 0; d < sizeof (demos) / sizeof (demos[0]); ++d)
        {
            Synare s; apply (s, demos[d].p); s.prepare (kSr);
            std::vector<float> o = render (s, 4.0, hits, vels, 8);
            writeWav (renderDir + "/" + demos[d].name + ".wav", o, kSr);
            std::printf ("    %s.wav  peak %.3f\n", demos[d].name, peakRange (o, 0, o.size()));
        }
    }

    std::printf (failures ? "\n%d FAILED\n" : "\nAll DSP tests passed\n", failures);
    return failures ? 1 : 0;
}
