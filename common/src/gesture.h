// Turns taps, holds and keys into reader actions (pure logic, testable).
//
// Tap zones (RTL): left third = forward, centre third = toggle/forward,
// right third = back. LTR books mirror the side zones.
// A tap in the top MENU_ZONE_DIV'th of the screen opens the menu bar.
// A centre tap waits DOUBLE_TAP_MS for a second tap: two taps toggle
// panel/page mode, one tap moves forward. Side taps act immediately.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DOUBLE_TAP_MS 300
#define MENU_ZONE_DIV 12    // top 1/12 of the screen opens the menu

typedef enum {
	ACT_NONE = 0,
	ACT_FORWARD,
	ACT_BACK,
	ACT_TOGGLE,
	ACT_EXIT,
	ACT_REFRESH,    // full flashing redraw
	ACT_ZOOM,       // hold on a side: step the zoom (2x, 3x, fitted) around the finger
	ACT_MENU,       // tap on the top edge: open the menu bar
} Action;

typedef enum { ZONE_LEFT, ZONE_CENTRE, ZONE_RIGHT } Zone;

typedef struct {
	bool    pending;     // a centre tap is waiting for a possible second tap
	int64_t deadline;    // ms timestamp when the pending tap becomes a single tap
} Gesture;

Zone gesture_zone(int x, int screen_w);

// Feed a tap at time `now` (ms). Writes up to 2 actions to out; returns count.
// (A side tap while a centre tap is pending first resolves it as forward.)
int gesture_tap(Gesture* g, int x, int y, int w, int h, bool rtl, int64_t now, Action out[2]);

// Long press: centre exits the book, sides step the zoom.
Action gesture_long(Gesture* g, int x, int w);

// Physical key (linux/input.h code).
Action gesture_key(Gesture* g, int code, bool rtl);

// Milliseconds until the pending tap resolves, or -1 if none.
int gesture_timeout(const Gesture* g, int64_t now);

// Call when the timeout expires. Returns ACT_FORWARD if a single centre tap resolved.
Action gesture_expire(Gesture* g, int64_t now);
