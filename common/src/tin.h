// Touch gestures from touch-panel samples: taps, long presses, drags and
// two-finger pinches (pure logic). Every platform turns its raw touch data
// into contact lists and feeds them here: the CYD polls its controller, the
// Kindle decodes evdev frames (reader/src/touch.c).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

#define TIN_LONG_MS 700

typedef struct {
	int x, y;
} TPoint;

typedef enum { TIN_IDLE, TIN_ONE, TIN_PINCH, TIN_WAIT_UP } TinState;

typedef struct {
	TinState state;
	int      move_px;    // a press that moves less than this is a tap
	int      sx, sy, lx, ly, maxmove;
	TPoint   p0[2], p1[2];
	int64_t  t_down;
} Tin;

void tin_init(Tin* t, int move_px);
// Feed the contacts seen now (n = 0 when nothing touches). Returns true and
// fills *ev when a gesture completes. A long press fires while the finger is
// still down, so feed the same contacts again now and then while waiting
// (tin_waiting() says when that matters).
bool tin_feed(Tin* t, int n, const TPoint* pts, int64_t now, InEvent* ev);
// A touch position in the screen's physical (portrait w x h) coordinates,
// turned to rotation 0..3 (see hal_set_rotation).
TPoint tin_rotate(int rot, int w, int h, int x, int y);

// A single finger is down and could still become a long press.
bool tin_waiting(const Tin* t);
