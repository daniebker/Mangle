// Mangle for the ESP32-3248S035 Cheap Yellow Display.
#include <Arduino.h>

#include "../app.h"

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
	app_run();    // only returns in the desktop simulator
}
