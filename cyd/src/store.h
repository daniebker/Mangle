// Settings, reading positions and cover thumbnails on the SD card.
// Positions use the Kindle reader's format ("<book>.pos": page panel mode npages).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nav.h"

typedef struct {
	bool overview;      // panel mode: show each paneled page whole first
	int  sleep_min;     // screen off after this many idle minutes (0 = never)
	int  brightness;    // backlight, percent
	int  rotation;      // 0..3, see hal_set_rotation
	bool night;         // inverted grays
	bool invert;        // the panel's own inversion (for panels that come up as a negative)
} CydSettings;

void store_defaults(CydSettings* s);
void store_load_settings(CydSettings* s);
int  store_save_settings(const CydSettings* s);

// Position for a book file (path or name). Returns 0 if found.
int store_load_pos(const char* book, Pos* p, int* npages);
int store_save_pos(const char* book, Pos p, int npages);

// Cover thumbnail cache: gray w*h pixels plus the book's title.
// Returns 0 if a thumbnail of exactly this size is cached.
int store_load_thumb(const char* book, int w, int h, uint8_t* px, char* title, int title_len);
int store_save_thumb(const char* book, int w, int h, const uint8_t* px, const char* title);

// Calibration data for resistive touch (opaque numbers).
int store_load_calib(int32_t* v, int n);
int store_save_calib(const int32_t* v, int n);

// Write a file via a temporary one (FAT can't rename over an existing file,
// so the old one is removed first).
int store_write(const char* path, const void* data, int len);

// The book read last (opened again at power-on).
int store_set_last(const char* book);
int store_get_last(char* out, int len);
