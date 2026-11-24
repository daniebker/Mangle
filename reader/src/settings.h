// User settings, saved as a small text file ("settings") in the state folder.
#pragma once

#include <stdbool.h>

typedef struct {
	bool sleep;    // show the book cover after a period of inactivity
} Settings;

// Defaults, overridden by whatever the file holds.
void settings_load(const char* dir, Settings* s);
int  settings_save(const char* dir, const Settings* s);
