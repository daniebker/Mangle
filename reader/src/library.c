#include "library.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "book.h"
#include "font.h"
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

// ---- shelf view ---------------------------------------------------------------

#define COLS 3
#define SCALE 2    // font scale: 16x26 px glyphs

typedef struct {
	int top, bottom;        // header height, footer start
	int cell_w, cell_h;     // one book slot (cover + caption + plank)
	int cover_w, cover_h;   // largest cover box inside a slot
	int caption_h;          // two text lines under the cover
	int rows, per;          // shelf rows per screen, books per screen
} Layout;

static Layout make_layout(const Screen* s)
{
	Layout L;
	int    line = FONT_H * SCALE;
	L.top       = line + 24;
	L.bottom    = s->height - (line + 24);
	L.cell_w    = s->width / COLS;
	L.cover_w   = L.cell_w - 24;
	L.cover_h   = L.cover_w * 3 / 2;
	L.caption_h = 2 * line + 8;
	L.cell_h    = L.cover_h + L.caption_h + 26;
	L.rows      = (L.bottom - L.top) / L.cell_h;
	if (L.rows < 1) {    // very small screen: shrink the covers to fit one row
		L.rows      = 1;
		L.cover_h   = L.bottom - L.top - L.caption_h - 26;
		if (L.cover_h < 40)
			L.cover_h = 40;
		L.cell_h = L.cover_h + L.caption_h + 26;
	}
	L.per = L.rows * COLS;
	return L;
}

// Box-filter `src` down (or nearest-neighbour up) to fit inside bw x bh.
static void scale_fit(const Image* src, int bw, int bh, Image* out)
{
	out->px = NULL;
	out->w = out->h = 0;
	if (!src->px || src->w < 1 || src->h < 1)
		return;
	int dw, dh;
	if ((long long) src->w * bh > (long long) src->h * bw) {
		dw = bw;
		dh = (int) ((long long) src->h * bw / src->w);
	} else {
		dh = bh;
		dw = (int) ((long long) src->w * bh / src->h);
	}
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;
	out->px = malloc((size_t) dw * dh);
	if (!out->px)
		return;
	out->w = dw, out->h = dh;
	for (int y = 0; y < dh; y++) {
		int y0 = (int) ((long long) y * src->h / dh);
		int y1 = (int) ((long long) (y + 1) * src->h / dh);
		if (y1 <= y0)
			y1 = y0 + 1;
		for (int x = 0; x < dw; x++) {
			int x0 = (int) ((long long) x * src->w / dw);
			int x1 = (int) ((long long) (x + 1) * src->w / dw);
			if (x1 <= x0)
				x1 = x0 + 1;
			unsigned sum = 0;
			for (int yy = y0; yy < y1; yy++)
				for (int xx = x0; xx < x1; xx++)
					sum += src->px[(size_t) yy * src->w + xx];
			out->px[(size_t) y * dw + x] = (uint8_t) (sum / (unsigned) ((y1 - y0) * (x1 - x0)));
		}
	}
}

// The first page of the book, scaled to fit the cover box. px is NULL on failure.
static void load_cover(const char* path, int bw, int bh, Image* out)
{
	out->px = NULL;
	Book* b = book_open(path, NULL, 0);
	if (!b)
		return;
	Image full;
	if (b->npages > 0 && book_load_image(b, b->pages[0].image, &full) == 0) {
		scale_fit(&full, bw, bh, out);
		image_free(&full);
	}
	book_close(b);
}

static void fill_rect(uint8_t* buf, int w, int h, int x, int y, int rw, int rh, uint8_t v)
{
	for (int j = y < 0 ? 0 : y; j < y + rh && j < h; j++)
		for (int i = x < 0 ? 0 : x; i < x + rw && i < w; i++)
			buf[(size_t) j * w + i] = v;
}

// Cut text to `maxpx` pixels wide, ending in "..." if needed.
static void fit_px(char* dst, size_t dstlen, const char* src, int maxpx)
{
	int cols = maxpx / (FONT_W * SCALE);
	fit(dst, dstlen, src, cols);
}

static void draw_centered(uint8_t* buf, const Screen* s, int x, int w, int y, const char* txt, uint8_t color)
{
	int tw = (int) strlen(txt) * FONT_W * SCALE;
	font_draw(buf, s->width, s->height, x + (w - tw) / 2, y, SCALE, txt, color);
}

static void draw_shelf(const Screen* s, const Layout* L, char** names, Image* covers, const char* state_dir,
                       int n, int page, int pages, uint8_t* buf, bool* flash)
{
	int W = s->width, H = s->height;
	memset(buf, 0xFF, (size_t) W * H);
	int line = FONT_H * SCALE;
	font_draw(buf, W, H, 12, 12, SCALE, "MANGLE", 0x00);
	{
		const char* ex = "[exit]";
		font_draw(buf, W, H, W - 12 - (int) strlen(ex) * FONT_W * SCALE, 12, SCALE, ex, 0x00);
	}
	fill_rect(buf, W, H, 0, L->top - 8, W, 2, 0x00);

	if (n <= 0) {
		draw_centered(buf, s, 0, W, L->top + 40, n < 0 ? "Cannot open folder" : "No .zip books found", 0x00);
		draw_centered(buf, s, 0, W, L->top + 40 + line + 8, "Make them with prep.py", 0x55);
	}
	for (int k = 0; k < L->per && page * L->per + k < n; k++) {
		int idx = page * L->per + k;
		int col = k % COLS, row = k / COLS;
		int cx = col * L->cell_w, cy = L->top + row * L->cell_h;
		int base = cy + L->cover_h;    // shelf plank top edge: covers stand on it
		const Image* c = &covers[idx];
		int cw = c->px ? c->w : L->cover_w * 2 / 3, ch = c->px ? c->h : L->cover_h * 2 / 3;
		int x = cx + (L->cell_w - cw) / 2, y = base - ch;
		// soft drop shadow, then the cover (or a blank book)
		fill_rect(buf, W, H, x + 6, y + 6, cw, ch, 0x99);
		if (c->px) {
			for (int j = 0; j < ch; j++)
				memcpy(buf + (size_t) (y + j) * W + x, c->px + (size_t) j * cw, (size_t) cw);
		} else {
			fill_rect(buf, W, H, x, y, cw, ch, 0xDD);
		}
		fill_rect(buf, W, H, x - 2, y - 2, cw + 4, 2, 0x00);
		fill_rect(buf, W, H, x - 2, y + ch, cw + 4, 2, 0x00);
		fill_rect(buf, W, H, x - 2, y - 2, 2, ch + 4, 0x00);
		fill_rect(buf, W, H, x + cw, y - 2, 2, ch + 4, 0x00);
		// plank
		fill_rect(buf, W, H, cx, base + 4, L->cell_w, 10, 0x55);
		fill_rect(buf, W, H, cx, base + 14, L->cell_w, 2, 0x00);

		// caption: title, then progress
		const char* name = names[idx];
		char        title[256], t2[256], prog[64] = "";
		snprintf(title, sizeof(title), "%.*s", (int) strlen(name) - 4, name);
		fit_px(t2, sizeof(t2), title, L->cell_w - 8);
		draw_centered(buf, s, cx, L->cell_w, base + 20, t2, 0x00);
		Saved sv;
		if (state_load(state_dir, name, &sv) == 0 && sv.npages > 0)
			snprintf(prog, sizeof(prog), "%d/%d", sv.pos.page + 1, sv.npages);
		else
			snprintf(prog, sizeof(prog), "new");
		draw_centered(buf, s, cx, L->cell_w, base + 20 + line + 2, prog, 0x55);
	}

	fill_rect(buf, W, H, 0, L->bottom, W, 2, 0x00);
	char nav[64];
	snprintf(nav, sizeof(nav), "%d / %d", page + 1, pages);
	draw_centered(buf, s, 0, W, L->bottom + 12, nav, 0x00);
	if (page > 0)
		font_draw(buf, W, H, 12, L->bottom + 12, SCALE, "< prev", 0x00);
	if (page + 1 < pages)
		font_draw(buf, W, H, W - 12 - 6 * FONT_W * SCALE, L->bottom + 12, SCALE, "next >", 0x00);
	(void) flash;
}

char* library_pick(const Screen* s, const char* dir, const char* state_dir, volatile sig_atomic_t* quit)
{
	char** names = NULL;
	int    n     = scan(dir, &names);
	Layout L     = make_layout(s);
	int    pages = n > 0 ? (n + L.per - 1) / L.per : 1;
	int    page  = 0;
	char*  chosen = NULL;
	Image* covers = calloc(n > 0 ? (size_t) n : 1, sizeof(Image));
	uint8_t* buf  = malloc((size_t) s->width * s->height);
	bool*   tried = calloc(n > 0 ? (size_t) n : 1, sizeof(bool));
	int     loaded_page = -1;

	bool flash = true;
	while (!*quit && buf && covers && tried) {
		if (loaded_page != page) {
			// Keep only the covers on this page in memory.
			for (int i = 0; i < n; i++) {
				if (i / L.per == page)
					continue;
				image_free(&covers[i]);
				covers[i].px = NULL;
				tried[i]     = false;
			}
			for (int k = 0; k < L.per && page * L.per + k < n; k++) {
				int i = page * L.per + k;
				if (tried[i])
					continue;
				char path[4096];
				snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
				load_cover(path, L.cover_w, L.cover_h, &covers[i]);
				tried[i] = true;
			}
			loaded_page = page;
		}
		draw_shelf(s, &L, names, covers, state_dir, n, page, pages, buf, &flash);
		display_frame(buf, flash);
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
		if (ev.y < L.top) {
			break;
		} else if (ev.kind == IN_LONG) {
			continue;
		} else if (ev.y >= L.bottom) {
			if (ev.x < s->width / 2 && page > 0)
				page--;
			else if (ev.x >= s->width / 2 && page + 1 < pages)
				page++;
		} else if (n > 0) {
			int col = ev.x / L.cell_w, row = (ev.y - L.top) / L.cell_h;
			int idx = page * L.per + row * COLS + col;
			if (col < COLS && row < L.rows && idx < n) {
				size_t len = strlen(dir) + strlen(names[idx]) + 2;
				chosen     = malloc(len);
				snprintf(chosen, len, "%s/%s", dir, names[idx]);
				break;
			}
		}
	}
	for (int i = 0; i < n; i++) {
		free(names[i]);
		if (covers)
			image_free(&covers[i]);
	}
	free(names);
	free(covers);
	free(tried);
	free(buf);
	return chosen;
}
