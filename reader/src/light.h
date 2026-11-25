// Front light (backlight) brightness. Backends: Kindle (powerd via lipc, or
// sysfs) and sim (kept in memory, logged to stdout).
#pragma once

// Find the front light. Returns its highest level, or 0 if there is none
// (or it can't be controlled).
int light_open(void);

// Current level (0 = off .. light_open()), or -1 if unknown.
int light_get(void);

// Set the level (clamped to 0..max). Returns 0 on success.
int light_set(int level);
