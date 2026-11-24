// Touchscreen event decoding: raw evdev events -> taps and long presses.
// Pure logic (no I/O) so it can be tested with synthetic event streams.
#pragma once

#include <linux/input.h>
#include <stdbool.h>
#include <stdint.h>

#define LONG_PRESS_MS 700

typedef struct {
	// Raw axis ranges (from EVIOCGABS) and screen size.
	int  min_x, max_x, min_y, max_y;
	int  screen_w, screen_h;
	bool swap_xy, mirror_x, mirror_y;    // per-model quirks (MANGLE_TOUCH)
	bool proto_a;                        // MT protocol A without BTN_TOUCH: contact = frame has data

	// Internal state.
	int     slot;
	bool    down, was_down, frame_data;
	int     rx, ry;              // latest raw position of the first contact
	int     sx, sy, maxmove;     // start position and largest move (screen px)
	int64_t t_down;
} Touch;

typedef enum { TOUCH_NONE, TOUCH_TAP, TOUCH_LONG } TouchResult;

void touch_init(Touch* t, int min_x, int max_x, int min_y, int max_y, int screen_w, int screen_h);

// Parse "swap,mirror_x,mirror_y" (any subset, comma separated).
void touch_quirks(Touch* t, const char* spec);

// Map raw coordinates to the screen.
void touch_map(const Touch* t, int rx, int ry, int* x, int* y);

// Feed one event. On TOUCH_TAP / TOUCH_LONG, *x, *y hold the press position.
TouchResult touch_feed(Touch* t, const struct input_event* ev, int64_t now, int* x, int* y);
