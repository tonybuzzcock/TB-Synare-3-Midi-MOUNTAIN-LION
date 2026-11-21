// Synare.h
// Star Instruments Synare 3 drum synthesizer (1977), modelled from its schematic
// (Star Synare 3 repair manual, as redrawn by Clacktronics, rev. 1) and the
// Synare 3 repair manual's circuit descriptions.
//
// Circuit blocks, as in the unit:
//   Noise       reverse-biased B-E junction noise, one-transistor amplifier
//   Osc 1       4093 Schmitt-trigger relaxation oscillator, 22K + 1M (A) TUNE, 47n
//   Osc 2       the same, plus a SLO position that adds 1u (about 1/20 the pitch)
//               and a buffered triangle output that modulates the filter
//   Selectors   OSCILLATOR 1: OFF / NOISE / OSC1    OSCILLATOR 2: OFF / SLO / OSC2
//   Filter      four 3080 OTA integrators with emitter followers, 24 dB/oct low
//               pass, resonance from the fourth stage back to the first (it
//               self-oscillates, which gives the classic disco tom)
//   Filter CV   resistor mixer (TUNE pot, OSC 2 triangle depth, SWEEP envelope,
//               factory bottom-end resistor, 68K) into a transistor current source
//               feeding the four OTAs' bias pins
//   Envelopes   three emitter followers charging capacitors on a strike:
//               1u with FILTER DECAY bleed (the sweep), 1u with a fixed 10K bleed
//               (the click), 2u with AMPLIFIER DECAY bleed; the last two drive the VCA
//   VCA         a 3080 two-quadrant multiplier into the VOL pot
//
// The pad strike is replaced by strike(velocity): the trigger amplifier's output
// pulse charges the envelope capacitors, its height set by how hard the pad is hit.
//
// The whole voice runs at 4x the host rate and is decimated with a linear-phase
// FIR (Oversampler4x). Mono out. Plain C++98, no allocation in process().

#ifndef TBSYNARE_SYNARE_H
#define TBSYNARE_SYNARE_H

#include "Oversampler4x.h"

namespace tbsynare
{

class Synare
{
public:
    enum { kOff = 0, kMid = 1, kOn = 2 };   // selector positions: left, centre, right

    Synare();

    void prepare (double sampleRate);
    void reset();

    // Front panel (pot positions 0..1, clockwise = 1)
    void setOsc1Tune (double p);
    void setOsc1Select (int pos);           // OFF / NOISE / OSC1
    void setOsc2Tune (double p);
    void setOsc2Select (int pos);           // OFF / SLO / OSC2
    void setFilterTune (double p);
    void setFilterOsc2 (double p);          // triangle modulation depth
    void setSweep (double p);
    void setResonance (double p);
    void setFilterDecay (double p);
    void setAmpDecay (double p);
    void setVolume (double p);

    // Plugin: when off, every strike is a full-strength hit
    void setVelocitySense (bool on) { velSense = on; }

    // A hit on the pad. velocity 0..1 (MIDI velocity / 127).
    void strike (double velocity);

    // Mono output, n samples at the host rate
    void process (float* out, int n);

    static int latencySamples() { return Oversampler4x::latencySamples(); }

    // Values for the panel readout
    static double oscHz (double tunePos, bool slo);
    static double filterHz (double tunePos);           // with OSC 2 depth and SWEEP at zero
    static double filterDecayMs (double p);            // time constant of the sweep envelope
    static double ampDecayMs (double p);               // time constant of the amplifier envelope

    // For tests
    double debugFilterHz() const;                      // the cutoff at this moment
    double debugEnvelope (int which) const { return env[which]; }
    double debugOscPeriodSamples (int which) const;    // at the oversampled rate

private:
    struct Osc
    {
        double v;          // timing capacitor voltage
        bool   high;       // 4093 output state
        double sq;         // output averaged over the last step (0..12 V)
        void reset() { v = 5.0; high = true; sq = 12.0; }
    };

    void   stepOsc (Osc& o, double rc);
    double filterCurrent (double vth, double rth);
    double noiseSample();
    void   updateSmoothing();

    double fs, fsOver, dt;
    Oversampler4x os;

    // Smoothed pot positions (target, current)
    enum { kPots = 9 };
    enum { pOsc1, pOsc2, pFTune, pFOsc2, pSweep, pRes, pFDecay, pADecay, pVol };
    double target[kPots], cur[kPots];
    double smoothK;
    int sel1, sel2;
    bool velSense;

    // Derived from the pots (refreshed per host sample)
    double rc1, rc2;               // oscillator time constants
    double kfb, a1;                // resonance loop gain and stage-1 input divider
    double sweepAmt, depthAmt, tuneV, volGain;
    double bleed1, bleed3;         // 1 / (R C) of the variable envelopes

    // Circuit state
    Osc osc1, osc2;
    double s[4], vd[4];            // filter integrator states and last OTA input voltages
    double env[3];                 // envelope capacitor voltages: sweep, click, amplifier
    double vcaOut;
    double strikeLeft, strikeV;    // remaining strike pulse (s) and its height (V)
    double logI;                   // filter current solver state, ln(amps)
    double hp1, hpn, lpn, dcs;     // coupling filters (one-pole low-pass states)
    double hp1a, hpna, lpna, dca;
    unsigned int rng;
    double noiseScale;
};

} // namespace tbsynare

#endif
