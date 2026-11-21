// editor_test.mm  (macOS only)
// Loads the built plugin, opens its panel in an offscreen window the way a host
// does, renders screenshots, drives every control with synthetic mouse events at
// the coordinates in Layout.h, checks host automation messages, saves, names and
// recalls presets, and closes it again.
//
//   editor_test <plugin binary> <output dir for PNGs>

#import <Cocoa/Cocoa.h>

#include "../Source/VST2/vst2_abi.h"
#include "../Source/VST2/Params.h"
#include "../Source/VST2/Editor.h"
#include "../Source/VST2/Layout.h"

#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>

using namespace tbsynare;
using namespace tbsynare::layout;

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static int automateCalls = 0, beginCalls = 0, endCalls = 0;
static VstIntPtr VST_CALL hostCallback (AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    switch (opcode)
    {
        case audioMasterVersion:   return 2400;
        case audioMasterAutomate:  ++automateCalls; return 0;
        case audioMasterBeginEdit: ++beginCalls; return 1;
        case audioMasterEndEdit:   ++endCalls; return 1;
        case audioMasterWantMidi:  return 1;
        default:                   return 0;
    }
}

typedef AEffect* (*EntryProc) (audioMasterCallback);

static VstIntPtr call (AEffect* e, VstInt32 op, VstInt32 idx = 0, VstIntPtr val = 0, void* ptr = 0, float opt = 0.0f)
{
    return e->dispatcher (e, op, idx, val, ptr, opt);
}

static NSData* renderPNG (NSView* v, const std::string& path)
{
    const NSRect b = [v bounds];
    NSBitmapImageRep* rep = [v bitmapImageRepForCachingDisplayInRect: b];
    [v cacheDisplayInRect: b toBitmapImageRep: rep];
    NSData* png = [rep representationUsingType: NSPNGFileType properties: [NSDictionary dictionary]];
    [png writeToFile: [NSString stringWithUTF8String: path.c_str()] atomically: YES];
    std::printf ("  wrote %s (%lu bytes)\n", path.c_str(), (unsigned long) [png length]);
    return png;
}

// Send a mouse event at a point in the panel's (flipped) coordinates.
static void mouse (NSWindow* w, NSView* v, NSEventType type, NSPoint panelPt, int clicks = 1)
{
    const NSPoint wp = [v convertPoint: panelPt toView: nil];
    NSEvent* e = [NSEvent mouseEventWithType: type location: wp modifierFlags: 0 timestamp: 0
                                windowNumber: [w windowNumber] context: nil eventNumber: 0
                                  clickCount: clicks pressure: 1.0f];
    if (type == NSLeftMouseDown) [v mouseDown: e];
    else if (type == NSLeftMouseDragged) [v mouseDragged: e];
    else [v mouseUp: e];
}

static void click (NSWindow* w, NSView* v, NSPoint p)
{
    mouse (w, v, NSLeftMouseDown, p);
    mouse (w, v, NSLeftMouseUp, p);
}

static void pump (double secs)
{
    [[NSRunLoop currentRunLoop] runUntilDate: [NSDate dateWithTimeIntervalSinceNow: secs]];
}

static NSPoint slotPt (int k) { return NSMakePoint (kSlotX0 + k * kSlotPitch + kSlotW * 0.5, kSlotY + kSlotH * 0.5); }

static bool fileHas (const std::string& path, const char* text)
{
    FILE* pf = std::fopen (path.c_str(), "r");
    if (! pf) return false;
    char ln[256];
    bool found = false;
    while (std::fgets (ln, sizeof (ln), pf)) if (std::strstr (ln, text)) found = true;
    std::fclose (pf);
    return found;
}

static NSTextField* nameBox (NSView* panel)
{
    NSTextField* field = nil;
    for (NSView* sv in [panel subviews])
        if ([sv isKindOfClass: [NSTextField class]]) field = (NSTextField*) sv;
    return field;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: %s <plugin binary> <png dir>\n", argv[0]); return 2; }
    setvbuf (stdout, 0, _IONBF, 0);   // unbuffered, so a crash still shows how far it got
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];
    const std::string dir = argv[2];

    // Keep the test's presets away from any real ones
    const std::string presetFile = dir + "/test_presets/Presets.txt";
    setenv ("TBSYNARE_PRESET_FILE", presetFile.c_str(), 1);
    std::remove (presetFile.c_str());

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::printf ("dlopen failed: %s\n", dlerror()); return 1; }
    EntryProc entry = (EntryProc) dlsym (lib, "VSTPluginMain");
    if (! entry) { std::printf ("no entry point\n"); return 1; }
    AEffect* e = entry (hostCallback);
    call (e, effOpen);
    call (e, effSetSampleRate, 0, 0, 0, 44100.0f);
    call (e, effMainsChanged, 0, 1);

    std::printf ("Editor\n");
    check ((e->flags & effFlagsHasEditor) != 0, "plugin reports a custom editor");
    ERect* r = 0;
    call (e, effEditGetRect, 0, 0, &r);
    check (r && r->right - r->left == kEditorWidth && r->bottom - r->top == kEditorHeight,
           "editor size as declared");

    NSWindow* win = [[NSWindow alloc] initWithContentRect: NSMakeRect (0, 0, kEditorWidth, kEditorHeight)
                                                styleMask: NSBorderlessWindowMask
                                                  backing: NSBackingStoreBuffered defer: NO];
    NSView* parent = [win contentView];
    check (call (e, effEditOpen, 0, 0, parent) == 1, "effEditOpen succeeds");
    check ([[parent subviews] count] == 1, "panel view added to the host's view");
    NSView* panel = [[parent subviews] count] ? [[parent subviews] objectAtIndex: 0] : nil;
    if (! panel) return 1;
    check (NSEqualSizes ([panel frame].size, NSMakeSize (kEditorWidth, kEditorHeight)), "panel has the declared size");

    NSData* pngDefault = renderPNG (panel, dir + "/panel_default.png");
    check ([pngDefault length] > 10000, "panel renders");

    std::printf ("\nKnobs\n");
    // Every knob answers where it is drawn and turns on its own
    {
        const int knobs[9] = { kOsc1Tune, kOsc2Tune, kFltTune, kFltOsc2, kSweep, kRes, kFltDecay, kAmpDecay, kVolume };
        const CGFloat kx[9] = { (CGFloat) (kOsc1Box[0] + kDialDX), (CGFloat) (kOsc2Box[0] + kDialDX),
                                (CGFloat) kFiltX[0], (CGFloat) kFiltX[1], (CGFloat) kFiltX[2], (CGFloat) kFiltX[3], (CGFloat) kFiltX[4],
                                (CGFloat) kAmpX[0], (CGFloat) kAmpX[1] };
        const CGFloat ky[9] = { (CGFloat) kDialY, (CGFloat) kDialY, (CGFloat) kKnobY, (CGFloat) kKnobY, (CGFloat) kKnobY,
                                (CGFloat) kKnobY, (CGFloat) kKnobY, (CGFloat) kKnobY, (CGFloat) kKnobY };
        bool all = true;
        for (int k = 0; k < 9; ++k)
        {
            for (int j = 0; j < 9; ++j) e->setParameter (e, knobs[j], 0.5f);
            automateCalls = beginCalls = endCalls = 0;
            mouse (win, panel, NSLeftMouseDown,    NSMakePoint (kx[k], ky[k]));
            mouse (win, panel, NSLeftMouseDragged, NSMakePoint (kx[k], ky[k] - 10));
            mouse (win, panel, NSLeftMouseDragged, NSMakePoint (kx[k], ky[k] - 20));
            mouse (win, panel, NSLeftMouseUp,      NSMakePoint (kx[k], ky[k] - 20));
            bool others = true;
            for (int j = 0; j < 9; ++j) if (j != k && e->getParameter (e, knobs[j]) != 0.5f) others = false;
            const float v = e->getParameter (e, knobs[k]);
            if (std::fabs (v - 0.6f) > 0.01f || ! others || beginCalls != 1 || endCalls != 1 || automateCalls < 1)
            {
                all = false;
                std::printf ("  knob %d: %.3f, others %d, begin %d end %d automate %d\n", knobs[k], v, (int) others, beginCalls, endCalls, automateCalls);
            }
        }
        check (all, "all nine knobs turn up 0.1 for a 20 px drag, each on its own, host told");
        // double-click resets
        mouse (win, panel, NSLeftMouseDown, NSMakePoint (kx[5], ky[5]), 2);
        mouse (win, panel, NSLeftMouseUp,   NSMakePoint (kx[5], ky[5]), 2);
        check (std::fabs (e->getParameter (e, kRes) - kDefaults[kRes]) < 1e-6f, "double-click resets a knob");
        for (int j = 0; j < 9; ++j) e->setParameter (e, knobs[j], kDefaults[knobs[j]]);
        // readout after turning OSC 1 TUNE
        mouse (win, panel, NSLeftMouseDown,    NSMakePoint (kx[0], ky[0]));
        mouse (win, panel, NSLeftMouseDragged, NSMakePoint (kx[0], ky[0] - 30));
        mouse (win, panel, NSLeftMouseUp,      NSMakePoint (kx[0], ky[0] - 30));
        renderPNG (panel, dir + "/panel_readout.png");
        e->setParameter (e, kOsc1Tune, kDefaults[kOsc1Tune]);
    }

    std::printf ("\nSlide switches\n");
    {
        const CGFloat s1x = (CGFloat) (kOsc1Box[0] + kSlideDX), s2x = (CGFloat) (kOsc2Box[0] + kSlideDX), sy = (CGFloat) kSlideY;
        click (win, panel, NSMakePoint (s1x, sy));
        check (selIndex (e->getParameter (e, kOsc1Sel)) == 1, "OSCILLATOR 1 centre: NOISE");
        click (win, panel, NSMakePoint (s1x + kSlideStep, sy));
        check (selIndex (e->getParameter (e, kOsc1Sel)) == 2, "OSCILLATOR 1 right: OSC1");
        click (win, panel, NSMakePoint (s1x - kSlideStep - 6, sy + 27));      // on the OFF legend
        check (selIndex (e->getParameter (e, kOsc1Sel)) == 0, "clicking the OFF legend: OFF");
        click (win, panel, NSMakePoint (s2x, sy - 27));                       // on the SLO legend
        check (selIndex (e->getParameter (e, kOsc2Sel)) == 1, "OSCILLATOR 2 SLO legend: SLO");
        // drag along the slot
        mouse (win, panel, NSLeftMouseDown,    NSMakePoint (s2x, sy));
        mouse (win, panel, NSLeftMouseDragged, NSMakePoint (s2x + kSlideStep, sy));
        mouse (win, panel, NSLeftMouseUp,      NSMakePoint (s2x + kSlideStep, sy));
        check (selIndex (e->getParameter (e, kOsc2Sel)) == 2, "dragging the slider to OSC2");
        // VELOCITY
        click (win, panel, NSMakePoint (kVelX - kVelStep, kVelY));
        check (e->getParameter (e, kVelocity) < 0.5f, "VELOCITY off");
        click (win, panel, NSMakePoint (kVelX + kVelStep, kVelY));
        check (e->getParameter (e, kVelocity) >= 0.5f, "VELOCITY on");
        // showcase: noise on, Osc 2 slow
        e->setParameter (e, kOsc1Sel, selValue (1));
        e->setParameter (e, kOsc2Sel, selValue (1));
        pump (0.1);
        renderPNG (panel, dir + "/panel_switches.png");
        e->setParameter (e, kOsc1Sel, kDefaults[kOsc1Sel]);
        e->setParameter (e, kOsc2Sel, kDefaults[kOsc2Sel]);
    }

    std::printf ("\nPresets\n");
    e->setParameter (e, kSweep, 0.3f);
    e->setParameter (e, kOsc1Sel, selValue (1));
    pump (0.1);
    click (win, panel, NSMakePoint (kSaveRect[0] + kSaveRect[2] * 0.5, kSaveRect[1] + kSaveRect[3] * 0.5));   // SAVE
    renderPNG (panel, dir + "/panel_save_armed.png");
    click (win, panel, slotPt (2));                                          // slot 3
    FILE* pf = std::fopen (presetFile.c_str(), "r");
    check (pf != 0, "SAVE then a slot writes the preset file");
    if (pf) std::fclose (pf);

    NSTextField* field = nameBox (panel);
    check (field != nil, "saving opens the name entry in the readout");
    if (field)
    {
        [field setStringValue: @"Disco tom"];
        [(id) panel controlTextDidChange: nil];
        pump (0.1);
        renderPNG (panel, dir + "/panel_naming.png");
        [field sendAction: [field action] to: [field target]];       // Return
        pump (0.2);
    }
    check (fileHas (presetFile, "name 3: Disco tom"), "Return saves the name with the preset");
    check (nameBox (panel) == nil, "and the name entry goes away");
    renderPNG (panel, dir + "/panel_preset_saved.png");

    e->setParameter (e, kSweep, 0.9f);
    e->setParameter (e, kOsc1Sel, 0.0f);
    pump (0.1);
    automateCalls = 0;
    click (win, panel, slotPt (2));
    check (std::fabs (e->getParameter (e, kSweep) - 0.3f) < 1e-5f
           && selIndex (e->getParameter (e, kOsc1Sel)) == 1, "clicking the slot recalls the saved settings");
    check (automateCalls >= kNumParams, "recall tells the host about every parameter");
    pump (0.1);
    renderPNG (panel, dir + "/panel_preset_loaded.png");

    // Clicking the readout while the preset is in use renames it; Esc keeps the old name
    click (win, panel, NSMakePoint (kReadout[0] + kReadout[2] * 0.5, kReadout[1] + kReadout[3] * 0.5));
    field = nameBox (panel);
    check (field != nil && [[field stringValue] isEqualToString: @"Disco tom"], "clicking the readout opens the name to edit");
    if (field)
    {
        [field setStringValue: @"Something else"];
        [(id) panel control: field textView: nil doCommandBySelector: @selector (cancelOperation:)];   // Esc
        pump (0.2);
    }
    check (fileHas (presetFile, "name 3: Disco tom"), "Esc keeps the old name");

    const float before = e->getParameter (e, kSweep);
    click (win, panel, slotPt (5));                                          // empty slot 6
    check (e->getParameter (e, kSweep) == before, "an empty slot changes nothing");
    for (int i = 0; i < kNumParams; ++i) e->setParameter (e, i, kDefaults[i]);

    std::printf ("\nHost automation shows on the panel\n");
    pump (0.2);
    NSData* pngBefore = renderPNG (panel, dir + "/panel_before_host.png");
    e->setParameter (e, kFltTune, 0.9f);
    e->setParameter (e, kOsc2Sel, 1.0f);
    pump (0.3);
    NSData* pngAfter = renderPNG (panel, dir + "/panel_changed.png");
    check (! [pngAfter isEqualToData: pngBefore], "panel redraws after host changes");

    // The showcase: a Synare tom, a named preset in use
    for (int i = 0; i < kNumParams; ++i) e->setParameter (e, i, kDefaults[i]);
    click (win, panel, slotPt (2));
    e->setParameter (e, kOsc1Sel, selValue (1));
    e->setParameter (e, kOsc2Sel, selValue (2));
    e->setParameter (e, kOsc2Tune, 0.7f);
    pump (0.3);
    renderPNG (panel, dir + "/panel_showcase.png");

    std::printf ("\nClose\n");
    call (e, effEditClose);
    check ([[parent subviews] count] == 0, "effEditClose removes the panel");
    check (call (e, effEditOpen, 0, 0, parent) == 1 && [[parent subviews] count] == 1, "reopens");
    call (e, effEditClose);
    call (e, effMainsChanged, 0, 0);
    call (e, effClose);
    [win release];
    [pool release];      // drain before unloading the plugin's code
    dlclose (lib);

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL EDITOR CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
