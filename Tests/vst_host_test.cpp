// vst_host_test.cpp
// A minimal VST 2.4 host that loads the built TB Synare 3 binary through its
// entry point and checks it as a MIDI-triggered instrument: identity,
// parameters and display strings, Note Ons strike at the right sample, any key
// gives the same sound (no pitch from the keyboard), velocity, Note Offs ignored,
// both outputs, block sizes, 96 kHz, automation, and shutdown. The plugin must
// match the DSP core sample for sample.
//
//   vst_host_test <path to plugin binary>
// On macOS pass the binary inside the bundle: "TB Synare 3.vst/Contents/MacOS/TBSynare3"

#include "../Source/VST2/vst2_abi.h"
#include "../Source/VST2/Params.h"
#include "../Source/DSP/Synare.h"

#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <vector>

using namespace tbsynare;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static int automateCalls = 0, wantMidi = 0;
static VstIntPtr VST_CALL hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    switch (opcode)
    {
        case audioMasterVersion:   return 2400;
        case audioMasterAutomate:  ++automateCalls; return 0;
        case audioMasterWantMidi:  ++wantMidi; return 1;
        default:                   return 0;
    }
}

typedef AEffect* (*EntryProc) (audioMasterCallback);

static VstIntPtr call (AEffect* e, VstInt32 op, VstInt32 idx = 0, VstIntPtr val = 0, void* ptr = 0, float opt = 0.0f)
{
    return e->dispatcher (e, op, idx, val, ptr, opt);
}

// MIDI events for the next block
struct Midi
{
    VstMidiEvent ev[64];
    struct { VstInt32 numEvents; VstIntPtr reserved; VstEvent* events[64]; } list;
    int n;
    Midi() : n (0) { std::memset (ev, 0, sizeof (ev)); list.numEvents = 0; list.reserved = 0; }
    void add (int delta, int st, int d1, int d2)
    {
        if (n >= 64) return;
        VstMidiEvent& m = ev[n];
        m.type = kVstMidiType; m.byteSize = sizeof (VstMidiEvent); m.deltaFrames = delta;
        m.midiData[0] = (char) st; m.midiData[1] = (char) d1; m.midiData[2] = (char) d2;
        list.events[n] = (VstEvent*) &ev[n];
        ++n;
    }
    void send (AEffect* e)
    {
        list.numEvents = n;
        call (e, effProcessEvents, 0, 0, &list);
        n = 0;
    }
};

static bool finite (const std::vector<float>& x, float lim = 4.0f)
{
    for (size_t i = 0; i < x.size(); ++i) if (! (x[i] == x[i]) || std::fabs (x[i]) > lim) return false;
    return true;
}

static double peak (const std::vector<float>& x, size_t from = 0, size_t to = (size_t) -1)
{
    double p = 0.0;
    to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i) p = std::max (p, (double) std::fabs (x[i]));
    return p;
}

static void reset (AEffect* e, float sr = 44100.0f)
{
    call (e, effMainsChanged, 0, 0);
    call (e, effSetSampleRate, 0, 0, 0, sr);
    call (e, effMainsChanged, 0, 1);
}

// One note (status, key, velocity) at offset 'delta' of the first block, then 'frames' rendered
static void hit (AEffect* e, std::vector<float>& L, std::vector<float>& R, int frames, int block,
                 int delta, int st, int key, int vel)
{
    L.assign (frames, 0.0f);
    R.assign (frames, 0.0f);
    for (int pos = 0; pos < frames; pos += block)
    {
        const int n = std::min (block, frames - pos);
        if (pos <= delta && delta < pos + n)
        {
            Midi m;
            m.add (delta - pos, st, key, vel);
            m.send (e);
        }
        float* out[2] = { &L[pos], &R[pos] };
        e->processReplacing (e, 0, out, n);
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: %s <plugin binary>\n", argv[0]); return 2; }
    setenv ("TBSYNARE_PRESET_FILE", "/tmp/tbsynare_host_test_presets.txt", 1);
    std::printf ("Loading %s\n", argv[1]);
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::printf ("dlopen failed: %s\n", dlerror()); return 1; }
    EntryProc entry = (EntryProc) dlsym (lib, "VSTPluginMain");
    check (entry != 0, "exports VSTPluginMain");
#if defined(__APPLE__)
    check (dlsym (lib, "main_macho") != 0, "exports main_macho (older Mac hosts)");
#endif
    if (! entry) return 1;
    AEffect* e = entry (hostCallback);
    check (e != 0, "entry point returns an effect");
    if (! e) return 1;

    std::printf ("\nIdentity\n");
    check (e->magic == kEffectMagic, "magic is 'VstP'");
    check ((e->flags & effFlagsIsSynth) != 0, "flagged as an instrument");
    check (e->numInputs == 0 && e->numOutputs == 2, "no inputs, two outputs (mono on both)");
    check (e->numParams == kNumParams, "every control is a parameter");
    check ((e->flags & effFlagsCanReplacing) != 0, "processReplacing supported");
    check (e->initialDelay == Synare::latencySamples(), "latency reported (oversampler)");
    check (e->uniqueID == VST_FOURCC ('T', 'B', 's', '3'), "unique ID 'TBs3'");
    char buf[256];
    std::memset (buf, 0, sizeof (buf));
    call (e, effGetEffectName, 0, 0, buf);
    check (std::strcmp (buf, "TB Synare 3") == 0, "name 'TB Synare 3'");
    check (call (e, effGetPlugCategory) == kPlugCategSynth, "category: synth");
    check (call (e, effCanDo, 0, 0, (void*) "receiveVstMidiEvent") == 1, "takes MIDI");

    call (e, effOpen);
    check (wantMidi > 0, "asks the host for MIDI");
    call (e, effSetSampleRate, 0, 0, 0, 44100.0f);
    call (e, effSetBlockSize, 0, 512);
    call (e, effMainsChanged, 0, 1);

    std::printf ("\nParameters\n");
    {
        bool namesOk = true, defsOk = true, dispOk = true;
        for (int i = 0; i < kNumParams; ++i)
        {
            char name[64] = { 0 }, disp[64] = { 0 };
            call (e, effGetParamName, i, 0, name);
            call (e, effGetParamDisplay, i, 0, disp);
            if (std::strcmp (name, kParamNames[i]) != 0 || std::strlen (name) > 8) namesOk = false;
            if (std::strlen (disp) > 8 || disp[0] == 0) { dispOk = false; std::printf ("  display '%s' for %s\n", disp, name); }
            if (e->getParameter (e, i) != kDefaults[i]) defsOk = false;
        }
        check (namesOk, "names as in the table, 8 characters or fewer");
        check (dispOk, "display strings present, 8 characters or fewer");
        check (defsOk, "defaults as on the panel");
        // Every value of every parameter displays within 8 characters
        bool allDisp = true;
        for (int i = 0; i < kNumParams; ++i)
            for (int k = 0; k <= 40; ++k)
            {
                e->setParameter (e, i, k / 40.0f);
                char disp[64] = { 0 };
                call (e, effGetParamDisplay, i, 0, disp);
                if (std::strlen (disp) > 8 || disp[0] == 0) allDisp = false;
            }
        for (int i = 0; i < kNumParams; ++i) e->setParameter (e, i, kDefaults[i]);
        check (allDisp, "every setting of every control displays in 8 characters");
        e->setParameter (e, kRes, 7.0f);
        check (e->getParameter (e, kRes) == 1.0f, "values clamped to 0..1");
        e->setParameter (e, kRes, std::sqrt (-1.0f));
        check (e->getParameter (e, kRes) == 1.0f, "NaN ignored");
        e->setParameter (e, kRes, kDefaults[kRes]);
    }

    std::printf ("\nMIDI\n");
    const int frames = 44100;
    std::vector<float> L, R, L2, R2;
    {
        reset (e);
        L.assign (frames, 0.0f); R.assign (frames, 0.0f);
        for (int pos = 0; pos < frames; pos += 512)
        {
            const int n = std::min (512, frames - pos);
            float* out[2] = { &L[pos], &R[pos] };
            e->processReplacing (e, 0, out, n);
        }
        check (peak (L) < 1e-3, "silent with no notes");

        reset (e);
        hit (e, L, R, frames, 512, 700, 0x90, 60, 127);
        const size_t onset = 700 + Synare::latencySamples();
        std::printf ("  peak before the note %.6f, after %.3f\n", peak (L, 0, onset - 40), peak (L, onset, onset + 8000));
        check (peak (L, 0, onset - 40) < 1e-3 && peak (L, onset, onset + 8000) > 0.05, "a Note On strikes at its sample");
        bool same = true;
        for (int i = 0; i < frames; ++i) if (L[i] != R[i]) same = false;
        check (same, "both outputs carry the voice");
        check (finite (L), "finite");

        // Sample for sample the DSP core, struck at the same sample
        Synare core;
        core.prepare (44100.0);
        core.setOsc1Tune (kDefaults[kOsc1Tune]); core.setOsc1Select (selIndex (kDefaults[kOsc1Sel]));
        core.setOsc2Tune (kDefaults[kOsc2Tune]); core.setOsc2Select (selIndex (kDefaults[kOsc2Sel]));
        core.setFilterTune (kDefaults[kFltTune]); core.setFilterOsc2 (kDefaults[kFltOsc2]);
        core.setSweep (kDefaults[kSweep]); core.setResonance (kDefaults[kRes]);
        core.setFilterDecay (kDefaults[kFltDecay]); core.setAmpDecay (kDefaults[kAmpDecay]);
        core.setVolume (kDefaults[kVolume]); core.setVelocitySense (true);
        core.reset();
        std::vector<float> ref (frames);
        core.process (&ref[0], 700);
        core.strike (1.0);
        core.process (&ref[700], frames - 700);
        bool match = true;
        for (int i = 0; i < frames; ++i) if (std::fabs (ref[i] - L[i]) > 1e-6f) { match = false; break; }
        check (match, "matches the DSP core sample for sample");

        // Any key, any channel: the same sound
        reset (e);
        hit (e, L2, R2, frames, 512, 700, 0x90, 24, 127);
        bool keySame = true;
        for (int i = 0; i < frames; ++i) if (L2[i] != L[i]) keySame = false;
        check (keySame, "a low key gives exactly the same sound (no pitch from the keyboard)");
        reset (e);
        hit (e, L2, R2, frames, 512, 700, 0x99, 108, 127);
        keySame = true;
        for (int i = 0; i < frames; ++i) if (L2[i] != L[i]) keySame = false;
        check (keySame, "a top key on channel 10 gives exactly the same sound");

        // Velocity
        reset (e);
        hit (e, L2, R2, frames, 512, 700, 0x90, 60, 40);
        check (peak (L2) < peak (L) * 0.8, "a soft note is quieter");
        e->setParameter (e, kVelocity, 0.0f);
        reset (e);
        hit (e, L2, R2, frames, 512, 700, 0x90, 60, 40);
        check (std::fabs (peak (L2) / peak (L) - 1.0) < 0.02, "with VELOCITY off every note is a full hit");
        e->setParameter (e, kVelocity, 1.0f);

        // Note Off and velocity-0 Note On do nothing
        reset (e);
        hit (e, L2, R2, frames, 512, 700, 0x80, 60, 64);
        check (peak (L2) < 1e-3, "a Note Off does not strike");
        reset (e);
        hit (e, L2, R2, frames, 512, 700, 0x90, 60, 0);
        check (peak (L2) < 1e-3, "a velocity-0 Note On (a Note Off) does not strike");

        // Odd block sizes: the same result
        reset (e);
        hit (e, L2, R2, frames, 37, 700, 0x90, 60, 127);
        bool blocks = true;
        for (int i = 0; i < frames; ++i) if (std::fabs (L2[i] - L[i]) > 1e-6f) { blocks = false; break; }
        check (blocks, "37-sample blocks give the same output as 512");

        // Accumulating process adds to the buffer
        reset (e);
        std::vector<float> A (2048, 0.25f), B (2048, 0.25f);
        Midi m; m.add (0, 0x90, 60, 127); m.send (e);
        float* outs[2] = { &A[0], &B[0] };
        e->process (e, 0, outs, 2048);
        bool added = A[0] == 0.25f && std::fabs (A[2047] - 0.25f) > 1e-4f;
        check (added, "process() adds into the host's buffers");
    }

    std::printf ("\nSample rates, automation, CPU\n");
    {
        reset (e, 96000.0f);
        hit (e, L, R, 96000, 512, 100, 0x90, 60, 127);
        check (finite (L) && peak (L) > 0.05, "plays at 96 kHz");
        reset (e, 44100.0f);

        automateCalls = 0;
        std::vector<float> o, o2;
        o.assign (frames * 3, 0.0f); o2.assign (frames * 3, 0.0f);
        std::srand (7);
        const std::clock_t c0 = std::clock();
        for (int pos = 0, blk = 0; pos < frames * 3; pos += 256, ++blk)
        {
            const int n = std::min (256, frames * 3 - pos);
            if (blk % 20 == 0) { Midi m; m.add (blk % 7, 0x90, 30 + blk % 60, 20 + blk % 108); m.send (e); }
            for (int i = 0; i < kNumParams; ++i)
                e->setParameter (e, i, (float) std::rand() / RAND_MAX);
            float* out[2] = { &o[pos], &o2[pos] };
            e->processReplacing (e, 0, out, n);
        }
        const double cpu = (double) (std::clock() - c0) / CLOCKS_PER_SEC / 3.0;
        std::printf ("  %.1f ms of CPU per second of audio\n", cpu * 1000.0);
        check (finite (o), "every control automated at random while playing: finite and bounded");
        for (int i = 0; i < kNumParams; ++i) e->setParameter (e, i, kDefaults[i]);
    }

    call (e, effMainsChanged, 0, 0);
    call (e, effClose);
    dlclose (lib);
    std::printf (failures ? "\n%d FAILED\n" : "\nAll plugin tests passed\n", failures);
    return failures ? 1 : 0;
}
