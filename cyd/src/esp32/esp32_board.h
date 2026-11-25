// What each ESP32 board (cyd/src/boards/<name>/) provides to the shared
// ESP32 layer (esp32_hal.cpp). Besides these, a board implements the drawing
// and touch-related parts of hal.h: hal_width/height, hal_set_rotation,
// hal_row, hal_fill, hal_present, hal_set_night, hal_has_invert,
// hal_set_invert, hal_multitouch and the calibration functions (or takes the
// drawing ones from common/src/fb.c).
//
// Each board folder also has a board.h with its pins and these settings:
//   LCD_BL              backlight pin (PWM)
//   NATIVE_W, NATIVE_H  panel size in portrait
//   TAP_PX              a press that moves less than this is a tap
//   BOARD_PREFETCH      1 if there is RAM (PSRAM) to decode pages ahead
//   LIBRARY_DIR, STATE_DIR
#pragma once

#include <stdbool.h>
#include <stdint.h>

extern "C" {
#include "tin.h"
}

// Display and touch: initialise (screen cleared, backlight still off).
void board_init(void);
// Contacts now (up to 2), in screen coordinates for the current rotation.
// Controllers that report changes only keep the last report until a new one.
int board_touch(TPoint* pts);
// Display sleep (keeps the picture in its memory) and wake.
void board_display_sleep(bool sleep);
// Extra wake-up sources before light sleep (the timer is always set).
void board_sleep_prepare(void);
// Mount the SD card at /sd. board_sd_end undoes a failed attempt.
bool     board_sd_mount(void);
void     board_sd_end(void);
uint64_t board_sd_size(void);

// From esp32_hal.cpp, for the boards.
void esp32_init(void);
