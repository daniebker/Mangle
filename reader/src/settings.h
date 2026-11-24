// User settings, saved as a small text file ("settings") in the state folder.
#pragma once

#include <stdbool.h>

typedef struct {
	bool sleep;       // show the book cover after a period of inactivity, then suspend
	bool overview;    // panel mode: show each paneled page whole before its panels
} Settings;

// Overwrites the fields the file holds; others keep the caller's defaults.
void settings_load(const char* dir, Settings* s);
int  settings_save(const char* dir, const Settings* s);
