// Input events, as every platform's hal_wait() reports them.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// IN_SLEEP: the device's sleep button was pressed (the BOOT button on the
// ESP32 boards; the Kindle's power button is handled by the Kindle itself).
typedef enum { IN_TAP, IN_LONG, IN_KEY, IN_QUIT, IN_DRAG, IN_PINCH, IN_SLEEP } InKind;

typedef struct {
	InKind kind;
	int    x, y;      // screen coordinates: taps, drag start, pinch midpoint at the start
	int    code;      // key code for IN_KEY (linux/input.h numbering)
	int    dx, dy;    // IN_DRAG: movement; IN_PINCH: how far the midpoint moved
	float  scale;     // IN_PINCH: finger distance at the end / at the start
} InEvent;
