// Per-book resume state: one small text file per book in a state folder.
#pragma once

#include "nav.h"

typedef struct {
	Pos pos;
	int npages;    // page count when saved (shown in the library)
} Saved;

// Load the saved position for a book file. Returns 0 if found.
int state_load(const char* dir, const char* book_path, Saved* out);
// Save atomically (write temp file, rename).
int state_save(const char* dir, const char* book_path, const Saved* s);

// Remember / recall the last opened book (for "Resume last book").
int state_set_last(const char* dir, const char* book_path);
int state_get_last(const char* dir, char* out, size_t outlen);
