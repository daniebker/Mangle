// Hardware layer: everything that differs between the devices Mangle runs on.
// The app (app.c) and everything else in common/ only talk to the device
// through these functions.
//
//   reader/src/hal_kindle.c   Kindle: FBInk e-ink framebuffer, evdev input
//   cyd/src/board/hal_cyd.cpp Cheap Yellow Display: ST7796 LCD, XPT2046/GT911 touch
//   common/sim/hal_sim.c      desktop simulator: PGM frames, commands on stdin
//
// fb.c implements the drawing half for devices with a memory framebuffer
// (Kindle and simulator).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- screen ------------------------------------------------------------------

// Screen size in the current rotation.
int hal_width(void);
int hal_height(void);

// 0 = portrait, 1 = landscape (90 degrees), 2 = portrait upside down, 3 = 270 degrees.
void hal_set_rotation(int r);

// Draw a row of 8-bit gray pixels / fill a rectangle.
void hal_row(int x, int y, int w, const uint8_t* gray);
void hal_fill(int x, int y, int w, int h, uint8_t gray);

// The screen is complete: show it. full = a flashing refresh that clears
// e-ink ghosting (LCDs ignore it).
void hal_present(bool full);

// Inverted grays (night mode).
void hal_set_night(bool on);

// Backlight / frontlight, 0..100 (where hal_has_backlight()).
bool hal_has_backlight(void);
void hal_backlight(int pct);

// The panel's own colour inversion, for LCD batches that come up as a
// negative (where hal_has_invert()).
bool hal_has_invert(void);
void hal_set_invert(bool on);

// ---- input -------------------------------------------------------------------

// Wait up to timeout_ms (-1 = forever) for input. 1 = event, 0 = timeout.
int  hal_wait(InEvent* ev, int timeout_ms);
// Two-finger pinches are possible.
bool hal_multitouch(void);

// Touch calibration (resistive panels). hal_calibrate runs the on-screen
// procedure; it does nothing where hal_needs_calibration() can't be true.
bool hal_can_calibrate(void);
bool hal_needs_calibration(void);
void hal_calibrate(void);

// ---- system ------------------------------------------------------------------

// Sleep with what's on screen left showing (e-ink) or the screen off (LCD),
// until the user wakes the device. The waking input is swallowed.
// Returns false if the device can't sleep (the app then waits for input).
bool hal_sleep(void);

// There is somewhere to exit to (the Kindle's KUAL menu); the CYD has not.
bool hal_can_exit(void);

// The clock knows the date (reading statistics need it).
bool hal_clock_ok(void);

int64_t hal_ms(void);    // monotonic milliseconds
void    hal_log(const char* fmt, ...)
#ifdef __GNUC__
	__attribute__((format(printf, 1, 2)))
#endif
	;

#ifdef __cplusplus
}
#endif
