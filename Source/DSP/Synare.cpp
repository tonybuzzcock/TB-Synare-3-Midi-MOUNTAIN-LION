// Synare.cpp
// Star Instruments Synare 3, component-level behavioural model. See Synare.h.
//
// All voltages are relative to the unit's 6.2 V "imaginary ground" unless noted
// (the drawing rounds it to 6 V, and so does this model).
//
// Values the drawing does not fix are estimates, each in a named constant below
// and listed in the README.

#include "Synare.h"

#include <cmath>
#include <cstring>

namespace tbsynare
{

namespace
{
    const double kPi = 3.14159265358979323846;
    const double kVt = 0.02585;            // thermal voltage at 25 C

    // ---- Supplies
    const double kVdd = 12.0;              // regulated rail (absolute)
    const double kVgnd = 6.0;              // imaginary ground (absolute)

    // ---- 4093 oscillators (RCA CD4093 at 12 V). ESTIMATE: thresholds chosen so the
    // range matches the repair manual's 40-1500 Hz with 22K + 1M and 47n.
    const double kVpos = 6.95;             // positive-going threshold (absolute)
    const double kVneg = 5.0;              // negative-going threshold (absolute)
    const double kOscC = 47e-9;            // timing capacitor
    const double kOscCSlo = 47e-9 + 1e-6;  // SLO adds the 1u (repair manual: "a factor of 20")
    const double kOscR = 22e3;             // series resistor
    const double kTunePot = 1e6;           // 1M (A) TUNE pots
    const double kVbe = 0.65;              // follower base-emitter drop

    // ---- Audio mixer into the filter's first OTA (+ input, 220R to ground)
    const double kR220 = 220.0;
    const double kOsc1Path = 470e3 + 10e3; // 470K, switch, 100N, 10K
    const double kOsc2Path = 470e3 + 470e3;// 470K, switch, 470K
    const double kNoisePath = 10e3 + 10e3; // 10K, switch, 100N, 10K
    const double kCoup = 100e-9;           // the shared 100N coupling capacitor
    // Noise: ESTIMATE of the amplified junction noise (rms volts at the amplifier's
    // collector) and the amplifier's bandwidth; it clips on the rails when loud.
    const double kNoiseRms = 0.35;
    const double kNoiseLpHz = 20000.0;
    const double kNoiseFloor = 2e-6;       // the circuit's own hiss at the filter input (starts oscillation)

    // ---- Filter: 3080 OTA integrators. Each stage: 100K into 220R at both inputs.
    const double kR100 = 100e3;
    const double kR68 = 6.8e3;             // resonance feedback resistor
    // ESTIMATE: the drawing leaves the integrating capacitors unmarked; chosen with
    // the bottom-end resistor so TUNE spans the manual's 30 Hz - 4 kHz.
    const double kFiltC = 1.5e-9;
    const double kRail = 5.0;              // OTA output / follower swing about ground
    const double kOtaMax = 2e-3;           // 3080 maximum bias current per OTA

    // ---- Filter CV mixer and current source
    const double kR220k = 220e3;
    const double kR68k = 68e3;
    const double kRsel = 2.7e6;            // factory-selected (manual: 2-3 meg; drawing: 1K8-3M)
    const double kIs = 1e-14;              // junction saturation current (current source + OTA bias diode)
    const double kBeta = 200.0;            // 2N3904 current gain
    const double kRemit = 220.0 + 220.0 / 4.0;   // 220R to the bus, then four 220R in parallel

    // ---- Envelopes
    const double kEnvC1 = 1e-6, kEnvC2 = 1e-6, kEnvC3 = 2e-6;
    const double kEnvR = 10e3;             // series with each bleed / the VCA feed
    const double kDecayPot = 1e6;          // 1M (A) DECAY pots
    // ESTIMATE: the trigger amplifier's pulse (its collector swings from saturation
    // up to about 7.5 V on a hard hit) and how fast the followers charge.
    const double kStrikeMaxV = 7.5;       // absolute volts; the caps are referenced to 0 V
    const double kStrikeTime = 0.005;      // seconds the pulse holds the bases up
    const double kChargeTau = 0.001;       // follower charging time constant
    const double kVabc = 0.6;              // 3080 bias pin, one diode above the negative rail

    // ---- VCA
    const double kVcaLoad = 1.0 / (1.0 / 10e3 + 1.0 / 50e3);   // 10K || VOL pot
    // ESTIMATE: the drawing shows no input resistor on the VCA's + input; the same
    // 100K / 220R divider as the filter stages is assumed.
    const double kVcaK = 220.0 / (100e3 + 220.0);

    // ---- Output
    const double kOutScale = 2.5;          // full scale per volt at the jack (plugin level)
    const double kDcHz = 5.0;              // output coupling

    inline double audioTaper (double p)    // "A" (log) pot: 10 % at mid travel
    {
        if (p <= 0.0) return 0.0;
        if (p >= 1.0) return 1.0;
        return (std::pow (81.0, p) - 1.0) / 80.0;
    }

    inline double clamp01 (double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

    // Hard limit with a soft knee: linear to 80 % of the rail
    inline double railClip (double x)
    {
        const double knee = 0.8 * kRail;
        const double a = std::fabs (x);
        if (a <= knee) return x;
        const double over = (a - knee) / (kRail - knee);
        const double y = knee + (kRail - knee) * std::tanh (over);
        return x < 0.0 ? -y : y;
    }

    // tanh(z)/z, the OTA's falling transconductance at large inputs
    inline double otaFactor (double vdiff)
    {
        const double z = vdiff / (2.0 * kVt);
        const double a = std::fabs (z);
        if (a < 1e-4) return 1.0;
        return std::tanh (a) / a;
    }

    inline double periodFactor()
    {
        return std::log ((kVdd - kVneg) / (kVdd - kVpos)) + std::log (kVpos / kVneg);
    }

    inline double tuneR (double p)         // TUNE: clockwise = less resistance = higher pitch
    {
        return kOscR + kTunePot * audioTaper (1.0 - clamp01 (p));
    }

    // Thevenin voltage and resistance at the current source's base
    inline void cvThevenin (double tuneV, double depthV, double sweepV, double& vth, double& rth)
    {
        const double g220 = 1.0 / kR220k;
        const double gsel = 1.0 / kRsel;
        const double g68 = 1.0 / kR68k;
        const double g = 3.0 * g220 + gsel + g68;
        vth = ((tuneV + depthV + sweepV) * g220 + kVdd * gsel) / g;
        rth = 1.0 / g;
    }

    // Solve  vth = I (rth / beta + Remit) + Vt ln(I / Is) + Vt ln(I / 4 / Is)   for ln I
    inline double solveLogI (double vth, double rth, double L, int iterations = 4)
    {
        const double rs = rth / kBeta + kRemit;
        const double c = kVt * (std::log (0.25) - 2.0 * std::log (kIs));
        for (int it = 0; it < iterations; ++it)
        {
            if (L > -3.0) L = -3.0;
            if (L < -40.0) L = -40.0;
            const double I = std::exp (L);
            const double f = I * rs + 2.0 * kVt * L + c - vth;
            const double fp = I * rs + 2.0 * kVt;
            double step = f / fp;
            if (step > 2.0) step = 2.0;
            if (step < -2.0) step = -2.0;
            L -= step;
            if (std::fabs (step) < 1e-9) break;
        }
        if (L > -3.0) L = -3.0;
        if (L < -40.0) L = -40.0;
        return L;
    }

    inline double cutoffHzForCurrent (double itotal)
    {
        double iota = itotal * 0.25;
        if (iota > kOtaMax) iota = kOtaMax;
        const double k = kR220 / (kR100 + kR220);
        return (iota / (2.0 * kVt)) * k / kFiltC / (2.0 * kPi);
    }
}

//==============================================================================
Synare::Synare()
    : fs (44100.0), fsOver (176400.0), dt (1.0 / 176400.0), smoothK (0.01),
      sel1 (kOff), sel2 (kOff), velSense (true),
      rc1 (1e-3), rc2 (1e-3), kfb (0.0), a1 (0.002), sweepAmt (0.0), depthAmt (0.0),
      tuneV (0.0), volGain (0.0), bleed1 (1.0), bleed3 (1.0),
      vcaOut (0.0), strikeLeft (0.0), strikeV (0.0), logI (-12.0),
      hp1 (0.0), hpn (0.0), lpn (0.0), dcs (0.0),
      hp1a (0.0), hpna (0.0), lpna (0.0), dca (0.0), rng (0x1234567u), noiseScale (0.0)
{
    const double defaults[kPots] = { 0.5, 0.5, 0.3, 0.0, 0.5, 0.9, 0.5, 0.5, 0.8 };
    for (int i = 0; i < kPots; ++i) target[i] = cur[i] = defaults[i];
    prepare (44100.0);
}

void Synare::prepare (double sampleRate)
{
    fs = sampleRate > 1000.0 ? sampleRate : 44100.0;
    fsOver = fs * Oversampler4x::kFactor;
    dt = 1.0 / fsOver;
    smoothK = 1.0 - std::exp (-1.0 / (0.010 * fs));          // pots settle in about 30 ms

    hp1a = 1.0 - std::exp (-dt / ((kOsc1Path + kR220) * kCoup));
    hpna = 1.0 - std::exp (-dt / ((kNoisePath + kR220) * kCoup));
    lpna = 1.0 - std::exp (-2.0 * kPi * kNoiseLpHz * dt);
    dca = 1.0 - std::exp (-2.0 * kPi * kDcHz * dt);
    // Scale white noise so the band-limited result has kNoiseRms
    noiseScale = kNoiseRms / std::sqrt (lpna / (2.0 - lpna));
    reset();
}

void Synare::reset()
{
    for (int i = 0; i < kPots; ++i) cur[i] = target[i];
    osc1.reset();
    osc2.reset();
    osc2.v = 6.0;                          // start the two out of step
    for (int i = 0; i < 4; ++i) { s[i] = 0.0; vd[i] = 0.0; }
    for (int i = 0; i < 3; ++i) env[i] = 0.0;
    vcaOut = 0.0;
    strikeLeft = 0.0;
    strikeV = 0.0;
    logI = -12.0;
    hp1 = hpn = lpn = dcs = 0.0;
    rng = 0x1234567u;
    os.reset();
    updateSmoothing();
}

//==============================================================================
void Synare::setOsc1Tune (double p)    { target[pOsc1] = clamp01 (p); }
void Synare::setOsc2Tune (double p)    { target[pOsc2] = clamp01 (p); }
void Synare::setFilterTune (double p)  { target[pFTune] = clamp01 (p); }
void Synare::setFilterOsc2 (double p)  { target[pFOsc2] = clamp01 (p); }
void Synare::setSweep (double p)       { target[pSweep] = clamp01 (p); }
void Synare::setResonance (double p)   { target[pRes] = clamp01 (p); }
void Synare::setFilterDecay (double p) { target[pFDecay] = clamp01 (p); }
void Synare::setAmpDecay (double p)    { target[pADecay] = clamp01 (p); }
void Synare::setVolume (double p)      { target[pVol] = clamp01 (p); }
void Synare::setOsc1Select (int pos)   { sel1 = pos < kOff ? kOff : (pos > kOn ? kOn : pos); }
void Synare::setOsc2Select (int pos)   { sel2 = pos < kOff ? kOff : (pos > kOn ? kOn : pos); }

void Synare::strike (double velocity)
{
    const double v = clamp01 (velocity);
    // A soft tap still lifts the bases a little; a hard hit reaches the full swing
    const double force = velSense ? (0.2 + 0.8 * v) : 1.0;
    const double height = kStrikeMaxV * force;
    if (strikeLeft <= 0.0 || height > strikeV) strikeV = height;
    strikeLeft = kStrikeTime;
}

//==============================================================================
double Synare::oscHz (double tunePos, bool slo)
{
    return 1.0 / (tuneR (tunePos) * (slo ? kOscCSlo : kOscC) * periodFactor());
}

double Synare::filterHz (double tunePos)
{
    double vth, rth;
    cvThevenin (kVdd * clamp01 (tunePos), kVgnd, 0.0, vth, rth);
    return cutoffHzForCurrent (std::exp (solveLogI (vth, rth, -10.0, 100)));
}

double Synare::filterDecayMs (double p) { return 1000.0 * kEnvC1 * (kEnvR + kDecayPot * audioTaper (clamp01 (p))); }
double Synare::ampDecayMs (double p)    { return 1000.0 * kEnvC3 * (kEnvR + kDecayPot * audioTaper (clamp01 (p))); }

double Synare::debugFilterHz() const { return cutoffHzForCurrent (std::exp (logI)); }

double Synare::debugOscPeriodSamples (int which) const
{
    const double rc = which == 0 ? rc1 : rc2;
    return rc * periodFactor() * fsOver;
}

//==============================================================================
void Synare::updateSmoothing()
{
    rc1 = tuneR (cur[pOsc1]) * kOscC;
    rc2 = tuneR (cur[pOsc2]) * (sel2 == kMid ? kOscCSlo : kOscC);

    // RES: 50K (A) between 10K (from the fourth stage) and 1K (to ground); its wiper
    // drives the first stage's - input through 6K8 (the wiper's own resistance included)
    const double rb = 1e3 + 50e3 * audioTaper (cur[pRes]);
    const double rt = 10e3 + 50e3 - (rb - 1e3);
    const double r = rb / (rt + rb);
    const double rthw = rt * rb / (rt + rb);
    const double g68 = 1.0 / (kR68 + rthw);
    const double g100 = 1.0 / kR100;
    a1 = g100 / (g100 + g68 + 1.0 / kR220);
    kfb = g68 * r / g100;

    tuneV = kVdd * cur[pFTune];            // TUNE: 50K (B) across the 12 V rail
    depthAmt = cur[pFOsc2];                // OSC 2: 50K (B) from the triangle to ground
    sweepAmt = audioTaper (cur[pSweep]);   // SWEEP: 50K (A) on the envelope follower
    bleed1 = 1.0 / (kEnvC1 * (kEnvR + kDecayPot * audioTaper (cur[pFDecay])));
    bleed3 = 1.0 / (kEnvC3 * (kEnvR + kDecayPot * audioTaper (cur[pADecay])));

    // VOL: 50K (A) loaded by the 10K at the jack
    const double a = audioTaper (cur[pVol]);
    const double rbot = 50e3 * a, rtop = 50e3 - rbot;
    const double rl = rbot > 0.0 ? (rbot * 10e3) / (rbot + 10e3) : 0.0;
    volGain = (rtop + rl) > 0.0 ? rl / (rtop + rl) : 0.0;
}

// One oversampled step of a 4093 relaxation oscillator. The output's average over
// the step is kept (sub-sample accurate edges, which keeps aliasing down).
void Synare::stepOsc (Osc& o, double rc)
{
    double t = dt;
    double highTime = 0.0;
    for (int guard = 0; guard < 4 && t > 0.0; ++guard)
    {
        const double tgt = o.high ? kVdd : 0.0;
        const double thr = o.high ? kVpos : kVneg;
        // Time to reach the threshold from here
        double tc = 1e9;
        const double num = tgt - o.v, den = tgt - thr;
        if ((o.high && o.v < thr) || (! o.high && o.v > thr))
            if (num / den > 1.0) tc = rc * std::log (num / den);
        if (tc >= t)
        {
            o.v = tgt + (o.v - tgt) * std::exp (-t / rc);
            if (o.high) highTime += t;
            t = 0.0;
        }
        else
        {
            if (o.high) highTime += tc;
            o.v = thr;
            o.high = ! o.high;
            t -= tc;
        }
    }
    o.sq = kVdd * highTime / dt;
}

double Synare::noiseSample()
{
    // Approximately Gaussian: sum of four uniforms (unit variance)
    double acc = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        acc += (double) (rng >> 8) * (1.0 / 16777216.0);
    }
    return (acc - 2.0) * 1.7320508075688772;
}

//==============================================================================
void Synare::process (float* out, int n)
{
    double over[Oversampler4x::kFactor];
    const double k = kR220 / (kR100 + kR220);

    for (int i = 0; i < n; ++i)
    {
        bool moving = false;
        for (int p = 0; p < kPots; ++p)
        {
            const double d = target[p] - cur[p];
            if (std::fabs (d) > 1e-6) { cur[p] += d * smoothK; moving = true; }
            else cur[p] = target[p];
        }
        if (moving || (i & 15) == 0) updateSmoothing();

        for (int j = 0; j < Oversampler4x::kFactor; ++j)
        {
            // ---- Oscillators (always running, as in the unit)
            stepOsc (osc1, rc1);
            stepOsc (osc2, rc2);

            // ---- Noise generator
            double nz = noiseSample() * noiseScale;
            lpn += (nz - lpn) * lpna;
            nz = railClip (lpn);

            // ---- Selector switches and the mixer into the first OTA
            double iin = 0.0;
            double x1 = 0.0;   // signal through the shared 100N
            if (sel1 == kOn)       x1 = (osc1.sq - kVgnd) / (kOsc1Path + kR220);
            else if (sel1 == kMid) x1 = nz / (kNoisePath + kR220);
            {
                // The 100N passes the selected source; its corner depends on the path
                const double c = (sel1 == kMid) ? hpna : hp1a;
                hp1 += (x1 - hp1) * c;
                iin += x1 - hp1;
            }
            if (sel2 == kOn) iin += (osc2.sq - kVgnd) / (kOsc2Path + kR220);
            rng = rng * 1664525u + 1013904223u;
            const double floorNoise = ((double) (rng >> 8) * (1.0 / 16777216.0) - 0.5) * 3.46 * kNoiseFloor;
            const double vplus = iin * kR220 + floorNoise;

            // ---- Envelopes (capacitor voltages, absolute)
            if (strikeLeft > 0.0)
            {
                const double vb = strikeV - kVbe;
                const double c = 1.0 - std::exp (-dt / kChargeTau);
                for (int e = 0; e < 3; ++e)
                    if (vb > env[e]) env[e] += (vb - env[e]) * c;
                strikeLeft -= dt;
            }
            env[0] -= env[0] * bleed1 * dt;
            env[2] -= env[2] * bleed3 * dt;
            {
                // The click envelope bleeds into the VCA's bias pin through 10K
                const double x = env[1] - kVabc;
                const double i2 = 0.5 * (x + std::sqrt (x * x + 4.0 * kVt * kVt)) / kEnvR;
                env[1] -= i2 / kEnvC2 * dt;
                if (env[1] < 0.0) env[1] = 0.0;
            }

            // ---- Filter CV: mixer into the current source
            const double vtri = osc2.v - kVbe;                          // triangle, absolute
            const double depthV = kVgnd + depthAmt * (vtri - kVgnd);    // OSC 2 pot wiper
            const double sweepV = (env[0] > kVbe ? env[0] - kVbe : 0.0) * sweepAmt;
            double vth, rth;
            cvThevenin (tuneV, depthV, sweepV, vth, rth);
            logI = solveLogI (vth, rth, logI);
            double iota = std::exp (logI) * 0.25;
            if (iota > kOtaMax) iota = kOtaMax;
            const double gm = iota / (2.0 * kVt);

            // ---- Four-pole OTA ladder, solved without delay in the resonance loop
            double G[4], S[4];
            for (int st = 0; st < 4; ++st)
            {
                const double div = st == 0 ? a1 : k;
                double w = gm * div / kFiltC * otaFactor (vd[st]);
                double g = w * dt * 0.5;
                if (g > 1.4) g = 1.4;
                g = std::tan (g);
                G[st] = g / (1.0 + g);
                S[st] = (1.0 - G[st]) * s[st];
            }
            const double X = vplus / a1;
            const double gam = G[0] * G[1] * G[2] * G[3];
            const double sig = G[3] * G[2] * G[1] * S[0] + G[3] * G[2] * S[1] + G[3] * S[2] + S[3];
            const double y4 = (gam * X + sig) / (1.0 + gam * kfb);
            double u = X - kfb * y4;
            double y = 0.0;
            for (int st = 0; st < 4; ++st)
            {
                const double v = (u - s[st]) * G[st];
                y = v + s[st];
                s[st] = railClip (y + v);
                vd[st] = (st == 0 ? a1 : k) * (u - y);
                y = railClip (y);
                u = y;
            }
            const double vf = y;

            // ---- VCA: 3080 with its output into 10K || VOL; bias from two envelopes
            const double x3 = env[2] - kVbe - kVabc;
            const double i3 = 0.5 * (x3 + std::sqrt (x3 * x3 + 4.0 * kVt * kVt)) / kEnvR;
            const double xc = env[1] - kVabc;
            const double i2 = 0.5 * (xc + std::sqrt (xc * xc + 4.0 * kVt * kVt)) / kEnvR;
            double iabc = i2 + i3;
            if (iabc > kOtaMax) iabc = kOtaMax;
            double vo = vcaOut;
            for (int it = 0; it < 3; ++it)
            {
                const double z = kVcaK * (vf - vo) / (2.0 * kVt);
                const double th = std::tanh (z);
                const double h = vo - kVcaLoad * iabc * th;
                const double hp = 1.0 + kVcaLoad * iabc * (1.0 - th * th) * kVcaK / (2.0 * kVt);
                vo -= h / hp;
            }
            vcaOut = vo;

            // ---- VOL pot, output coupling
            const double jack = vo * volGain;
            dcs += (jack - dcs) * dca;
            over[j] = (jack - dcs) * kOutScale;
        }

        double yout = os.downsample (over);
        if (! (yout == yout)) { reset(); yout = 0.0; }
        out[i] = (float) yout;
    }
}

} // namespace tbsynare
