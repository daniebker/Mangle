// Zoom and pan over the current view (pure logic, no I/O).
//
// What is on screen at zoom 1 is a "base": a rectangle of the image (a panel,
// or the whole page) fitted to the screen. Zooming scales that up around a
// centre point in image pixels; the view never leaves the base rectangle, so
// a zoomed panel shows only that panel.
#pragma once

#include <stdbool.h>

#define ZOOM_MAX 4.0     // relative to the fitted view
#define ZOOM_SNAP 1.1    // pinching back below this returns to the fitted view

typedef struct {
	int    bx, by, bw, bh;    // base rectangle in image pixels
	double s0;                // screen pixels per image pixel at zoom 1
	int    sw, sh;            // screen size
} ZoomBase;

typedef struct {
	double z;         // 1 = fitted (not zoomed)
	double cx, cy;    // centre of the view, image pixels (only used when zoomed)
} Zoom;

// Base for showing rect {x, y, w, h} on a sw x sh screen. Without upscale the
// image is never enlarged at zoom 1 (as compose() does for whole pages).
ZoomBase zoom_base(const int rect[4], bool upscale, int sw, int sh);

void zoom_reset(Zoom* zm);
bool zoom_active(const Zoom* zm);

// Screen pixels per image pixel.
double zoom_scale(const Zoom* zm, const ZoomBase* b);

// Keep the view inside the base rectangle (centred on an axis where it is
// wider than the base).
void zoom_clamp(Zoom* zm, const ZoomBase* b);

// Pinch: fingers' midpoint moved from (mx0, my0) to (mx1, my1) (screen pixels)
// and their distance changed by `scale`. The image point under the first
// midpoint ends up under the second. Returns true if the view changed.
bool zoom_pinch(Zoom* zm, const ZoomBase* b, int mx0, int my0, int mx1, int my1, double scale);

// Drag by (dx, dy) screen pixels (the image follows the finger). Only when
// zoomed. Returns true if the view changed.
bool zoom_pan(Zoom* zm, const ZoomBase* b, int dx, int dy);
