// The Mangle app: library, reading screen, menu bar, settings and reading
// statistics. The same code runs on every device; hal.h is all that differs.
#pragma once

#include <stdbool.h>

#include "store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	RESUME_NONE,       // start in the library (or with `book`)
	RESUME_LAST,       // reopen the book read last (Kindle: "Resume last book")
	RESUME_IF_OPEN,    // reopen it only if it was open when the program stopped (CYD power-on)
} Resume;

typedef struct {
	const char* library;        // folder of books; NULL = read `book` and stop
	const char* state;          // folder for settings, positions, statistics
	const char* book;           // open this book first (may be NULL)
	Resume      resume;
	int         flash_every;    // e-ink: full refresh every N page turns (0 = never)
	int         idle_ms;        // sleep delay override in ms (tests); 0 = the Sleep setting
	bool        prefetch;       // decode the next and previous images in the background
	bool        verbose;        // log every image shown
	Settings    defaults;       // settings before the saved ones are loaded
} AppConfig;

// Natural-order compare ("Vol 2" before "Vol 10"), as the library sorts.
int app_natcmp(const char* a, const char* b);

// Runs until the user exits (or the input layer reports IN_QUIT).
void app_run(const AppConfig* cfg);

#ifdef __cplusplus
}
#endif
