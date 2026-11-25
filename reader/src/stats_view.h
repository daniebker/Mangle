// Stats screen: daily reading habits and a monthly overview.
#pragma once

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

#include "display.h"
#include "stats.h"

// Show the stats screen until the user taps Back. `live` (may be NULL) is the
// session in progress, so it is counted too. `buf` is a full-screen frame
// buffer. Returns true if the program should quit.
bool stats_screen(const Screen* s, const char* state_dir, const StatRec* live, uint8_t* buf,
                  volatile sig_atomic_t* quit);
