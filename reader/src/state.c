#include "state.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// State file name: the book's file name with unsafe characters replaced.
// Keyed by name (not full path) so moving the library folder keeps progress.
static void state_path(const char* dir, const char* book, const char* ext, char* out, size_t n)
{
	const char* base = strrchr(book, '/');
	base             = base ? base + 1 : book;
	char   safe[256];
	size_t i = 0;
	for (; base[i] && i < sizeof(safe) - 1; i++) {
		char c  = base[i];
		safe[i] = (c == '/' || c == '\\' || c == '\n' || (unsigned char) c < 32) ? '_' : c;
	}
	safe[i] = 0;
	snprintf(out, n, "%s/%s%s", dir, safe, ext);
}

int state_load(const char* dir, const char* book, Saved* out)
{
	char path[4096];
	state_path(dir, book, ".pos", path, sizeof(path));
	FILE* f = fopen(path, "r");
	if (!f)
		return -1;
	int page, panel, mode, npages = 0;
	int n = fscanf(f, "%d %d %d %d", &page, &panel, &mode, &npages);
	fclose(f);
	if (n < 3)
		return -1;
	out->pos    = (Pos){ page, panel, mode ? MODE_PANEL : MODE_PAGE };
	out->npages = n == 4 ? npages : 0;
	return 0;
}

static int write_atomic(const char* path, const char* text)
{
	char tmp[4200];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "w");
	if (!f)
		return -1;
	int ok = fputs(text, f) >= 0;
	ok &= fflush(f) == 0;
	ok &= fclose(f) == 0;
	if (!ok || rename(tmp, path) != 0) {
		remove(tmp);
		return -1;
	}
	return 0;
}

int state_save(const char* dir, const char* book, const Saved* s)
{
	mkdir(dir, 0755);
	char path[4096], text[64];
	state_path(dir, book, ".pos", path, sizeof(path));
	snprintf(text, sizeof(text), "%d %d %d %d\n", s->pos.page, s->pos.panel, s->pos.mode == MODE_PANEL,
		 s->npages);
	return write_atomic(path, text);
}

int state_set_last(const char* dir, const char* book)
{
	mkdir(dir, 0755);
	char path[4096], text[4100];
	snprintf(path, sizeof(path), "%s/last", dir);
	snprintf(text, sizeof(text), "%s\n", book);
	return write_atomic(path, text);
}

int state_get_last(const char* dir, char* out, size_t n)
{
	char path[4096];
	snprintf(path, sizeof(path), "%s/last", dir);
	FILE* f = fopen(path, "r");
	if (!f)
		return -1;
	char* r = fgets(out, (int) n, f);
	fclose(f);
	if (!r)
		return -1;
	out[strcspn(out, "\n")] = 0;
	return out[0] ? 0 : -1;
}
