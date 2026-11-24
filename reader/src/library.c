#include "library.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "gesture.h"
#include "input.h"
#include "state.h"

int natcmp(const char* a, const char* b)
{
	while (*a && *b) {
		if (isdigit((unsigned char) *a) && isdigit((unsigned char) *b)) {
			while (*a == '0')
				a++;
			while (*b == '0')
				b++;
			size_t la = 0, lb = 0;
			while (isdigit((unsigned char) a[la]))
				la++;
			while (isdigit((unsigned char) b[lb]))
				lb++;
			if (la != lb)
				return la < lb ? -1 : 1;
			int c = strncmp(a, b, la);
			if (c)
				return c;
			a += la, b += lb;
			continue;
		}
		int ca = tolower((unsigned char) *a), cb = tolower((unsigned char) *b);
		if (ca != cb)
			return ca < cb ? -1 : 1;
		a++, b++;
	}
	return (unsigned char) *a - (unsigned char) *b;
}

static int cmp_names(const void* x, const void* y)
{
	return natcmp(*(char* const*) x, *(char* const*) y);
}

static bool is_zip(const char* name)
{
	size_t n = strlen(name);
	return n > 4 && name[0] != '.' && strcasecmp(name + n - 4, ".zip") == 0;
}

static int scan(const char* dir, char*** out)
{
	DIR* d = opendir(dir);
	if (!d)
		return -1;
	int            n = 0, cap = 0;
	char**         names = NULL;
	struct dirent* de;
	while ((de = readdir(d))) {
		if (!is_zip(de->d_name))
			continue;
		if (n == cap) {
			cap        = cap ? cap * 2 : 32;
			char** tmp = realloc(names, (size_t) cap * sizeof(char*));
			if (!tmp)
				break;
			names = tmp;
		}
		names[n++] = strdup(de->d_name);
	}
	closedir(d);
	qsort(names, (size_t) n, sizeof(char*), cmp_names);
	*out = names;
	return n;
}

// Fit text into `cols` columns, cutting with "..." if needed.
static void fit(char* dst, size_t dstlen, const char* src, int cols)
{
	if (cols < 4)
		cols = 4;
	if ((size_t) cols >= dstlen)
		cols = (int) dstlen - 1;
	size_t n = strlen(src);
	if ((int) n <= cols) {
		memcpy(dst, src, n + 1);
		return;
	}
	memcpy(dst, src, (size_t) cols - 3);
	memcpy(dst + cols - 3, "...", 4);
}

char* library_pick(const Screen* s, const char* dir, const char* state_dir, volatile sig_atomic_t* quit)
{
	char** names = NULL;
	int    n     = scan(dir, &names);
	int    rows  = s->rows < 8 ? 8 : s->rows;
	int    cols  = s->cols > 0 ? s->cols : 40;
	int    first = 2;                      // first item row
	int    per   = (rows - 4) / 2;         // items per screen (one blank row between items)
	int    pages = n > 0 ? (n + per - 1) / per : 1;
	int    page  = 0;
	char*  chosen = NULL;

	char** lines = calloc((size_t) rows, sizeof(char*));
	char*  store = calloc((size_t) rows, 256);
	for (int i = 0; i < rows; i++)
		lines[i] = store + i * 256;

	bool flash = true;
	while (!*quit) {
		for (int i = 0; i < rows; i++)
			lines[i][0] = 0;
		fit(lines[0], 256, "MANGLE                       [ tap here to exit ]", cols);
		if (n < 0) {
			snprintf(lines[first], 256, "Cannot open folder %s", dir);
		} else if (n == 0) {
			snprintf(lines[first], 256, "No .zip books in %s", dir);
			fit(lines[first + 1], 256, "Make them with prep.py and copy them there.", cols);
		}
		for (int k = 0; k < per && page * per + k < n; k++) {
			const char* name = names[page * per + k];
			char        label[512], title[256];
			snprintf(title, sizeof(title), "%.*s", (int) strlen(name) - 4, name);
			Saved sv;
			if (state_load(state_dir, name, &sv) == 0 && sv.npages > 0)
				snprintf(label, sizeof(label), "%s  [%d/%d]", title, sv.pos.page + 1, sv.npages);
			else
				snprintf(label, sizeof(label), "%s", title);
			fit(lines[first + 2 * k], 256, label, cols);
		}
		char nav[256];
		snprintf(nav, sizeof(nav), "< prev          %d/%d          next >", page + 1, pages);
		fit(lines[rows - 1], 256, nav, cols);
		display_text((const char* const*) lines, rows, flash);
		flash = false;

		InEvent ev;
		int     r = input_wait(&ev, -1);
		if (r <= 0)
			continue;
		if (ev.kind == IN_QUIT)
			break;
		if (ev.kind == IN_KEY) {
			Gesture g = { 0 };
			Action  a = gesture_key(&g, ev.code, false);
			if (a == ACT_EXIT)
				break;
			if (a == ACT_FORWARD && page + 1 < pages)
				page++;
			if (a == ACT_BACK && page > 0)
				page--;
			continue;
		}
		int row = ev.y / (s->row_height > 0 ? s->row_height : 1);
		if (ev.kind == IN_LONG && row == 0)
			break;
		if (row == 0) {
			break;
		} else if (row >= rows - 2) {
			if (ev.x < s->width / 2 && page > 0)
				page--;
			else if (ev.x >= s->width / 2 && page + 1 < pages)
				page++;
		} else if (row >= first && n > 0) {
			int k   = (row - first) / 2;
			int idx = page * per + k;
			if (k < per && idx < n) {
				size_t len = strlen(dir) + strlen(names[idx]) + 2;
				chosen     = malloc(len);
				snprintf(chosen, len, "%s/%s", dir, names[idx]);
				break;
			}
		}
	}
	for (int i = 0; i < n; i++)
		free(names[i]);
	free(names);
	free(store);
	free(lines);
	return chosen;
}
