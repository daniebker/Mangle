// Input: touch taps, long presses, drags, pinches and physical keys.
// Backends: evdev (Kindle, /dev/input/eventX) and sim (commands on stdin).
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { IN_TAP, IN_LONG, IN_KEY, IN_QUIT, IN_DRAG, IN_PINCH } InKind;

typedef struct {
	InKind kind;
	int    x, y;      // screen coordinates: taps, drag start, pinch midpoint at the start
	int    code;      // key code for IN_KEY
	int    dx, dy;    // IN_DRAG: movement; IN_PINCH: how far the midpoint moved
	float  scale;     // IN_PINCH: finger distance at the end / at the start
} InEvent;

int  input_open(int screen_w, int screen_h);
void input_close(void);

// Wait up to timeout_ms (-1 = forever) for an event.
// Returns 1 with *ev filled, 0 on timeout, -1 if interrupted by a signal.
int input_wait(InEvent* ev, int timeout_ms);

// Monotonic milliseconds.
int64_t now_ms(void);

// Release grabbed devices. Async-signal-safe (used from crash handlers).
void input_release_grabs(void);
