// A preprocessed book: ZIP with manifest.json and grayscale PNGs.
// Shared by every Mangle build (Kindle, CYD firmware, desktop simulator).
#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zipf.h"

typedef struct {
	int      w, h;
	uint8_t* px;    // 8-bit grayscale, w*h bytes
} Image;

// A panel is a rectangle of the page image (manifest v2). Version 1 books
// stored each panel as its own image instead.
typedef struct {
	char* image;       // own image name, or NULL: use the page image
	int   x, y, w, h;  // rectangle of the page image (when image is NULL)
} Panel;

typedef struct {
	char*  image;      // full-page image name
	int    npanels;
	Panel* panels;     // in reading order
} Page;

typedef struct {
	char*           path;
	char*           title;
	bool            rtl;
	int             screen_w, screen_h;    // what the preprocessor targeted (0 if absent)
	int             npages;
	Page*           pages;
	Zip             zip;
	pthread_mutex_t lock;    // one reader of the ZIP at a time (the prefetch thread shares it)
} Book;

// Returns NULL and fills err (if given) on failure.
Book* book_open(const char* path, char* err, size_t errlen);
void  book_close(Book* b);

// Decode a whole image into 8-bit gray (thread-safe). Returns 0 on success;
// on failure img->px is NULL.
int  book_load_image(Book* b, const char* name, Image* img);
void image_free(Image* img);

// Parse a manifest from a byte source (exposed for tests). Fills b's pages,
// title, direction and screen size.
typedef int (*JsonRead)(void* ctx, uint8_t* buf, int n);
int  book_parse_manifest(Book* b, JsonRead rd, void* ctx, char* err, size_t errlen);
void book_free_pages(Book* b);
