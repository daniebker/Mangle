// Hardware layer for the CYD firmware. hal_cyd.cpp implements it on the board,
// ../sim/hal_sim.c on the desktop (frames as PGM files, input from stdin).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "input.h"    // InEvent (shared with the Kindle reader)

#ifdef __cplusplus
extern "C" {
#endif

// Screen size for the current rotation.
int hal_width(void);
int hal_height(void);

// 0 = portrait, 1 = landscape, 2 = portrait upside down, 3 = landscape upside down.
void hal_set_rotation(int r);

// Draw one row of 8-bit gray pixels / fill a rectangle with a gray level.
void hal_row(int x, int y, int w, const uint8_t* gray);
void hal_fill(int x, int y, int w, int h, uint8_t gray);
// Push out anything buffered (call when a screen is complete).
void hal_flush(void);

// Display options: inverted grays (night mode), the panel's own colour
// inversion (some batches need it), backlight 0..100.
void hal_set_night(bool on);
void hal_set_invert(bool on);
void hal_backlight(int pct);

// Wait up to timeout_ms (-1 = forever) for input. 1 = event, 0 = timeout.
// Never returns IN_PINCH on single-touch (resistive) boards.
int  hal_wait(InEvent* ev, int timeout_ms);
bool hal_multitouch(void);

// Turn the screen off and sleep lightly until the screen is touched.
// The touch that wakes it is swallowed.
void hal_sleep_until_touch(void);

// Touch calibration (resistive boards). hal_calibrate runs the on-screen
// procedure; returns false where it doesn't apply.
bool hal_needs_calibration(void);
bool hal_calibrate(void);

// Folder holding the books and the state folder, e.g. "/sd/manga".
const char* hal_library_dir(void);
const char* hal_state_dir(void);

int64_t hal_ms(void);
void    hal_log(const char* fmt, ...)
#ifdef __GNUC__
	__attribute__((format(printf, 1, 2)))
#endif
	;

// Show a fatal problem (no SD card...) and stop.
void hal_halt(void);

#ifdef __cplusplus
}
#endif
