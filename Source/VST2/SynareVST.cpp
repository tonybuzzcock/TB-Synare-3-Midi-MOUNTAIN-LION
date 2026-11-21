// SynareVST.cpp
// VST 2.4 instrument wrapper for TB Synare 3 MIDI (the Star Instruments Synare 3).
//
// - MIDI in: every Note On, any key, any channel, strikes the pad. The key never
//   changes the pitch (the panel's TUNE controls set it, as on the unit); the
//   velocity sets how hard the pad is hit, unless VELOCITY is off. Note Offs are
//   ignored, as lifting the stick does nothing on the unit. Notes play at their
//   exact sample within the block.
// - Parameters follow the unit's panel (OSCILLATOR 1, OSCILLATOR 2, FILTER,
//   AMPLIFIER), then the plugin-only VELOCITY switch.
// - No audio inputs; mono voice out on both outputs.
// - On the Mac, a custom panel (PanelView.mm); elsewhere the host shows
//   generic controls.
// - Latency: the 4x oversampler's decimation filter, reported to the host.
// - Plain C++98; the editor uses only Cocoa APIs present in Mac OS X 10.8.5.

#include "vst2_abi.h"
#include "Params.h"
#include "Editor.h"
#include "Presets.h"
#include "../DSP/Synare.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
  #include <xmmintrin.h>
  #define TBSY_HAVE_SSE 1
#endif

namespace
{

const VstInt32 kUniqueID    = VST_FOURCC ('T', 'B', 's', '3');
const VstInt32 kVersion     = 1000;       // 1.0.0
const VstInt32 kNumOutputs  = 2;
const char* const kPluginName = "TB Synare 3";

using namespace tbsynare;

void copyString (char* dst, const char* src, size_t maxLen)
{
    std::strncpy (dst, src, maxLen);
    dst[maxLen] = 0;
}

class SynareVST : public EditorHost
{
public:
    explicit SynareVST (audioMasterCallback hostCallback)
        : host (hostCallback), sampleRate (44100.0), editor (0), numEvents (0)
    {
        std::memset (&effect, 0, sizeof (effect));
        effect.magic = kEffectMagic;
        effect.dispatcher = &dispatcherProc;
        effect.process = &processAccumulateProc;
        effect.setParameter = &setParameterProc;
        effect.getParameter = &getParameterProc;
        effect.processReplacing = &processReplacingProc;
        effect.processDoubleReplacing = 0;
        effect.numPrograms = 1;
        effect.numParams = kNumParams;
        effect.numInputs = 0;
        effect.numOutputs = kNumOutputs;
        effect.flags = effFlagsCanReplacing | effFlagsIsSynth;
#if defined(__APPLE__)
        effect.flags |= effFlagsHasEditor;
#endif
        effect.initialDelay = Synare::latencySamples();
        effect.ioRatio = 1.0f;
        effect.object = this;
        effect.uniqueID = kUniqueID;
        effect.version = kVersion;

        for (int i = 0; i < kNumParams; ++i)
            params[i] = kDefaults[i];

        copyString (programName, "Default", kVstMaxProgNameLen);
        applyAllParams();
        synth.prepare (sampleRate);
    }

    ~SynareVST()
    {
#if defined(__APPLE__)
        if (editor) editorClose (editor);
#endif
    }

    AEffect* getAEffect() { return &effect; }

    //==========================================================================
    // EditorHost
    float getParam (int index) const
    {
        return (index >= 0 && index < kNumParams) ? params[index] : 0.0f;
    }
    void getParamDisplay (int index, char* text) const
    {
        if (index >= 0 && index < kNumParams) formatParam (index, text);
        else text[0] = 0;
    }
    void guiBeginEdit (int index) { if (host) host (&effect, audioMasterBeginEdit, index, 0, 0, 0.0f); }
    void guiEndEdit (int index)   { if (host) host (&effect, audioMasterEndEdit, index, 0, 0, 0.0f); }
    void guiSetParam (int index, float value)
    {
        setParameter (index, value);
        if (host && index >= 0 && index < kNumParams)
            host (&effect, audioMasterAutomate, index, 0, 0, params[index]);
    }

    void refreshPresets() { presets.load(); }
    bool presetExists (int slot) const { return presets.has (slot); }
    bool savePreset (int slot)
    {
        presets.load();                 // keep slots saved by other instances
        presets.put (slot, params);
        return presets.save();
    }
    void presetName (int slot, char* text, int size) const
    {
        if (! text || size <= 0) return;
        const std::string n = presets.name (slot);
        std::strncpy (text, n.c_str(), (size_t) size - 1);
        text[size - 1] = 0;
    }
    bool renamePreset (int slot, const char* name)
    {
        presets.load();                 // keep slots saved by other instances
        if (! presets.has (slot)) return false;
        presets.setName (slot, name ? name : "");
        return presets.save();
    }
    bool loadPreset (int slot)
    {
        float v[kNumParams];
        if (! presets.get (slot, v)) return false;
        for (int i = 0; i < kNumParams; ++i)
        {
            guiBeginEdit (i);
            guiSetParam (i, v[i]);
            guiEndEdit (i);
        }
        return true;
    }

private:
    //==========================================================================
    static SynareVST* self (AEffect* e) { return static_cast<SynareVST*> (e->object); }

    static VstIntPtr VST_CALL dispatcherProc (AEffect* e, VstInt32 opcode, VstInt32 index,
                                              VstIntPtr value, void* ptr, float opt)
    {
        SynareVST* p = self (e);
        if (opcode == effClose)
        {
            delete p;
            return 1;
        }
        return p->dispatch (opcode, index, value, ptr, opt);
    }

    static void VST_CALL setParameterProc (AEffect* e, VstInt32 index, float value)
    {
        self (e)->setParameter (index, value);
    }

    static float VST_CALL getParameterProc (AEffect* e, VstInt32 index)
    {
        SynareVST* p = self (e);
        return (index >= 0 && index < kNumParams) ? p->params[index] : 0.0f;
    }

    static void VST_CALL processReplacingProc (AEffect* e, float** in, float** out, VstInt32 n)  { self (e)->process (in, out, n, false); }
    static void VST_CALL processAccumulateProc (AEffect* e, float** in, float** out, VstInt32 n) { self (e)->process (in, out, n, true); }

    //==========================================================================
    VstIntPtr dispatch (VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
    {
        switch (opcode)
        {
            case effOpen:
                if (host) host (&effect, audioMasterWantMidi, 0, 1, 0, 0.0f);    // older hosts ask for this
                return 0;
            case effSetProgram:        return 0;
            case effGetProgram:        return 0;
            case effSetProgramName:
                if (ptr) copyString (programName, (const char*) ptr, kVstMaxProgNameLen);
                return 0;
            case effGetProgramName:
                if (ptr) copyString ((char*) ptr, programName, kVstMaxProgNameLen);
                return 0;
            case effGetProgramNameIndexed:
                if (ptr && index == 0) { copyString ((char*) ptr, programName, kVstMaxProgNameLen); return 1; }
                return 0;

            case effGetParamLabel:
                if (ptr && index >= 0 && index < kNumParams)
                {
                    const int k = kParamKind[index];
                    const char* lab = "";
                    if (k == kOscHz || k == kFiltHz) lab = "Hz";
                    else if (k == kFDecayMs || k == kADecayMs) lab = "ms";
                    copyString ((char*) ptr, lab, kVstMaxParamStrLen);
                }
                return 0;
            case effGetParamDisplay:
                if (ptr && index >= 0 && index < kNumParams)
                    formatParam (index, (char*) ptr);
                return 0;
            case effGetParamName:
                if (ptr && index >= 0 && index < kNumParams)
                    copyString ((char*) ptr, kParamNames[index], kVstMaxParamStrLen);
                return 0;
            case effCanBeAutomated:
                return (index >= 0 && index < kNumParams) ? 1 : 0;

            case effSetSampleRate:
                if (opt > 0.0f) sampleRate = opt;
                return 0;
            case effSetBlockSize:
                return 0;
            case effMainsChanged:
                if (value)
                {
                    synth.prepare (sampleRate);
                    numEvents = 0;
                    if (host) host (&effect, audioMasterWantMidi, 0, 1, 0, 0.0f);
                }
                return 0;

            case effProcessEvents:
                if (ptr) queueEvents ((const VstEvents*) ptr);
                return 1;

#if defined(__APPLE__)
            case effEditGetRect:
                if (ptr)
                {
                    editRect.top = 0; editRect.left = 0;
                    editRect.bottom = (short) kEditorHeight; editRect.right = (short) kEditorWidth;
                    *(ERect**) ptr = &editRect;
                    return 1;
                }
                return 0;
            case effEditOpen:
                if (editor) { editorClose (editor); editor = 0; }
                if (ptr) editor = editorOpen (ptr, this);
                return editor ? 1 : 0;
            case effEditClose:
                if (editor) { editorClose (editor); editor = 0; }
                return 0;
            case effEditIdle:
                return 0;
#endif
            case effGetPlugCategory:   return kPlugCategSynth;
            case effGetEffectName:
                if (ptr) copyString ((char*) ptr, kPluginName, kVstMaxEffectNameLen);
                return 1;
            case effGetProductString:
                if (ptr) copyString ((char*) ptr, kPluginName, kVstMaxProductStrLen);
                return 1;
            case effGetVendorString:
                if (ptr) copyString ((char*) ptr, "TB", kVstMaxVendorStrLen);
                return 1;
            case effGetVendorVersion:  return kVersion;
            case effGetVstVersion:     return 2400;

            case effCanDo:
                if (ptr)
                {
                    const char* s = (const char*) ptr;
                    if (! std::strcmp (s, "receiveVstEvents") || ! std::strcmp (s, "receiveVstMidiEvent")) return 1;
                    if (! std::strcmp (s, "sendVstEvents") || ! std::strcmp (s, "sendVstMidiEvent")) return -1;
                }
                return 0;

            case effGetTailSize:       return (VstIntPtr) (sampleRate * 6.0);   // longest AMPLIFIER DECAY
            case effStartProcess:      return 0;
            case effStopProcess:       return 0;
            default:                   return 0;
        }
    }

    void formatParam (int index, char* text) const
    {
        char buf[32];
        const float v = params[index];
        switch (kParamKind[index])
        {
            case kOscHz:
            {
                const bool slo = index == kOsc2Tune && selIndex (params[kOsc2Sel]) == Synare::kMid;
                const double hz = Synare::oscHz (v, slo);
                std::snprintf (buf, sizeof (buf), hz < 10.0 ? "%.2f" : (hz < 100.0 ? "%.1f" : "%.0f"), hz);
                break;
            }
            case kSel1:
            {
                static const char* const names[3] = { "OFF", "NOISE", "OSC1" };
                std::strcpy (buf, names[selIndex (v)]);
                break;
            }
            case kSel2:
            {
                static const char* const names[3] = { "OFF", "SLO", "OSC2" };
                std::strcpy (buf, names[selIndex (v)]);
                break;
            }
            case kFiltHz:
            {
                const double hz = Synare::filterHz (v);
                std::snprintf (buf, sizeof (buf), hz < 100.0 ? "%.1f" : "%.0f", hz);
                break;
            }
            case kDial:     std::snprintf (buf, sizeof (buf), "%.1f", v * 10.0); break;
            case kFDecayMs: std::snprintf (buf, sizeof (buf), "%.0f", Synare::filterDecayMs (v)); break;
            case kADecayMs: std::snprintf (buf, sizeof (buf), "%.0f", Synare::ampDecayMs (v)); break;
            case kSwitch:   std::strcpy (buf, isOn (v) ? "ON" : "OFF"); break;
            default:        buf[0] = 0; break;
        }
        copyString (text, buf, kVstMaxParamStrLen);
    }

    void setParameter (VstInt32 index, float value)
    {
        if (index < 0 || index >= kNumParams) return;
        if (! (value == value)) return;   // NaN guard
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        params[index] = value;
        applyParam (index);
    }

    void applyParam (int index)
    {
        const double v = params[index];
        switch (index)
        {
            case kOsc1Tune: synth.setOsc1Tune (v); break;
            case kOsc1Sel:  synth.setOsc1Select (selIndex (params[index])); break;
            case kOsc2Tune: synth.setOsc2Tune (v); break;
            case kOsc2Sel:  synth.setOsc2Select (selIndex (params[index])); break;
            case kFltTune:  synth.setFilterTune (v); break;
            case kFltOsc2:  synth.setFilterOsc2 (v); break;
            case kSweep:    synth.setSweep (v); break;
            case kRes:      synth.setResonance (v); break;
            case kFltDecay: synth.setFilterDecay (v); break;
            case kAmpDecay: synth.setAmpDecay (v); break;
            case kVolume:   synth.setVolume (v); break;
            case kVelocity: synth.setVelocitySense (isOn (params[index])); break;
            default: break;
        }
    }

    void applyAllParams()
    {
        for (int i = 0; i < kNumParams; ++i)
            applyParam (i);
    }

    //==========================================================================
    // MIDI: Note Ons are kept with their offsets and strike at the right sample
    enum { kMaxEvents = 1024 };
    struct Ev { int delta; float vel; };

    void queueEvents (const VstEvents* ev)
    {
        for (int i = 0; i < ev->numEvents; ++i)
        {
            const VstEvent* e = ev->events[i];
            if (! e || e->type != kVstMidiType || numEvents >= kMaxEvents) continue;
            const VstMidiEvent* m = (const VstMidiEvent*) e;
            const unsigned char st = (unsigned char) m->midiData[0];
            const unsigned char d2 = (unsigned char) m->midiData[2];
            if ((st & 0xF0) != 0x90 || (d2 & 0x7F) == 0) continue;     // Note On, any key, any channel
            Ev& q = events[numEvents++];
            q.delta = m->deltaFrames < 0 ? 0 : m->deltaFrames;
            q.vel = (float) (d2 & 0x7F) / 127.0f;
        }
        // keep them in time order (hosts normally send them sorted)
        for (int i = 1; i < numEvents; ++i)
        {
            Ev x = events[i];
            int j = i - 1;
            while (j >= 0 && events[j].delta > x.delta) { events[j + 1] = events[j]; --j; }
            events[j + 1] = x;
        }
    }

    void process (float** in, float** out, VstInt32 n, bool accumulate)
    {
        (void) in;
        if (n <= 0 || ! out) return;
#if TBSY_HAVE_SSE
        const unsigned int oldCsr = _mm_getcsr();
        _mm_setcsr (oldCsr | 0x8040);   // flush denormals to zero
#endif
        VstInt32 pos = 0;
        int e = 0;
        while (pos < n)
        {
            while (e < numEvents && events[e].delta <= pos) { synth.strike (events[e].vel); ++e; }
            VstInt32 end = n;
            if (e < numEvents && events[e].delta < n) end = events[e].delta;
            if (end > pos + kScratch) end = pos + kScratch;
            const int len = (int) (end - pos);
            synth.process (scratch, len);
            for (int c = 0; c < kNumOutputs; ++c)
            {
                if (! out[c]) continue;
                if (accumulate) for (int i = 0; i < len; ++i) out[c][pos + i] += scratch[i];
                else std::memcpy (out[c] + pos, scratch, sizeof (float) * (size_t) len);
            }
            pos = end;
        }
        for (; e < numEvents; ++e) synth.strike (events[e].vel);    // offsets past the block
        numEvents = 0;
#if TBSY_HAVE_SSE
        _mm_setcsr (oldCsr);
#endif
    }

    //==========================================================================
    enum { kScratch = 512 };

    AEffect effect;
    audioMasterCallback host;
    double sampleRate;
    float params[kNumParams];
    char programName[kVstMaxProgNameLen + 1];
    Synare synth;
    void* editor;
    ERect editRect;
    PresetStore presets;
    Ev events[kMaxEvents];
    int numEvents;
    float scratch[kScratch];
};

} // namespace

//==============================================================================
VST_EXPORT AEffect* VSTPluginMain (audioMasterCallback host)
{
    if (! host)
        return 0;
    SynareVST* plugin = new SynareVST (host);
    return plugin->getAEffect();
}

#if defined(__APPLE__)
VST_EXPORT AEffect* main_macho (audioMasterCallback host)
{
    return VSTPluginMain (host);
}
#endif
