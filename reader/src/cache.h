// Decoded-image cache with a background prefetch thread.
#pragma once

#include "book.h"

typedef struct Cache Cache;

Cache* cache_new(Book* b);
void   cache_free(Cache* c);

// Decoded image for `name`, decoding it now if the prefetcher doesn't have it.
// The image stays valid until the next cache_get call. NULL on decode error.
const Image* cache_get(Cache* c, const char* name);

// Replace the prefetch queue (first = most wanted). Names must stay valid
// while the book is open (they point into the Book's manifest).
void cache_prefetch(Cache* c, const char* const* names, int n);
