// The CYD reader: library, reading screen, menu and settings.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Runs until the input layer reports IN_QUIT (desktop simulator only; on the
// board it never returns).
void app_run(void);

#ifdef __cplusplus
}
#endif
