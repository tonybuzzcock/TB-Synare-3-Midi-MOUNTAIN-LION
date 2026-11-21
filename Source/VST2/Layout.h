// Layout.h
// Panel geometry, shared by the panel (PanelView.mm) and the editor test, so the
// test always clicks where the controls are drawn. Coordinates are in points,
// y down from the top left.
//
// Two rows of controls on the black face, each section in its own rounded
// oblong box as on the Synare 3 (the unit's boxes curve only because the unit
// is round): OSCILLATOR 1 and OSCILLATOR 2 on top, FILTER and AMPLIFIER below.
// Underneath: the TB logo and VELOCITY on the black, and a small white preset
// panel.

#ifndef TBSYNARE_LAYOUT_H
#define TBSYNARE_LAYOUT_H

namespace tbsynare
{

enum { kEditorWidth = 828, kEditorHeight = 486 };

namespace layout
{
    // Section boxes: x, y, w, h
    const double kOsc1Box[4] = {  24,  18, 380, 168 };
    const double kOsc2Box[4] = { 424,  18, 380, 168 };
    const double kFiltBox[4] = {  24, 204, 550, 154 };
    const double kAmpBox[4]  = { 594, 204, 210, 154 };
    const double kBoxRadius = 14.0;

    // Oscillator sections: TUNE dial knob and the three-way slide switch
    const double kDialDX = 92.0, kDialY = 106.0, kDialR = 35.0;     // relative to the box's x
    const double kSlideDX = 262.0, kSlideY = 104.0;
    const double kSlideStep = 46.0;                                 // between switch positions

    // FILTER and AMPLIFIER knobs (centres)
    const double kKnobY = 282.0, kKnobR = 22.0;
    const double kFiltX[5] = { 94.0, 196.5, 299.0, 401.5, 504.0 };   // TUNE, OSC 2, SWEEP, RES, DECAY
    const double kAmpX[2]  = { 649.0, 749.0 };                       // DECAY, VOL

    // Bottom: logo, VELOCITY, preset panel
    const double kLogoBox[4] = { 24, 392, 122, 68 };
    const double kVelX = 222.0, kVelY = 432.0, kVelStep = 20.0;      // two-way slide
    const double kPresetPanel[4] = { 300, 376, 504, 96 };
    const double kSaveRect[4] = { 312, 386, 60, 34 };
    const double kSlotX0 = 382.0, kSlotPitch = 51.0, kSlotW = 45.0, kSlotY = 386.0, kSlotH = 34.0;
    const double kReadout[4] = { 312, 428, 480, 34 };

    enum { kNumSlotsDrawn = 8 };
}

} // namespace tbsynare

#endif
