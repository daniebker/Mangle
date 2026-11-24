#include "cache.h"

#include <stdlib.h>
#include <string.h>

#define SLOTS 4    // current + next + previous + one spare
#define QMAX  4

typedef enum { E_EMPTY, E_LOADING, E_READY } State;

typedef struct {
	const char*   name;
	State         state;
	int           ok;
	Image         img;
	unsigned long used;
} Entry;

struct Cache {
	Book*           book;
	Entry           e[SLOTS];
	int             pinned;    // index of the image currently on screen, or -1
	const char*     queue[QMAX];
	int             qn;
	unsigned long   tick;
	bool            quit;
	pthread_t       thread;
	pthread_mutex_t mu;
	pthread_cond_t  work;    // queue changed / quit
	pthread_cond_t  done;    // an entry finished loading
};

static int find(Cache* c, const char* name)
{
	for (int i = 0; i < SLOTS; i++)
		if (c->e[i].state != E_EMPTY && strcmp(c->e[i].name, name) == 0)
			return i;
	return -1;
}

// Pick a slot to (re)use: empty first, else least recently used that's not
// pinned, loading, or wanted by the prefetch queue. -1 if none is free.
static int victim(Cache* c, bool protect_queue)
{
	int best = -1;
	for (int i = 0; i < SLOTS; i++) {
		Entry* e = &c->e[i];
		if (e->state == E_EMPTY)
			return i;
		if (i == c->pinned || e->state == E_LOADING)
			continue;
		if (protect_queue) {
			bool wanted = false;
			for (int q = 0; q < c->qn; q++)
				wanted |= strcmp(c->queue[q], e->name) == 0;
			if (wanted)
				continue;
		}
		if (best < 0 || e->used < c->e[best].used)
			best = i;
	}
	return best;
}

// Decode into slot i with the lock released. Called with the lock held.
static void load(Cache* c, int i, const char* name)
{
	Entry* e = &c->e[i];
	if (e->img.px)
		image_free(&e->img);
	e->name  = name;
	e->state = E_LOADING;
	e->used  = ++c->tick;
	pthread_mutex_unlock(&c->mu);
	Image img;
	int   ok = book_load_image(c->book, name, &img) == 0;
	pthread_mutex_lock(&c->mu);
	e->img   = img;
	e->ok    = ok;
	e->state = E_READY;
	pthread_cond_broadcast(&c->done);
}

static void* worker(void* arg)
{
	Cache* c = arg;
	pthread_mutex_lock(&c->mu);
	while (!c->quit) {
		const char* next = NULL;
		for (int q = 0; q < c->qn && !next; q++)
			if (find(c, c->queue[q]) < 0)
				next = c->queue[q];
		int slot = next ? victim(c, true) : -1;
		if (!next || slot < 0) {
			pthread_cond_wait(&c->work, &c->mu);
			continue;
		}
		load(c, slot, next);
	}
	pthread_mutex_unlock(&c->mu);
	return NULL;
}

Cache* cache_new(Book* b)
{
	Cache* c = calloc(1, sizeof(Cache));
	if (!c)
		return NULL;
	c->book   = b;
	c->pinned = -1;
	pthread_mutex_init(&c->mu, NULL);
	pthread_cond_init(&c->work, NULL);
	pthread_cond_init(&c->done, NULL);
	if (pthread_create(&c->thread, NULL, worker, c) != 0) {
		free(c);
		return NULL;
	}
	return c;
}

void cache_free(Cache* c)
{
	if (!c)
		return;
	pthread_mutex_lock(&c->mu);
	c->quit = true;
	pthread_cond_broadcast(&c->work);
	pthread_mutex_unlock(&c->mu);
	pthread_join(c->thread, NULL);
	for (int i = 0; i < SLOTS; i++)
		if (c->e[i].img.px)
			image_free(&c->e[i].img);
	pthread_cond_destroy(&c->work);
	pthread_cond_destroy(&c->done);
	pthread_mutex_destroy(&c->mu);
	free(c);
}

const Image* cache_get(Cache* c, const char* name)
{
	pthread_mutex_lock(&c->mu);
	c->pinned = -1;
	int i;
	for (;;) {
		i = find(c, name);
		if (i >= 0 && c->e[i].state == E_LOADING) {
			pthread_cond_wait(&c->done, &c->mu);    // prefetcher is on it
			continue;
		}
		if (i >= 0)
			break;
		i = victim(c, false);
		if (i < 0) {    // every slot is loading: wait for one
			pthread_cond_wait(&c->done, &c->mu);
			continue;
		}
		load(c, i, name);
		// The worker could have touched the slot while unlocked; re-check.
	}
	c->pinned    = i;
	c->e[i].used = ++c->tick;
	const Image* img = c->e[i].ok ? &c->e[i].img : NULL;
	pthread_cond_broadcast(&c->work);    // a slot may have become evictable
	pthread_mutex_unlock(&c->mu);
	return img;
}

void cache_prefetch(Cache* c, const char* const* names, int n)
{
	pthread_mutex_lock(&c->mu);
	c->qn = 0;
	for (int q = 0; q < n && q < QMAX; q++)
		if (names[q])
			c->queue[c->qn++] = names[q];
	pthread_cond_broadcast(&c->work);
	pthread_mutex_unlock(&c->mu);
}
