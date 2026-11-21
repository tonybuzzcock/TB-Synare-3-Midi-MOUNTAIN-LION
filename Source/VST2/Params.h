// Params.h
// Parameter table shared by the VST wrapper, the editor and the preset store.
//
// Front-panel controls first, in the Synare 3's order round the pad
// (OSCILLATOR 1, OSCILLATOR 2, FILTER, AMPLIFIER), then the plugin-only VELOCITY
// switch on the strip. New parameters go on the end, so saved sessions and
// presets keep working.

#ifndef TBSYNARE_PARAMS_H
#define TBSYNARE_PARAMS_H

namespace tbsynare
{

enum Param
{
    // OSCILLATOR 1
    kOsc1Tune = 0, kOsc1Sel,
    // OSCILLATOR 2
    kOsc2Tune, kOsc2Sel,
    // FILTER
    kFltTune, kFltOsc2, kSweep, kRes, kFltDecay,
    // AMPLIFIER
    kAmpDecay, kVolume,
    // Plugin only (strip)
    kVelocity,
    kNumParams
};

enum Kind { kOscHz, kSel1, kSel2, kFiltHz, kDial, kFDecayMs, kADecayMs, kSwitch };

const char* const kParamNames[kNumParams] = {
    "O1 Tune", "O1 Sel", "O2 Tune", "O2 Sel",
    "F Tune", "F Osc2", "Sweep", "Res", "F Decay",
    "A Decay", "Vol",
    "Velocty"
};

// Names in the panel readout
const char* const kReadoutNames[kNumParams] = {
    "OSC 1 TUNE", "OSCILLATOR 1", "OSC 2 TUNE", "OSCILLATOR 2",
    "FILTER TUNE", "FILTER OSC 2", "SWEEP", "RES", "FILTER DECAY",
    "AMP DECAY", "VOL",
    "VELOCITY"
};

const int kParamKind[kNumParams] = {
    kOscHz, kSel1, kOscHz, kSel2,
    kFiltHz, kDial, kDial, kDial, kFDecayMs,
    kADecayMs, kDial,
    kSwitch
};

// Starts as a classic Synare tom: both sources off, the filter ringing, swept down
const float kDefaults[kNumParams] = {
    0.5f, 0.0f,        // OSC 1 TUNE, OFF
    0.5f, 0.0f,        // OSC 2 TUNE, OFF
    0.25f, 0.0f,       // FILTER TUNE, OSC 2 depth
    0.8f, 1.0f,        // SWEEP, RES fully up
    0.55f,             // FILTER DECAY
    0.55f, 0.85f,      // AMPLIFIER DECAY, VOL
    1.0f               // VELOCITY on
};

// Three-position slide switches: 0 = OFF (left), 0.5 = centre, 1 = right
inline int selIndex (float v)
{
    const int r = (int) (v * 2.0f + 0.5f);
    return r < 0 ? 0 : (r > 2 ? 2 : r);
}
inline float selValue (int r) { return (float) r * 0.5f; }

inline bool isOn (float v) { return v >= 0.5f; }

} // namespace tbsynare

#endif
