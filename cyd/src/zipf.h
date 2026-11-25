// Minimal ZIP reader for small-memory devices: central directory index plus a
// streaming reader for one entry at a time (stored or deflated).
//
// Unlike miniz's archive API it never holds a whole entry in memory, so a
// book's pages can be decoded straight from the SD card.
#pragma once

#include <stdint.h>
#include <stdio.h>

typedef struct {
	char*    name;
	uint32_t ofs;      // local header offset
	uint32_t csize;    // compressed size
	uint32_t usize;    // uncompressed size
	uint16_t method;   // 0 = stored, 8 = deflate
} ZipEntry;

typedef struct {
	FILE*     f;
	long      at;    // current file position, -1 if unknown (saves seeks on SD cards)
	int       n;
	ZipEntry* e;
} Zip;

int  zip_open(Zip* z, const char* path);    // 0 on success
void zip_close(Zip* z);
int  zip_find(const Zip* z, const char* name);    // entry index or -1

typedef struct ZipInflate ZipInflate;

typedef struct {
	Zip*        z;
	uint32_t    pos;       // file offset of the next compressed byte
	uint32_t    left;      // compressed bytes not yet read from the file
	uint32_t    out_left;  // uncompressed bytes not yet returned
	ZipInflate* inf;       // deflate state (NULL for stored entries)
} ZipStream;

int zs_open(ZipStream* s, Zip* z, int index);    // 0 on success
// Read up to n bytes. Returns the count, 0 at the end, -1 on error.
int  zs_read(ZipStream* s, uint8_t* buf, int n);
void zs_close(ZipStream* s);
