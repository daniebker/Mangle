// Kindle touchscreen: raw evdev events -> the contacts that are down, in
// screen coordinates (multitouch protocols A and B, and single-touch panels).
// The gestures themselves (tap, hold, drag, pinch) come from common/src/tin.c,
// as on every other device. Pure logic, tested with synthetic event streams.
#pragma once

#include <linux/input.h>
#include <stdbool.h>
#include <stdint.h>

#include "tin.h"

#define TOUCH_MAX 2    // contacts tracked (two are enough for a pinch)

typedef struct {
	bool down;
	bool data;      // protocol A: position seen for this contact in the current frame
	int  rx, ry;    // latest raw position
} Contact;

typedef struct {
	// Raw axis ranges (from EVIOCGABS) and screen size.
	int  min_x, max_x, min_y, max_y;
	int  screen_w, screen_h;
	bool swap_xy, mirror_x, mirror_y;    // per-model quirks (MANGLE_TOUCH)
	bool proto_a;                        // MT protocol A without BTN_TOUCH: contact = frame has data

	// Internal state.
	int     slot;      // protocol B slot / protocol A contact index in the frame
	bool    mt_a;      // SYN_MT_REPORT seen: the device speaks protocol A
	bool    mt_pos;    // ABS_MT_POSITION_* seen: ignore the single-touch ABS_X/ABS_Y copy
	bool    mt_id;     // ABS_MT_TRACKING_ID seen: ignore BTN_TOUCH
	Contact c[TOUCH_MAX];
} Touch;

void touch_init(Touch* t, int min_x, int max_x, int min_y, int max_y, int screen_w, int screen_h);

// Parse "swap,mirror_x,mirror_y" (any subset, comma separated).
void touch_quirks(Touch* t, const char* spec);

// Map raw coordinates to the screen.
void touch_map(const Touch* t, int rx, int ry, int* x, int* y);

// Feed one event. At the end of a frame (SYN_REPORT) returns the number of
// contacts down (0..TOUCH_MAX) with their positions in pts; otherwise -1.
int touch_feed(Touch* t, const struct input_event* ev, TPoint* pts);
