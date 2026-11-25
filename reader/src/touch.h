// Touchscreen event decoding: raw evdev events -> taps, long presses, drags
// and two-finger pinches.
// Pure logic (no I/O) so it can be tested with synthetic event streams.
#pragma once

#include <linux/input.h>
#include <stdbool.h>
#include <stdint.h>

#define LONG_PRESS_MS 700
#define TOUCH_MAX 2    // contacts tracked (two are enough for a pinch)

typedef struct {
	bool down;
	bool data;      // protocol A: position seen for this contact in the current frame
	int  rx, ry;    // latest raw position
} Contact;

typedef enum { TS_IDLE, TS_ONE, TS_PINCH, TS_WAIT_UP } TouchState;

typedef struct {
	// Raw axis ranges (from EVIOCGABS) and screen size.
	int  min_x, max_x, min_y, max_y;
	int  screen_w, screen_h;
	bool swap_xy, mirror_x, mirror_y;    // per-model quirks (MANGLE_TOUCH)
	bool proto_a;                        // MT protocol A without BTN_TOUCH: contact = frame has data

	// Internal state.
	int        slot;          // protocol B slot / protocol A contact index in the frame
	bool       mt_a;          // SYN_MT_REPORT seen: the device speaks protocol A
	bool       mt_pos;        // ABS_MT_POSITION_* seen: ignore the single-touch ABS_X/ABS_Y copy
	bool       mt_id;         // ABS_MT_TRACKING_ID seen: ignore BTN_TOUCH
	Contact    c[TOUCH_MAX];
	TouchState state;
	int        sx, sy, lx, ly, maxmove;    // one finger: start, last position, largest move (screen px)
	int        p0[4], p1[4];               // pinch: both contacts at the start and now (x0, y0, x1, y1)
	int64_t    t_down;
} Touch;

typedef enum { TOUCH_NONE, TOUCH_TAP, TOUCH_LONG, TOUCH_DRAG, TOUCH_PINCH } TouchResult;

typedef struct {
	int   x, y;      // tap/long/drag: press position; pinch: midpoint of the fingers at the start
	int   dx, dy;    // drag: movement; pinch: how far the midpoint moved
	float scale;     // pinch: finger distance at the end / at the start (>1 = spread = zoom in)
} TouchOut;

void touch_init(Touch* t, int min_x, int max_x, int min_y, int max_y, int screen_w, int screen_h);

// Parse "swap,mirror_x,mirror_y" (any subset, comma separated).
void touch_quirks(Touch* t, const char* spec);

// Map raw coordinates to the screen.
void touch_map(const Touch* t, int rx, int ry, int* x, int* y);

// Feed one event. On a result other than TOUCH_NONE, *out describes it.
// One finger: TOUCH_TAP / TOUCH_LONG if it barely moved, else TOUCH_DRAG on
// release. Two fingers: TOUCH_PINCH when one of them lifts (the rest of the
// touch is then ignored until every finger is up).
TouchResult touch_feed(Touch* t, const struct input_event* ev, int64_t now, TouchOut* out);
