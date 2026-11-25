// Opening books on the CYD: the ZIP index plus the manifest, parsed while it is
// read (no JSON tree in memory). Fills the reader's Book struct, so the shared
// navigation code (nav.c) works unchanged.
#pragma once

#include <stddef.h>

#include "book.h"
#include "zipf.h"

typedef struct {
	Book b;
	Zip  zip;
} CBook;

// Returns NULL and fills err on failure.
CBook* cbook_open(const char* path, char* err, size_t errlen);
void   cbook_close(CBook* cb);

// Parse a manifest from a byte source (exposed for tests).
typedef int (*JsonRead)(void* ctx, uint8_t* buf, int n);
int cbook_parse_manifest(Book* b, JsonRead rd, void* ctx, char* err, size_t errlen);
void cbook_free_pages(Book* b);
