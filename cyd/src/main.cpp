// Mangle firmware for ESP32 touch-screen boards. The app is common/src; the
// board is chosen by the PlatformIO environment (cyd/src/boards/<name>/).
#include <Arduino.h>

#include "app.h"
#include "board.h"
#include "esp32_board.h"

// The JSON and PNG code is frugal with the stack, but the default 8 KiB loop
// stack leaves little room for the SD card driver underneath.
#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

void setup()
{
	Serial.begin(115200);
	esp32_init();
}

void loop()
{
	AppConfig cfg = {};
	cfg.library   = LIBRARY_DIR;
	cfg.state     = STATE_DIR;
	cfg.resume    = RESUME_IF_OPEN;    // power-on carries on with the book that was open
	cfg.prefetch  = BOARD_PREFETCH;    // decode pages ahead where there is PSRAM
	cfg.verbose   = true;              // page timings on the serial log
	cfg.defaults  = { true, 5, 80, 0, false, false };    // preview, sleep 5 min, 80% backlight
	app_run(&cfg);                     // the boards have nowhere to exit to: it never returns
}
