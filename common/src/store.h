// The state folder: settings, reading positions, the last book, cover
// thumbnails and touch calibration. Positions are one small file per book
// ("<book file name>.pos": page panel mode npages), keyed by file name so
// moving the library keeps them.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nav.h"

typedef struct {
	bool overview;      // panel mode: show each paneled page whole first
	int  sleep_min;     // sleep after this many idle minutes (0 = never)
	int  brightness;    // backlight, percent
	int  rotation;      // 0..3, see hal_set_rotation
	bool night;         // inverted grays
	bool invert;        // the LCD's own inversion (for panels that come up as a negative)
} Settings;

// Where everything is kept (created when first written).
void        store_init(const char* dir);
const char* store_dir(void);

// Loading overwrites only what the file holds: set defaults first.
void store_load_settings(Settings* s);
int  store_save_settings(const Settings* s);

// Position for a book file (path or name). Returns 0 if found.
int store_load_pos(const char* book, Pos* p, int* npages);
int store_save_pos(const char* book, Pos p, int npages);

// Cover thumbnail cache: gray w*h pixels plus the book's title. `stamp`
// identifies the book file's version (its size). Returns 0 if a thumbnail
// of exactly this size and stamp is cached.
int store_load_thumb(const char* book, long stamp, int w, int h, uint8_t* px, char* title, int title_len);
int store_save_thumb(const char* book, long stamp, int w, int h, const uint8_t* px, const char* title);

// Calibration data for resistive touch (opaque numbers).
int store_load_calib(int32_t* v, int n);
int store_save_calib(const int32_t* v, int n);

// Write a file via a temporary one (FAT can't rename over an existing file,
// so the old one is removed first).
int store_write(const char* path, const void* data, int len);

// The book read last, and whether it was still open when the program
// stopped (the CYD reopens it at power-on; the Kindle's "Resume" always does).
int store_set_last(const char* book, bool open);
int store_get_last(char* out, int len, bool* open);
