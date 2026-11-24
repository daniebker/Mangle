// Library screen: a paged list of the ZIPs in a folder.
#pragma once

#include <signal.h>

#include "display.h"

// Show the library and wait for a choice. Returns a malloc'd path to the
// chosen book, or NULL to quit (exit tapped, exit key, or *quit set).
char* library_pick(const Screen* s, const char* dir, const char* state_dir, volatile sig_atomic_t* quit);

// Natural-order string compare ("vol2" < "vol10"), case-insensitive.
int natcmp(const char* a, const char* b);
