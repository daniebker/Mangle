// Touch gestures from polled touch-panel samples: taps, long presses, drags and
// two-finger pinches (pure logic, tested on the desktop).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

#define TIN_LONG_MS 700
#define TIN_MOVE_PX 14    // a press that moves less than this is a tap (resistive panels jitter)

typedef struct {
	int x, y;
} TPoint;

typedef enum { TIN_IDLE, TIN_ONE, TIN_PINCH, TIN_WAIT_UP } TinState;

typedef struct {
	TinState state;
	int      sx, sy, lx, ly, maxmove;
	TPoint   p0[2], p1[2];
	int64_t  t_down;
} Tin;

void tin_init(Tin* t);
// Feed the contacts seen now (n = 0 when nothing touches). Returns true and
// fills *ev when a gesture completes.
bool tin_feed(Tin* t, int n, const TPoint* pts, int64_t now, InEvent* ev);
