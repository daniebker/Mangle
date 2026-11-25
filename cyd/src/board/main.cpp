// Mangle for the ESP32-3248S035 Cheap Yellow Display.
#include <Arduino.h>

#include "app.h"
#include "board.h"

extern "C" void cyd_init(void);

// The JSON and PNG code is frugal with the stack, but the default 8 KiB loop
// stack leaves little room for the SD card driver underneath.
#ifdef SET_LOOP_TASK_STACK_SIZE
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

void setup()
{
	Serial.begin(115200);
	cyd_init();
}

void loop()
{
	AppConfig cfg = {};
	cfg.library   = LIBRARY_DIR;
	cfg.state     = STATE_DIR;
	cfg.resume    = RESUME_IF_OPEN;    // power-on carries on with the book that was open
	cfg.verbose   = true;              // page timings on the serial log
	cfg.defaults  = { true, 5, 80, 0, false, false };    // preview, sleep 5 min, 80% backlight
	app_run(&cfg);                     // the board has nowhere to exit to: it never returns
}
