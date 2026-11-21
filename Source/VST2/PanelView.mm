// PanelView.mm
// The custom panel for TB Synare 3 MIDI: an original drawing laid out from the
// user's Synare 3, as the user asked: a black face with white lettering, each
// section in a straight oblong box with rounded corners (the unit's boxes curve
// only because the unit is round), in two rows: OSCILLATOR 1 and OSCILLATOR 2
// on top (TUNE dial knobs with numbered skirts and brushed caps, three-way slide
// switches), FILTER and AMPLIFIER below (black fluted knobs). No manufacturer
// artwork or logos; the user's TB electronic logo sits bottom left with
// "electronic" in white.
//
// Every slide switch lights up at its engaged position (the user's standard):
// the slider glows and its legend shines white, the other legends dim.
//
// Presets live in a small white panel (the user's design): black lettering in a
// DATA 70-style face drawn in code (Data70Font.h), slot keys white when empty,
// light grey when saved, dark grey when engaged; SAVE, then a slot, then type a
// name and press Return. The readout shows the last control touched.
//
// Cocoa, manual retain/release, and only APIs that exist in Mac OS X 10.8.5.
// Everything is drawn in code; no image files.

#import <Cocoa/Cocoa.h>

#include "Editor.h"
#include "Layout.h"
#include "Params.h"
#include "Presets.h"
#include "TBLogo.h"
#include "Data70Font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tbsynare;
using namespace tbsynare::layout;

namespace
{

enum CtlType { kCtlDial, kCtlKnob, kCtlSlide3, kCtlSlide2 };

struct Ctl
{
    int param;
    int type;
    CGFloat cx, cy;
    const char* label;      // printed above the control (0 = none)
    const char* posA;       // slide legends: left, centre (three-way only), right
    const char* posB;
    const char* posC;
};

const Ctl kCtls[] = {
    { kOsc1Tune, kCtlDial,   (CGFloat) (kOsc1Box[0] + kDialDX),  (CGFloat) kDialY,  "TUNE", 0, 0, 0 },
    { kOsc1Sel,  kCtlSlide3, (CGFloat) (kOsc1Box[0] + kSlideDX), (CGFloat) kSlideY, 0, "OFF", "NOISE", "OSC1" },
    { kOsc2Tune, kCtlDial,   (CGFloat) (kOsc2Box[0] + kDialDX),  (CGFloat) kDialY,  "TUNE", 0, 0, 0 },
    { kOsc2Sel,  kCtlSlide3, (CGFloat) (kOsc2Box[0] + kSlideDX), (CGFloat) kSlideY, 0, "OFF", "SLO", "OSC2" },
    { kFltTune,  kCtlKnob,   (CGFloat) kFiltX[0], (CGFloat) kKnobY, "TUNE",   0, 0, 0 },
    { kFltOsc2,  kCtlKnob,   (CGFloat) kFiltX[1], (CGFloat) kKnobY, "OSC 2",  0, 0, 0 },
    { kSweep,    kCtlKnob,   (CGFloat) kFiltX[2], (CGFloat) kKnobY, "SWEEP",  0, 0, 0 },
    { kRes,      kCtlKnob,   (CGFloat) kFiltX[3], (CGFloat) kKnobY, "RES",    0, 0, 0 },
    { kFltDecay, kCtlKnob,   (CGFloat) kFiltX[4], (CGFloat) kKnobY, "DECAY",  0, 0, 0 },
    { kAmpDecay, kCtlKnob,   (CGFloat) kAmpX[0],  (CGFloat) kKnobY, "DECAY",  0, 0, 0 },
    { kVolume,   kCtlKnob,   (CGFloat) kAmpX[1],  (CGFloat) kKnobY, "VOL",    0, 0, 0 },
    { kVelocity, kCtlSlide2, (CGFloat) kVelX,     (CGFloat) kVelY,  "VELOCITY", "OFF", 0, "ON" },
};
const int kNumCtls = (int) (sizeof (kCtls) / sizeof (kCtls[0]));

inline NSRect R4 (const double* r) { return NSMakeRect (r[0], r[1], r[2], r[3]); }
inline NSRect slotRect (int k) { return NSMakeRect (kSlotX0 + k * kSlotPitch, kSlotY, kSlotW, kSlotH); }

// Pointer angle (radians, 0 = up, clockwise positive) for a 0..1 value over 300 degrees.
inline CGFloat valueAngle (CGFloat v) { return (CGFloat) ((-150.0 + 300.0 * v) * M_PI / 180.0); }

// Point at angle a (0 = up, clockwise) and radius r around (cx, cy) in flipped coordinates.
inline NSPoint polar (CGFloat cx, CGFloat cy, CGFloat r, CGFloat a)
{
    return NSMakePoint (cx + r * std::sin (a), cy - r * std::cos (a));
}

NSColor* rgb (int r, int g, int b) { return [NSColor colorWithCalibratedRed: r / 255.0 green: g / 255.0 blue: b / 255.0 alpha: 1.0]; }
NSColor* ink()     { return rgb (238, 238, 234); }      // white lettering on the black
NSColor* dimInk()  { return rgb (112, 112, 110); }      // legends of positions not engaged
NSColor* glowCol() { return [NSColor colorWithCalibratedRed: 1.0 green: 0.97 blue: 0.88 alpha: 0.95]; }

// The unit's lettering: a plain, round geometric sans
NSFont* labelFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Futura-Medium" size: size];
    if (! f) f = [NSFont fontWithName: @"Helvetica" size: size];
    return f ? f : [NSFont systemFontOfSize: size];
}

NSDictionary* textAttrs (NSFont* font, NSColor* color, CGFloat kern)
{
    return [NSDictionary dictionaryWithObjectsAndKeys:
              font, NSFontAttributeName, color, NSForegroundColorAttributeName,
              [NSNumber numberWithDouble: kern], NSKernAttributeName, nil];
}

void drawText (const char* s, NSFont* font, NSColor* color, CGFloat cx, CGFloat cy, CGFloat kern = 0.0)
{
    NSString* str = [NSString stringWithUTF8String: s];
    NSDictionary* attrs = textAttrs (font, color, kern);
    const NSSize sz = [str sizeWithAttributes: attrs];
    [str drawAtPoint: NSMakePoint (cx - (sz.width - kern) * 0.5, cy - sz.height * 0.5) withAttributes: attrs];
}

// Lettering with a soft glow round it (a lit legend)
void drawGlowText (const char* s, NSFont* font, CGFloat cx, CGFloat cy, CGFloat kern)
{
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (0.0, 0.0)];
    [sh setShadowBlurRadius: 9.0];
    [sh setShadowColor: glowCol()];
    [sh set];
    drawText (s, font, [NSColor whiteColor], cx, cy, kern);
    [NSGraphicsContext restoreGraphicsState];
    drawText (s, font, [NSColor whiteColor], cx, cy, kern);
}

void line (CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1, CGFloat w)
{
    NSBezierPath* p = [NSBezierPath bezierPath];
    [p moveToPoint: NSMakePoint (x0, y0)];
    [p lineToPoint: NSMakePoint (x1, y1)];
    [p setLineWidth: w];
    [p stroke];
}

// DATA 70-style text, shrunk if needed to fit maxW
void drawData70 (const char* s, CGFloat cap, NSColor* colour, CGFloat cx, CGFloat cy, CGFloat maxW)
{
    const double w = d70::width (s, cap);
    if (w > maxW && w > 0.0) cap = (CGFloat) std::max (8.0, cap * maxW / w);
    d70::draw (s, cap, colour, cx, cy);
}

} // namespace

//==============================================================================
@interface TBSynare3PanelView : NSView <NSTextFieldDelegate>
{
    EditorHost* host;
    float shown[kNumParams];
    NSTimer* timer;
    int dragCtl;
    CGFloat dragStartY, dragStartValue;
    int readoutParam;
    bool saveMode;
    int activePreset;
    bool presetUsed[kNumPresetSlots];
    char status[96];
    NSImage* backdrop;        // the face, boxes and fixed lettering, made once
    NSTextField* nameField;   // naming a preset (its text is drawn in DATA 70 by the panel)
    NSTextField* retiredField;// a finished one, taken away once its own event is over
    int namingSlot;
}
- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h;
- (void) shutdown;
@end

@implementation TBSynare3PanelView

- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h
{
    self = [super initWithFrame: frame];
    if (self)
    {
        host = h;
        for (int i = 0; i < kNumParams; ++i) shown[i] = host->getParam (i);
        dragCtl = -1;
        readoutParam = -1;
        saveMode = false;
        activePreset = -1;
        status[0] = 0;
        backdrop = nil;
        nameField = nil;
        retiredField = nil;
        namingSlot = -1;
        host->refreshPresets();
        for (int k = 0; k < kNumPresetSlots; ++k) presetUsed[k] = host->presetExists (k);
        timer = [[NSTimer timerWithTimeInterval: 1.0 / 20.0 target: self selector: @selector (tick:)
                                       userInfo: nil repeats: YES] retain];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSDefaultRunLoopMode];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSEventTrackingRunLoopMode];
    }
    return self;
}

- (void) shutdown
{
    [NSObject cancelPreviousPerformRequestsWithTarget: self];
    namingSlot = -1;
    [self dropRetired];
    if (nameField)
    {
        [nameField setDelegate: nil];
        [nameField setTarget: nil];
        [nameField removeFromSuperview];
        [nameField release];
        nameField = nil;
    }
    if (timer) { [timer invalidate]; [timer release]; timer = nil; }
    if (backdrop) { [backdrop release]; backdrop = nil; }
    host = 0;
}

- (void) dealloc
{
    [self shutdown];
    [super dealloc];
}

- (BOOL) isFlipped { return YES; }
- (BOOL) isOpaque { return YES; }
- (BOOL) acceptsFirstMouse: (NSEvent*) e { (void) e; return YES; }

// Host-side changes (automation, presets) redraw the panel.
- (void) tick: (NSTimer*) t
{
    (void) t;
    if (! host) return;
    bool changed = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = host->getParam (i);
        if (v != shown[i]) { shown[i] = v; changed = true; }
    }
    if (changed) [self setNeedsDisplay: YES];
}

//------------------------------------------------------------------------------
// Drawing

- (void) strokeBox: (NSRect) r
{
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect: NSInsetRect (r, 1.0, 1.0)
                                                      xRadius: kBoxRadius yRadius: kBoxRadius];
    [ink() set];
    [p setLineWidth: 2.0];
    [p stroke];
}

// Face, section boxes and everything printed that never changes, drawn once
- (NSImage*) backdropImage
{
    if (backdrop) return backdrop;
    backdrop = [[NSImage alloc] initWithSize: NSMakeSize (kEditorWidth, kEditorHeight)];
    [backdrop lockFocusFlipped: YES];

    // Black face with a faint grain of light from the top
    NSGradient* face = [[[NSGradient alloc] initWithStartingColor: rgb (30, 30, 31)
                                                      endingColor: rgb (12, 12, 13)] autorelease];
    [face drawInRect: NSMakeRect (0, 0, kEditorWidth, kEditorHeight) angle: 90.0];
    [rgb (0, 0, 0) set];
    NSFrameRectWithWidth (NSMakeRect (0, 0, kEditorWidth, kEditorHeight), 1.0);

    // Section boxes and their titles (titles sit on the bottom line, as on the unit)
    const double* boxes[4] = { kOsc1Box, kOsc2Box, kFiltBox, kAmpBox };
    const char* titles[4] = { "OSCILLATOR 1", "OSCILLATOR 2", "FILTER", "AMPLIFIER" };
    for (int b = 0; b < 4; ++b)
    {
        const NSRect r = R4 (boxes[b]);
        [self strokeBox: r];
        NSFont* tf = labelFont (17.0);
        NSDictionary* a = textAttrs (tf, ink(), 2.0);
        const NSSize sz = [[NSString stringWithUTF8String: titles[b]] sizeWithAttributes: a];
        // a gap in the box line behind the title
        const CGFloat tx = NSMidX (r), ty = NSMaxY (r) - 1.0;
        [rgb (14, 14, 15) set];
        NSRectFill (NSMakeRect (tx - sz.width * 0.5 - 8.0, ty - 4.0, sz.width + 16.0, 8.0));
        drawText (titles[b], tf, ink(), tx, ty - 0.5, 2.0);
    }

    // Knob labels
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        if (! c.label || c.type == kCtlSlide2) continue;
        const CGFloat ly = (c.type == kCtlDial) ? c.cy - kDialR - 22.0 : c.cy - kKnobR - 24.0;
        drawText (c.label, labelFont (14.0), ink(), c.cx, ly, 1.2);
    }

    // Index marks for the TUNE dials (fixed on the face; the numbered skirt turns)
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        if (c.type != kCtlDial) continue;
        NSBezierPath* tri = [NSBezierPath bezierPath];
        const CGFloat ty = c.cy - kDialR - 3.0;
        [tri moveToPoint: NSMakePoint (c.cx - 5.0, ty - 7.0)];
        [tri lineToPoint: NSMakePoint (c.cx + 5.0, ty - 7.0)];
        [tri lineToPoint: NSMakePoint (c.cx, ty)];
        [tri closePath];
        [ink() set];
        [tri fill];
    }

    // The user's logo, "electronic" in white on the black
    tblogo::drawTBLogo (R4 (kLogoBox), ink());

    [backdrop unlockFocus];
    return backdrop;
}

// Black fluted knob (FILTER, AMPLIFIER) with a white line
- (void) drawKnobAt: (NSPoint) c value: (CGFloat) v
{
    const CGFloat r = kKnobR;
    // skirt / shadow
    NSBezierPath* body = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - r, c.y - r, 2 * r, 2 * r)];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (2.0, -3.0)];
    [sh setShadowBlurRadius: 5.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.9]];
    [sh set];
    [rgb (16, 16, 16) set];
    [body fill];
    [NSGraphicsContext restoreGraphicsState];

    // flutes round the edge: alternating lit and dark facets
    const int flutes = 14;
    for (int k = 0; k < flutes; ++k)
    {
        const CGFloat a0 = (CGFloat) (2.0 * M_PI * k / flutes) + valueAngle (v);
        const CGFloat a1 = a0 + (CGFloat) (M_PI / flutes);
        NSBezierPath* f = [NSBezierPath bezierPath];
        [f moveToPoint: polar (c.x, c.y, r * 0.72, a0)];
        [f lineToPoint: polar (c.x, c.y, r, a0)];
        [f lineToPoint: polar (c.x, c.y, r, a1)];
        [f lineToPoint: polar (c.x, c.y, r * 0.72, a1)];
        [f closePath];
        // light from the top left
        const CGFloat mid = (a0 + a1) * 0.5;
        const CGFloat lit = (CGFloat) (0.5 + 0.5 * std::cos (mid + M_PI * 0.25));
        [[NSColor colorWithCalibratedWhite: 0.10 + 0.22 * lit alpha: 1.0] set];
        [f fill];
    }
    // domed top
    const CGFloat rt = r * 0.74;
    NSBezierPath* top = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rt, c.y - rt, 2 * rt, 2 * rt)];
    NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (78, 78, 80) endingColor: rgb (8, 8, 9)] autorelease];
    [g drawInBezierPath: top relativeCenterPosition: NSMakePoint (-0.35, 0.45)];
    [rgb (0, 0, 0) set];
    [body setLineWidth: 1.0];
    [body stroke];
    // position line
    const CGFloat a = valueAngle (v);
    [ink() set];
    NSBezierPath* p = [NSBezierPath bezierPath];
    [p moveToPoint: polar (c.x, c.y, r * 0.30, a)];
    [p lineToPoint: polar (c.x, c.y, r * 0.95, a)];
    [p setLineWidth: 2.6];
    [p setLineCapStyle: NSRoundLineCapStyle];
    [p stroke];
}

// TUNE: black skirt numbered 0-10 that turns with the knob, knurled grip, brushed cap
- (void) drawDialAt: (NSPoint) c value: (CGFloat) v
{
    const CGFloat rs = kDialR, rg = 24.0, rc = 16.0;
    NSBezierPath* skirt = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rs, c.y - rs, 2 * rs, 2 * rs)];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (2.0, -3.0)];
    [sh setShadowBlurRadius: 6.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.9]];
    [sh set];
    [rgb (20, 20, 21) set];
    [skirt fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* sg = [[[NSGradient alloc] initWithStartingColor: rgb (58, 58, 60) endingColor: rgb (10, 10, 11)] autorelease];
    [sg drawInBezierPath: skirt relativeCenterPosition: NSMakePoint (-0.4, 0.5)];
    [rgb (0, 0, 0) set];
    [skirt setLineWidth: 1.0];
    [skirt stroke];

    // Numbers on the skirt: the one under the index mark is the setting
    NSFont* nf = labelFont (10.5);
    for (int k = 0; k <= 10; ++k)
    {
        const CGFloat a = (CGFloat) ((k / 10.0 - v) * 300.0 * M_PI / 180.0);
        const NSPoint p = polar (c.x, c.y, rs - 6.5, a);
        char num[4];
        std::snprintf (num, sizeof (num), "%d", k);
        NSAffineTransform* t = [NSAffineTransform transform];
        [t translateXBy: p.x yBy: p.y];
        [t rotateByRadians: a];
        [NSGraphicsContext saveGraphicsState];
        [t concat];
        drawText (num, nf, ink(), 0.0, 0.0);
        [NSGraphicsContext restoreGraphicsState];
    }

    // Knurled grip
    NSBezierPath* grip = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rg, c.y - rg, 2 * rg, 2 * rg)];
    [rgb (14, 14, 15) set];
    [grip fill];
    for (int k = 0; k < 48; ++k)
    {
        const CGFloat a = (CGFloat) (2.0 * M_PI * k / 48.0) + valueAngle (v);
        const CGFloat lit = (CGFloat) (0.5 + 0.5 * std::cos (a + M_PI * 0.25));
        [[NSColor colorWithCalibratedWhite: 0.16 + 0.30 * lit alpha: 1.0] set];
        const NSPoint a0 = polar (c.x, c.y, rc + 1.0, a), a1 = polar (c.x, c.y, rg - 0.5, a);
        line (a0.x, a0.y, a1.x, a1.y, 1.0);
    }
    // Brushed aluminium cap
    NSBezierPath* cap = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rc, c.y - rc, 2 * rc, 2 * rc)];
    NSGradient* cg = [[[NSGradient alloc] initWithColorsAndLocations:
                          rgb (236, 236, 232), 0.0, rgb (176, 177, 176), 0.55, rgb (120, 121, 121), 1.0, nil] autorelease];
    [cg drawInBezierPath: cap relativeCenterPosition: NSMakePoint (-0.3, 0.4)];
    [[NSColor colorWithCalibratedWhite: 1.0 alpha: 0.18] set];
    for (int k = 1; k < 6; ++k)
    {
        const CGFloat rr = rc * k / 6.0;
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.x - rr, c.y - rr, 2 * rr, 2 * rr)];
        [ring setLineWidth: 0.5];
        [ring stroke];
    }
    [rgb (40, 40, 40) set];
    [cap setLineWidth: 0.8];
    [cap stroke];
}

// Slide switch: positions (count 2 or 3) at cx -/+ step; pos = engaged index.
// The slider glows and its legend shines; the other legends are dimmed.
- (void) drawSlideAt: (NSPoint) c step: (CGFloat) step count: (int) count pos: (int) pos ctl: (const Ctl&) ctl
{
    const CGFloat half = (count == 3) ? step + 16.0 : step + 14.0;
    const NSRect slot = NSMakeRect (c.x - half, c.y - 5.0, 2.0 * half, 10.0);
    NSBezierPath* sp = [NSBezierPath bezierPathWithRoundedRect: slot xRadius: 5.0 yRadius: 5.0];
    [rgb (2, 2, 2) set];
    [sp fill];
    [rgb (86, 86, 88) set];
    [sp setLineWidth: 1.0];
    [sp stroke];

    // Slider position
    const CGFloat x = (count == 3) ? c.x + (pos - 1) * step : c.x + (pos == 0 ? -step : step);
    const NSRect tab = NSMakeRect (x - 13.0, c.y - 9.0, 26.0, 18.0);
    NSBezierPath* tp = [NSBezierPath bezierPathWithRoundedRect: tab xRadius: 2.5 yRadius: 2.5];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (0.0, 0.0)];
    [sh setShadowBlurRadius: 12.0];
    [sh setShadowColor: glowCol()];
    [sh set];
    [rgb (60, 60, 62) set];
    [tp fill];
    [NSGraphicsContext restoreGraphicsState];
    NSGradient* tg = [[[NSGradient alloc] initWithStartingColor: rgb (150, 150, 152) endingColor: rgb (44, 44, 46)] autorelease];
    [tg drawInBezierPath: tp angle: 90.0];
    [rgb (0, 0, 0) set];
    [tp setLineWidth: 0.8];
    [tp stroke];
    [[NSColor colorWithCalibratedWhite: 0.0 alpha: 0.55] set];
    for (int k = -2; k <= 2; ++k) line (x + k * 4.0, c.y - 6.0, x + k * 4.0, c.y + 6.0, 1.0);

    // Legends
    NSFont* f = labelFont (14.0);
    const char* names[3] = { ctl.posA, ctl.posB, ctl.posC };
    for (int k = 0; k < 3; ++k)
    {
        if (! names[k]) continue;
        const int idx = (count == 3) ? k : (k == 0 ? 0 : 1);
        CGFloat lx, ly;
        if (count == 3)
        {
            lx = c.x + (k - 1) * step;
            ly = (k == 1) ? c.y - 27.0 : c.y + 27.0;
            if (k == 0) lx -= 6.0;
            if (k == 2) lx += 6.0;
        }
        else
        {
            lx = c.x + (k == 0 ? -step - 6.0 : step + 4.0);
            ly = c.y + 24.0;
        }
        if (idx == pos) drawGlowText (names[k], f, lx, ly, 1.0);
        else            drawText (names[k], f, dimInk(), lx, ly, 1.0);
    }
    if (ctl.type == kCtlSlide2 && ctl.label)
        drawText (ctl.label, labelFont (13.0), ink(), c.x, c.y - 25.0, 1.2);
}

// The white preset panel
- (void) drawPresetPanel
{
    const NSRect panel = R4 (kPresetPanel);
    NSBezierPath* pp = [NSBezierPath bezierPathWithRoundedRect: panel xRadius: 10.0 yRadius: 10.0];
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (0.0, -2.0)];
    [sh setShadowBlurRadius: 6.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.8]];
    [sh set];
    [rgb (250, 250, 248) set];
    [pp fill];
    [NSGraphicsContext restoreGraphicsState];

    // SAVE: white, dark grey while armed
    [self drawKey: R4 (kSaveRect) state: saveMode ? 2 : 0];
    drawData70 ("SAVE", 13.0, saveMode ? [NSColor whiteColor] : [NSColor blackColor],
                NSMidX (R4 (kSaveRect)), NSMidY (R4 (kSaveRect)), kSaveRect[2] - 10.0);

    // Slots: white empty, light grey saved, dark grey engaged
    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        const NSRect r = slotRect (k);
        const int state = (k == activePreset) ? 2 : (presetUsed[k] ? 1 : 0);
        [self drawKey: r state: state];
        char num[4];
        std::snprintf (num, sizeof (num), "%d", k + 1);
        drawData70 (num, 16.0, state == 2 ? [NSColor whiteColor] : [NSColor blackColor], NSMidX (r), NSMidY (r), kSlotW);
    }

    [self drawReadout];
}

// state: 0 white (empty), 1 light grey (saved), 2 dark grey (engaged)
- (void) drawKey: (NSRect) r state: (int) state
{
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect: NSInsetRect (r, 0.5, 0.5) xRadius: 4.0 yRadius: 4.0];
    NSColor* top = state == 2 ? rgb (84, 84, 86) : (state == 1 ? rgb (214, 214, 212) : rgb (255, 255, 255));
    NSColor* bot = state == 2 ? rgb (58, 58, 60) : (state == 1 ? rgb (192, 192, 190) : rgb (240, 240, 238));
    NSGradient* g = [[[NSGradient alloc] initWithStartingColor: top endingColor: bot] autorelease];
    [g drawInBezierPath: p angle: 90.0];
    [rgb (60, 60, 60) set];
    [p setLineWidth: 1.0];
    [p stroke];
}

- (void) readoutText: (char*) text size: (size_t) size
{
    text[0] = 0;
    if (status[0]) { std::snprintf (text, size, "%s", status); return; }
    if (readoutParam < 0 || ! host) return;
    char disp[16] = { 0 };
    host->getParamDisplay (readoutParam, disp);
    const int kind = kParamKind[readoutParam];
    const char* unit = "";
    if (kind == kOscHz || kind == kFiltHz) unit = " HZ";
    else if (kind == kFDecayMs || kind == kADecayMs) unit = " MS";
    std::snprintf (text, size, "%s  %s%s", kReadoutNames[readoutParam], disp, unit);
}

- (void) drawReadout
{
    const NSRect box = R4 (kReadout);
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect: box xRadius: 3.0 yRadius: 3.0];
    [rgb (238, 238, 235) set];
    [p fill];
    [rgb (150, 150, 148) set];
    [p setLineWidth: 1.0];
    [p stroke];

    const CGFloat maxW = box.size.width - 18.0;
    if (nameField)
    {
        // Naming: the typed text in DATA 70, with a caret
        NSString* s = [nameField stringValue];
        const char* utf8 = [s UTF8String];
        char hint[64];
        std::snprintf (hint, sizeof (hint), "NAME PRESET %d THEN RETURN", namingSlot + 1);
        if (! utf8 || ! utf8[0])
        {
            drawData70 (hint, 12.0, rgb (150, 150, 148), NSMidX (box), NSMidY (box), maxW);
            [[NSColor blackColor] set];
            line (NSMinX (box) + 10.0, NSMidY (box) - 9.0, NSMinX (box) + 10.0, NSMidY (box) + 9.0, 1.5);
        }
        else
        {
            CGFloat cap = 14.0;
            const double w = d70::width (utf8, cap);
            if (w > maxW - 8.0) cap = (CGFloat) std::max (8.0, cap * (maxW - 8.0) / w);
            const double ww = d70::width (utf8, cap);
            d70::draw (utf8, cap, [NSColor blackColor], NSMidX (box) - 4.0, NSMidY (box));
            [[NSColor blackColor] set];
            const CGFloat cx = (CGFloat) (NSMidX (box) - 4.0 + ww * 0.5 + 4.0);
            line (cx, NSMidY (box) - 9.0, cx, NSMidY (box) + 9.0, 1.5);
        }
        return;
    }
    char text[128];
    [self readoutText: text size: sizeof (text)];
    if (text[0]) drawData70 (text, 14.0, [NSColor blackColor], NSMidX (box), NSMidY (box), maxW);
}

- (void) drawRect: (NSRect) dirty
{
    (void) dirty;
    [[self backdropImage] drawInRect: [self bounds] fromRect: NSZeroRect
                           operation: NSCompositeSourceOver fraction: 1.0 respectFlipped: YES hints: nil];

    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        const NSPoint p = NSMakePoint (c.cx, c.cy);
        switch (c.type)
        {
            case kCtlDial:   [self drawDialAt: p value: shown[c.param]]; break;
            case kCtlKnob:   [self drawKnobAt: p value: shown[c.param]]; break;
            case kCtlSlide3: [self drawSlideAt: p step: (CGFloat) kSlideStep count: 3 pos: selIndex (shown[c.param]) ctl: c]; break;
            case kCtlSlide2: [self drawSlideAt: p step: (CGFloat) kVelStep count: 2 pos: isOn (shown[c.param]) ? 1 : 0 ctl: c]; break;
            default: break;
        }
    }
    [self drawPresetPanel];
}

//------------------------------------------------------------------------------
// Mouse

- (int) ctlAtPoint: (NSPoint) p
{
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        const CGFloat dx = p.x - c.cx, dy = p.y - c.cy;
        if (c.type == kCtlSlide3)
        {
            if (std::fabs (dx) <= kSlideStep + 34.0 && dy >= -40.0 && dy <= 38.0) return i;
            continue;
        }
        if (c.type == kCtlSlide2)
        {
            if (std::fabs (dx) <= kVelStep + 26.0 && dy >= -16.0 && dy <= 34.0) return i;
            continue;
        }
        const CGFloat reach = (c.type == kCtlDial ? kDialR : kKnobR) + 5.0;
        if (dx * dx + dy * dy < reach * reach) return i;
    }
    return -1;
}

- (void) setParam: (int) param value: (float) v
{
    if (! host) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    host->guiSetParam (param, v);
    shown[param] = host->getParam (param);
    readoutParam = param;
    [self setNeedsDisplay: YES];
}

//------------------------------------------------------------------------------
// Preset names: after a save, or a click on the readout while a preset is in
// use, the readout takes typing. Return keeps the name, Esc leaves the old one,
// a click elsewhere keeps what was typed. The text field itself is invisible:
// the panel draws what is typed in DATA 70.

- (void) showPreset: (int) slot verb: (const char*) verb
{
    char name[kMaxPresetName + 8] = { 0 };
    if (host) host->presetName (slot, name, (int) sizeof (name));
    if (name[0]) std::snprintf (status, sizeof (status), "%d  %s", slot + 1, name);
    else         std::snprintf (status, sizeof (status), "PRESET %d %s", slot + 1, verb);
    [self setNeedsDisplay: YES];
}

- (void) dropRetired
{
    if (! retiredField) return;
    [retiredField setDelegate: nil];
    [retiredField setTarget: nil];
    [retiredField removeFromSuperview];
    [retiredField release];
    retiredField = nil;
    if (! nameField && [self window]) [[self window] makeFirstResponder: self];
    [self setNeedsDisplay: YES];
}

- (void) startNaming: (int) slot
{
    if (! host || slot < 0) return;
    if (nameField) [self finishNaming: YES];
    namingSlot = slot;
    char name[kMaxPresetName + 8] = { 0 };
    host->presetName (slot, name, (int) sizeof (name));

    nameField = [[NSTextField alloc] initWithFrame: NSInsetRect (R4 (kReadout), 6.0, 7.0)];
    [nameField setBezeled: NO];
    [nameField setBordered: NO];
    [nameField setDrawsBackground: NO];
    [nameField setTextColor: [NSColor clearColor]];
    [nameField setFont: [NSFont boldSystemFontOfSize: 14.0]];
    [nameField setAlignment: NSCenterTextAlignment];
    [nameField setFocusRingType: NSFocusRingTypeNone];
    [nameField setStringValue: [NSString stringWithUTF8String: name]];
    [[nameField cell] setScrollable: YES];
    [nameField setTarget: self];
    [nameField setAction: @selector (nameEntered:)];
    [nameField setDelegate: self];
    [nameField setAlphaValue: 0.0];        // takes the typing; the panel draws it in DATA 70
    [self addSubview: nameField];
    status[0] = 0;
    readoutParam = -1;
    [[self window] makeFirstResponder: nameField];
    NSText* ed = [nameField currentEditor];
    if (ed && [ed isKindOfClass: [NSTextView class]])
    {
        NSTextView* tv = (NSTextView*) ed;
        [tv setInsertionPointColor: [NSColor clearColor]];
        [tv setSelectedTextAttributes: [NSDictionary dictionaryWithObjectsAndKeys:
                                           [NSColor clearColor], NSBackgroundColorAttributeName,
                                           [NSColor clearColor], NSForegroundColorAttributeName, nil]];
        [tv setSelectedRange: NSMakeRange ([[tv string] length], 0)];
    }
    [self setNeedsDisplay: YES];
}

- (void) finishNaming: (BOOL) keep
{
    const int slot = namingSlot;
    if (slot < 0) return;
    namingSlot = -1;                       // only once (Return, then end of editing)
    if (keep && host && nameField)
    {
        const char* utf8 = [[nameField stringValue] UTF8String];
        host->renamePreset (slot, utf8 ? utf8 : "");
    }
    // the field may be in the middle of sending its action: take it away afterwards
    [self dropRetired];
    retiredField = nameField;
    nameField = nil;
    [retiredField setDelegate: nil];
    [retiredField setTarget: nil];
    [self showPreset: slot verb: "SAVED"];
    [self performSelector: @selector (dropRetired) withObject: nil afterDelay: 0.0];
}

- (void) nameEntered: (id) sender
{
    (void) sender;
    [self finishNaming: YES];
}

- (void) controlTextDidChange: (NSNotification*) n
{
    (void) n;
    [self setNeedsDisplay: YES];
}

- (void) controlTextDidEndEditing: (NSNotification*) n
{
    (void) n;
    [self finishNaming: YES];
}

- (BOOL) control: (NSControl*) c textView: (NSTextView*) tv doCommandBySelector: (SEL) sel
{
    (void) c; (void) tv;
    if (sel == @selector (cancelOperation:)) { [self finishNaming: NO]; return YES; }
    return NO;
}

// SAVE button and preset slots. Returns YES if the click was handled here.
- (BOOL) presetClick: (NSPoint) p
{
    if (NSPointInRect (p, R4 (kSaveRect)))
    {
        saveMode = ! saveMode;
        std::snprintf (status, sizeof (status), "%s", saveMode ? "SAVE: CLICK A SLOT" : "");
        [self setNeedsDisplay: YES];
        return YES;
    }
    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        if (! NSPointInRect (p, slotRect (k))) continue;
        if (saveMode)
        {
            const bool ok = host->savePreset (k);
            saveMode = false;
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (ok)
            {
                activePreset = k;
                std::snprintf (status, sizeof (status), "PRESET %d SAVED", k + 1);
                [self setNeedsDisplay: YES];
                [self startNaming: k];
                return YES;
            }
            std::snprintf (status, sizeof (status), "COULD NOT SAVE PRESET %d", k + 1);
        }
        else
        {
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (presetUsed[k] && host->loadPreset (k))
            {
                for (int j = 0; j < kNumParams; ++j) shown[j] = host->getParam (j);
                activePreset = k;
                [self showPreset: k verb: "LOADED"];
            }
            else
                std::snprintf (status, sizeof (status), "PRESET %d IS EMPTY", k + 1);
        }
        [self setNeedsDisplay: YES];
        return YES;
    }
    return NO;
}

- (void) setSlide: (const Ctl&) c atX: (CGFloat) x
{
    if (c.type == kCtlSlide3)
    {
        int pos = (int) std::floor ((x - c.cx) / kSlideStep + 1.5);
        pos = pos < 0 ? 0 : (pos > 2 ? 2 : pos);
        [self setParam: c.param value: selValue (pos)];
    }
    else
        [self setParam: c.param value: x >= c.cx ? 1.0f : 0.0f];
}

- (void) mouseDown: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    if (! host) return;
    if (nameField) [self finishNaming: YES];
    if (NSPointInRect (p, R4 (kReadout)) && activePreset >= 0)
    {
        [self startNaming: activePreset];
        return;
    }
    if ([self presetClick: p]) return;
    const int i = [self ctlAtPoint: p];
    if (i < 0) return;
    const Ctl& c = kCtls[i];
    shown[c.param] = host->getParam (c.param);   // the live value, even between timer ticks
    readoutParam = c.param;
    status[0] = 0;
    if (c.param != kVelocity) activePreset = -1;   // settings are being changed away from the preset

    if (c.type == kCtlSlide3 || c.type == kCtlSlide2)
    {
        host->guiBeginEdit (c.param);
        [self setSlide: c atX: p.x];
        dragCtl = i;                                // a slide can be dragged along too
        return;
    }

    host->guiBeginEdit (c.param);
    if ([e clickCount] == 2)
        [self setParam: c.param value: kDefaults[c.param]];   // double-click: back to default
    dragCtl = i;
    dragStartY = p.y;
    dragStartValue = shown[c.param];
    [self setNeedsDisplay: YES];
}

- (void) mouseDragged: (NSEvent*) e
{
    if (dragCtl < 0 || ! host) return;
    const Ctl& c = kCtls[dragCtl];
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    if (c.type == kCtlSlide3 || c.type == kCtlSlide2)
    {
        [self setSlide: c atX: p.x];
        return;
    }
    const bool fine = ([e modifierFlags] & NSShiftKeyMask) != 0;
    const CGFloat perPixel = fine ? 1.0 / 1000.0 : 1.0 / 200.0;
    const CGFloat v = dragStartValue + (dragStartY - p.y) * perPixel;
    [self setParam: c.param value: (float) v];
}

- (void) mouseUp: (NSEvent*) e
{
    (void) e;
    if (dragCtl >= 0 && host) host->guiEndEdit (kCtls[dragCtl].param);
    dragCtl = -1;
}

- (void) scrollWheel: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const int i = [self ctlAtPoint: p];
    if (i < 0 || ! host) return;
    const Ctl& c = kCtls[i];
    if (c.type != kCtlKnob && c.type != kCtlDial) return;
    const CGFloat d = [e deltaY];
    if (d == 0.0) return;
    status[0] = 0;
    activePreset = -1;
    host->guiBeginEdit (c.param);
    [self setParam: c.param value: (float) (shown[c.param] + d * 0.01)];
    host->guiEndEdit (c.param);
}

@end

//==============================================================================
namespace tbsynare
{

// Both calls use their own autorelease pool, so nothing of this plugin's is
// left waiting in the host's pool when the editor closes or the plugin unloads.
void* editorOpen (void* parentView, EditorHost* host)
{
    NSView* parent = (NSView*) parentView;
    if (! parent || ! host) return 0;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    TBSynare3PanelView* v = [[TBSynare3PanelView alloc]
                                 initWithFrame: NSMakeRect (0, 0, kEditorWidth, kEditorHeight) host: host];
    if (v) [parent addSubview: v];
    [pool release];
    return (void*) v;    // retained until editorClose
}

void editorClose (void* handle)
{
    TBSynare3PanelView* v = (TBSynare3PanelView*) handle;
    if (! v) return;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [v shutdown];
    [v removeFromSuperview];
    [v release];
    [pool release];
}

} // namespace tbsynare
