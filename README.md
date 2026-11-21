# TB Synare 3 (MIDI) — Mountain Lion VST

A VST 2.4 instrument model of the **Star Instruments Synare 3** drum synthesizer
(1977), played from MIDI: **every key on the keyboard hits the pad**. The keys never
change the pitch; the panel's TUNE controls set it, as on the unit. Velocity takes
the place of how hard the stick hits (switchable). For **Mac OS X 10.8.5 Mountain
Lion**, 64-bit Intel, loaded in Pro Tools 11 through Blue Cat PatchWork (or any
VST 2.4 host). Personal, non-commercial use.

Built from the Synare 3 repair manual (`Docs/Synare-3-Service-Manual.pdf`), the
Clacktronics redraw of its schematic (`Docs/Star-Synare-3-Schematic-Clacktronics.pdf`)
and photos of the user's own Synare 3.

## The panel

Black face, white lettering, each section in its own rounded oblong box, in two rows:

| OSCILLATOR 1 | OSCILLATOR 2 |
|---|---|
| TUNE (numbered dial), OFF / NOISE / OSC1 | TUNE (numbered dial), OFF / SLO / OSC2 |

| FILTER | AMPLIFIER |
|---|---|
| TUNE, OSC 2, SWEEP, RES, DECAY | DECAY, VOL |

Underneath: the TB electronic logo, the **VELOCITY** switch (plugin only: off = every
note is a full-strength hit), and a small white preset panel (8 named presets in
DATA 70-style lettering: SAVE, a slot, type a name, Return; empty slots are white,
saved light grey, the one in use dark grey). Slide switches light up at their
engaged position. Drag knobs up/down (Shift for fine), double-click resets.

## The circuit, as modelled

- **Noise**: reverse-biased junction noise, one-transistor amplifier (clips on the rails).
- **Oscillators**: two 4093 Schmitt-trigger relaxation oscillators, 22K + 1M (A) TUNE,
  47n: about 32 Hz – 1.47 kHz (manual: 40 – 1500 Hz). SLO adds the 1u: ÷22 (manual: ÷20).
  Oscillator 2's buffered triangle modulates the filter through FILTER OSC 2.
- **Mixer**: Osc 1 / noise through the shared 100N, Osc 2 square through 470K + 470K,
  into the first OTA's 220R.
- **Filter**: four 3080 OTA integrators with emitter followers (24 dB/oct), resonance
  from stage 4 back to stage 1 through the RES pot and 6K8; self-oscillates near full RES
  (the disco tom). The OTAs' tanh and the rail limits are modelled; solved without delay
  in the resonance loop.
- **Filter CV**: TUNE, OSC 2 triangle and SWEEP envelope through 220K each, plus the
  factory bottom-end resistor and 68K, into the transistor current source feeding the
  OTA bias pins (exponential at the bottom, resistor-limited above): 33 Hz – 4.2 kHz
  (manual: 30 Hz – 4 kHz).
- **Envelopes**: the strike charges three capacitors through emitter followers: 1u with
  FILTER DECAY bleed (sweep), 1u with a fixed 10K into the VCA (the click), 2u with
  AMPLIFIER DECAY bleed (the body). A new hit only tops them up, as on the unit.
- **VCA**: a 3080 into the VOL pot.
- Runs at 4x the host rate (linear-phase decimation; latency reported to the host).

### Estimates (values the drawing does not fix)

Each is a named constant in `Source/DSP/Synare.cpp`:

| Value | Estimate | Why |
|---|---|---|
| 4093 thresholds | 6.95 V / 5.0 V | gives the manual's oscillator range |
| Filter integrating capacitors | 1.5 nF | unmarked; sets the 4 kHz top |
| Factory bottom-end resistor | 2.7 M | manual: 2–3 M, set for 30 Hz |
| Noise level / bandwidth | 0.35 V rms, 20 kHz | not specified |
| Strike pulse | 7.5 V peak, 5 ms, 1 ms charge | trigger amp swing, estimated |
| VCA input divider | 100K / 220R | not drawn; same as the filter stages |
| Output level | 2.5 x the jack voltage | plugin level |

## Install (10.8.5)

1. GitHub → **Actions** → latest run → **Artifacts** → `TB-Synare-3-Midi-mountain-lion-vst`.
2. Unzip twice, copy **TB Synare 3.vst** to `/Library/Audio/Plug-Ins/VST/`.
3. Rescan in PatchWork and load it as an instrument. Send it MIDI from a track or keyboard.

## Build and test

- Linux: `make -C Tests run vst-run` (DSP tests, demo WAVs in `Tests/renders`, plugin
  loaded in a mini host), `make -C Tests asan`.
- Mac: `scripts/build_vst_mac.sh` (checks the binary declares 10.8.5 and links only
  10.8-era system libraries). GitHub Actions builds, tests the panel offscreen and
  publishes screenshots to the `gui-preview` branch.
