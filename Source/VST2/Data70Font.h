// Data70Font.h
// Lettering for the preset panel in the spirit of the typeface the user asked for,
// DATA 70 (Letraset, 1970): the "computer" face modelled on cheque-reading (MICR)
// numerals, with heavy uprights, hairline cross strokes and square shapes.
// Drawn here from our own outlines (no font file is bundled, nothing is copied
// from the commercial face), so it looks the same on Mac OS X 10.8.5 and current
// macOS.
//
// Glyphs are centre lines on a cap height of 1 (y up from the baseline): each
// stroke is a list of points "x,y"; Z closes it. Upright segments are drawn heavy,
// level ones thin, slanted ones in between. Lower case is drawn as capitals.
//
//   d70::draw ("PRESET 3", cap, colour, cx, cy)    centred on (cx, cy)
//   d70::width ("SAVE", cap)                       its width in points
//
// Objective-C++ (Cocoa); include in one .mm file only.

#ifndef TBSYNARE_DATA70FONT_H
#define TBSYNARE_DATA70FONT_H

#import <Cocoa/Cocoa.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace d70
{

struct Glyph { char ch; double w; const char* strokes; };   // strokes separated by '|'

// Widths in cap heights (between the outer centre lines).
static const Glyph kGlyphs[] = {
    { 'A', 0.9,  "0,0 0,1 0.9,1 0.9,0|0,0.5 0.9,0.5" },
    { 'B', 0.9,  "0,0 0,1 0.75,1 0.75,0.52|0,0.52 0.9,0.52 0.9,0 0,0" },
    { 'C', 0.86, "0.86,1 0,1 0,0 0.86,0" },
    { 'D', 0.9,  "0,0 0,1 0.65,1 0.9,0.75 0.9,0 0,0" },
    { 'E', 0.82, "0.82,1 0,1 0,0 0.82,0|0,0.52 0.66,0.52" },
    { 'F', 0.82, "0.82,1 0,1 0,0|0,0.52 0.66,0.52" },
    { 'G', 0.9,  "0.86,1 0,1 0,0 0.9,0 0.9,0.48 0.45,0.48" },
    { 'H', 0.9,  "0,0 0,1|0.9,0 0.9,1|0,0.52 0.9,0.52" },
    { 'I', 0.0,  "0,0 0,1" },
    { 'J', 0.8,  "0.8,1 0.8,0 0,0 0,0.3" },
    { 'K', 0.9,  "0,0 0,1|0.9,1 0.9,0.75 0.6,0.52 0,0.52|0.6,0.52 0.9,0.3 0.9,0" },
    { 'L', 0.8,  "0,1 0,0 0.8,0" },
    { 'M', 1.2,  "0,0 0,1 1.2,1 1.2,0|0.6,1 0.6,0.4" },
    { 'N', 0.9,  "0,0 0,1 0.9,1 0.9,0" },
    { 'O', 0.9,  "0,0 0,1 0.9,1 0.9,0 Z" },
    { 'P', 0.9,  "0,0 0,1 0.9,1 0.9,0.46 0,0.46" },
    { 'Q', 0.9,  "0,0 0,1 0.9,1 0.9,0 Z|0.55,0.18 0.95,-0.12" },
    { 'R', 0.9,  "0,0 0,1 0.9,1 0.9,0.5 0,0.5|0.6,0.5 0.9,0.25 0.9,0" },
    { 'S', 0.9,  "0.86,1 0,1 0,0.52 0.9,0.52 0.9,0 0,0" },
    { 'T', 0.9,  "0,1 0.9,1|0.45,1 0.45,0" },
    { 'U', 0.9,  "0,1 0,0 0.9,0 0.9,1" },
    { 'V', 0.9,  "0,1 0,0.35 0.45,0 0.9,0.35 0.9,1" },
    { 'W', 1.2,  "0,1 0,0 1.2,0 1.2,1|0.6,0 0.6,0.6" },
    { 'X', 0.9,  "0,1 0,0.75 0.9,0.25 0.9,0|0,0 0,0.25 0.9,0.75 0.9,1" },
    { 'Y', 0.9,  "0,1 0,0.5 0.9,0.5|0.9,1 0.9,0 0.1,0" },
    { 'Z', 0.88, "0,1 0.88,1 0.88,0.75 0,0.25 0,0 0.88,0" },
    { '0', 0.8,  "0,0 0,1 0.8,1 0.8,0 Z" },
    { '1', 0.5,  "0,0.85 0.25,0.85 0.25,0|0,0 0.5,0" },
    { '2', 0.8,  "0,1 0.8,1 0.8,0.52 0,0.52 0,0 0.8,0" },
    { '3', 0.8,  "0,1 0.8,1 0.8,0 0,0|0.25,0.52 0.8,0.52" },
    { '4', 0.84, "0,1 0,0.38 0.84,0.38|0.64,0.7 0.64,0" },
    { '5', 0.8,  "0.8,1 0,1 0,0.55 0.8,0.55 0.8,0 0,0" },
    { '6', 0.8,  "0.7,1 0,1 0,0 0.8,0 0.8,0.55 0,0.55" },
    { '7', 0.8,  "0,1 0.8,1 0.8,0.6 0.35,0.3 0.35,0" },
    { '8', 0.8,  "0,0 0,1 0.8,1 0.8,0 Z|0,0.53 0.8,0.53" },
    { '9', 0.8,  "0.1,0 0.8,0 0.8,1 0,1 0,0.45 0.8,0.45" },
    { '.', 0.0,  "0,0 0,0.12" },
    { ',', 0.0,  "0,0.12 0,-0.12" },
    { '-', 0.55, "0,0.48 0.55,0.48" },
    { '_', 0.8,  "0,-0.05 0.8,-0.05" },
    { ':', 0.0,  "0,0 0,0.12|0,0.6 0,0.72" },
    { '/', 0.6,  "0,0 0.6,1" },
    { '+', 0.7,  "0,0.48 0.7,0.48|0.35,0.18 0.35,0.78" },
    { '=', 0.7,  "0,0.32 0.7,0.32|0,0.64 0.7,0.64" },
    { '%', 0.9,  "0,0 0.9,1|0,0.75 0,1 0.25,1 0.25,0.75 Z|0.65,0 0.65,0.25 0.9,0.25 0.9,0 Z" },
    { '\'',0.0,  "0,1 0,0.72" },
    { '"', 0.24, "0,1 0,0.72|0.24,1 0.24,0.72" },
    { '!', 0.0,  "0,0 0,0.12|0,0.3 0,1" },
    { '?', 0.8,  "0,1 0.8,1 0.8,0.55 0.4,0.55 0.4,0.3|0.4,0 0.4,0.12" },
    { '(', 0.28, "0.28,1.1 0,1.1 0,-0.1 0.28,-0.1" },
    { ')', 0.28, "0,1.1 0.28,1.1 0.28,-0.1 0,-0.1" },
    { '&', 0.9,  "0.9,0 0.15,0.6 0.15,1 0.65,1 0.65,0.6 0,0.3 0,0 0.7,0 0.9,0.35" },
    { '#', 0.8,  "0.25,0 0.25,1|0.55,0 0.55,1|0,0.34 0.8,0.34|0,0.66 0.8,0.66" },
    { '*', 0.6,  "0.3,0.4 0.3,1|0,0.55 0.6,0.85|0,0.85 0.6,0.55" },
    { '<', 0.6,  "0.6,0.9 0,0.5 0.6,0.1" },
    { '>', 0.6,  "0,0.9 0.6,0.5 0,0.1" },
};

const double kSpace = 0.6;         // word space
const double kTrack = 0.42;        // between letters (centre line to centre line, beyond the strokes)
const double kHeavy = 0.24;        // upright strokes, in cap heights
const double kThin = 0.085;        // level strokes
const double kWeight = kHeavy;     // ink beyond the centre lines

inline const Glyph* find (char c)
{
    if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
    for (size_t i = 0; i < sizeof (kGlyphs) / sizeof (kGlyphs[0]); ++i)
        if (kGlyphs[i].ch == c) return &kGlyphs[i];
    return 0;
}

// Width of the text at cap height 1 (ink only, no trailing track)
inline double unitWidth (const char* s)
{
    double w = 0.0;
    bool any = false;
    for (const char* p = s; *p; ++p)
    {
        if (*p == ' ') { w += kSpace; continue; }
        const Glyph* g = find (*p);
        if (! g) { w += kSpace; continue; }
        if (any) w += kTrack;
        w += g->w;
        any = true;
    }
    return w;
}

inline double width (const char* s, double cap) { return (unitWidth (s) + kWeight) * cap; }

// Adds one stroke ("x,y x,y ... [Z]") as filled segments: uprights heavy, level
// strokes thin, slants between. Flipped view (y down).
inline void addStroke (NSBezierPath* path, const char* s, size_t len, double ox, double base, double cap)
{
    double xs[24], ys[24];
    int n = 0;
    bool closed = false;
    size_t i = 0;
    while (i < len && n < 24)
    {
        while (i < len && s[i] == ' ') ++i;
        if (i >= len) break;
        if (s[i] == 'Z') { closed = true; ++i; continue; }
        char* end = 0;
        const double x = std::strtod (s + i, &end);
        i = (size_t) (end - s);
        if (i < len && s[i] == ',') ++i;
        const double y = std::strtod (s + i, &end);
        i = (size_t) (end - s);
        xs[n] = ox + x * cap; ys[n] = base - y * cap;
        ++n;
    }
    if (n < 2) return;
    const double hv = kHeavy * cap, th = kThin * cap;
    const int segs = closed ? n : n - 1;
    for (int k = 0; k < segs; ++k)
    {
        const double x0 = xs[k], y0 = ys[k], x1 = xs[(k + 1) % n], y1 = ys[(k + 1) % n];
        const double dx = x1 - x0, dy = y1 - y0;
        if (std::fabs (dx) < 1e-6)
        {
            // upright: heavy, reaching the outer edges of the thin strokes it meets
            const double top = std::min (y0, y1) - th * 0.5, bot = std::max (y0, y1) + th * 0.5;
            [path appendBezierPathWithRect: NSMakeRect (x0 - hv * 0.5, top, hv, bot - top)];
        }
        else if (std::fabs (dy) < 1e-6)
        {
            const double l = std::min (x0, x1) - hv * 0.5, r = std::max (x0, x1) + hv * 0.5;
            [path appendBezierPathWithRect: NSMakeRect (l, y0 - th * 0.5, r - l, th)];
        }
        else
        {
            // slant: a parallelogram, cut level at both ends
            const double w = (hv + th) * 0.5;
            [path moveToPoint: NSMakePoint (x0 - w * 0.5, y0)];
            [path lineToPoint: NSMakePoint (x0 + w * 0.5, y0)];
            [path lineToPoint: NSMakePoint (x1 + w * 0.5, y1)];
            [path lineToPoint: NSMakePoint (x1 - w * 0.5, y1)];
            [path closePath];
        }
    }
}

// Draws text with its ink centred on (cx, cy), in a flipped view
inline void draw (const char* s, double cap, NSColor* colour, double cx, double cy)
{
    const double total = (unitWidth (s) + kWeight) * cap;
    double x = cx - total * 0.5 + kWeight * cap * 0.5;
    const double base = cy + cap * 0.5;
    NSBezierPath* path = [NSBezierPath bezierPath];
    [path setWindingRule: NSNonZeroWindingRule];
    bool any = false;
    for (const char* p = s; *p; ++p)
    {
        if (*p == ' ') { x += kSpace * cap; continue; }
        const Glyph* g = find (*p);
        if (! g) { x += kSpace * cap; continue; }
        if (any) x += kTrack * cap;
        const char* st = g->strokes;
        while (*st)
        {
            const char* bar = std::strchr (st, '|');
            const size_t len = bar ? (size_t) (bar - st) : std::strlen (st);
            addStroke (path, st, len, x, base, cap);
            st += len + (bar ? 1 : 0);
        }
        x += g->w * cap;
        any = true;
    }
    [colour set];
    [path fill];
}

} // namespace d70

#endif
