#include "app.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "cbook.h"
#include "font.h"
#include "gesture.h"
#include "hal.h"
#include "nav.h"
#include "render.h"
#include "store.h"
#include "ui.h"
#include "zoom.h"

#define SCALE 2                              // UI text scale
#define BAR_H (FONT_H * SCALE + 12)          // menu, library and settings bars
#define SWIPE_PX 40                          // horizontal drag that turns the page
#define COVER_MIN_W 140                      // library cell width it aims for
#define COVER_MIN_H 190

static CydSettings set;
static bool        quit;

// ---- small helpers ---------------------------------------------------------------

static void lcd_row(void* ctx, int x, int y, int w, const uint8_t* g)
{
	(void) ctx;
	hal_row(x, y, w, g);
}

static void lcd_fill(void* ctx, int x, int y, int w, int h, uint8_t v)
{
	(void) ctx;
	hal_fill(x, y, w, h, v);
}

static const Sink lcd = { lcd_row, lcd_fill, NULL };

static void apply_display(void)
{
	hal_set_rotation(set.rotation);
	hal_set_invert(set.invert);
	hal_set_night(set.night);
	hal_backlight(set.brightness);
}

static int idle_ms(void)
{
	return set.sleep_min > 0 ? set.sleep_min * 60000 : -1;
}

// Wait for an event, sleeping (screen off) after the idle time. Returns 1 with
// an event, 0 on timeout (only when timeout_ms >= 0), and 2 after waking from
// sleep (the screen must be redrawn).
static int wait_event(InEvent* ev, int timeout_ms)
{
	int64_t start = hal_ms();
	for (;;) {
		int idle = idle_ms(), wait = timeout_ms;
		if (idle >= 0) {
			int64_t left = start + idle - hal_ms();
			int     l    = left > 0 ? (int) left : 0;
			if (wait < 0 || l < wait)
				wait = l;
		}
		int rc = hal_wait(ev, wait);
		if (rc == 1) {
			if (ev->kind == IN_QUIT)
				quit = true;
			return 1;
		}
		if (idle >= 0 && hal_ms() - start >= idle) {
			hal_log("sleep\n");
			hal_sleep_until_touch();
			return 2;
		}
		if (timeout_ms >= 0)
			return 0;
	}
}

static void message(const char* a, const char* b)
{
	const char* lines[] = { a, "", b, "", "", "Tap to continue." };
	ui_message(lines, 6);
	InEvent ev;
	while (!quit) {
		int rc = wait_event(&ev, -1);
		if (rc == 2)
			ui_message(lines, 6);
		else if (rc == 1 && (ev.kind == IN_TAP || ev.kind == IN_LONG))
			break;
	}
}

static void bar(int y, int h)
{
	int sw = hal_width();
	hal_fill(0, y, sw, h, UI_WHITE);
	hal_fill(0, y == 0 ? h - 2 : y, sw, 2, UI_BLACK);
}

// ---- settings ----------------------------------------------------------------------

enum { S_BACK, S_OVERVIEW, S_SLEEP, S_BRIGHT, S_ROT, S_NIGHT, S_INVERT, S_CALIB, S_COUNT };

static void setting_label(int i, char* out, size_t n)
{
	static const char* rot[] = { "0", "90", "180", "270" };
	switch (i) {
		case S_BACK: snprintf(out, n, "< Back"); break;
		case S_OVERVIEW: snprintf(out, n, "Page preview: %s", set.overview ? "ON" : "OFF"); break;
		case S_SLEEP:
			if (set.sleep_min)
				snprintf(out, n, "Sleep: %d min", set.sleep_min);
			else
				snprintf(out, n, "Sleep: never");
			break;
		case S_BRIGHT: snprintf(out, n, "Brightness: %d%%", set.brightness); break;
		case S_ROT: snprintf(out, n, "Rotation: %s deg", rot[set.rotation & 3]); break;
		case S_NIGHT: snprintf(out, n, "Night mode: %s", set.night ? "ON" : "OFF"); break;
		case S_INVERT: snprintf(out, n, "Invert panel: %s", set.invert ? "ON" : "OFF"); break;
		case S_CALIB: snprintf(out, n, "Calibrate touch"); break;
	}
}

static int next_in(const int* v, int n, int cur)
{
	for (int i = 0; i < n; i++)
		if (v[i] > cur)
			return v[i];
	return v[0];
}

// Change the setting in row i. Book and position (may be NULL) are kept valid.
static void setting_tap(int i, const Book* b, Pos* pos)
{
	static const int sleeps[] = { 0, 2, 5, 10, 30 };
	static const int levels[] = { 10, 25, 50, 75, 100 };
	switch (i) {
		case S_OVERVIEW:
			set.overview = !set.overview;
			nav_set_overview(set.overview);
			if (b && pos)
				nav_clamp(b, pos);    // the overview may no longer exist here
			break;
		case S_SLEEP: set.sleep_min = next_in(sleeps, 5, set.sleep_min); break;
		case S_BRIGHT:
			set.brightness = next_in(levels, 5, set.brightness);
			hal_backlight(set.brightness);
			break;
		case S_ROT:
			set.rotation = (set.rotation + 1) & 3;
			hal_set_rotation(set.rotation);
			break;
		case S_NIGHT:
			set.night = !set.night;
			hal_set_night(set.night);
			break;
		case S_INVERT:
			set.invert = !set.invert;
			hal_set_invert(set.invert);
			break;
		case S_CALIB: hal_calibrate(); break;
	}
	if (i != S_CALIB && store_save_settings(&set) != 0)
		hal_log("cannot save settings\n");
}

static void settings_screen(const Book* b, Pos* pos)
{
	int rows = hal_multitouch() ? S_CALIB : S_COUNT;
	for (;;) {
		int sw = hal_width(), sh = hal_height();
		int row_h = (sh - BAR_H) / (rows - 1);
		if (row_h > BAR_H + 12)
			row_h = BAR_H + 12;
		hal_fill(0, 0, sw, sh, UI_WHITE);
		for (int i = 0; i < rows; i++) {
			char label[48];
			setting_label(i, label, sizeof(label));
			int y = i == 0 ? 0 : BAR_H + (i - 1) * row_h;
			int h = i == 0 ? BAR_H : row_h;
			ui_text_fit(8, y + (h - FONT_H * SCALE) / 2, sw - 16, SCALE, label, UI_BLACK, UI_WHITE, 0);
			hal_fill(0, y + h - (i == 0 ? 2 : 1), sw, i == 0 ? 2 : 1, i == 0 ? UI_BLACK : UI_GRAY);
		}
		hal_flush();

		InEvent ev;
		int     rc;
		do {
			rc = wait_event(&ev, -1);
		} while (!quit && rc == 1 && ev.kind != IN_TAP);
		if (quit)
			return;
		if (rc == 2)
			continue;
		if (ev.y < BAR_H)
			return;
		int i = 1 + (ev.y - BAR_H) / row_h;
		if (i < rows)
			setting_tap(i, b, pos);
	}
}

// ---- library -------------------------------------------------------------------------

static int natcmp(const char* a, const char* b)
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
	*out   = NULL;
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
			cap        = cap ? cap * 2 : 16;
			char** tmp = realloc(names, (size_t) cap * sizeof(char*));
			if (!tmp)
				break;
			names = tmp;
		}
		names[n] = strdup(de->d_name);
		if (names[n])
			n++;
	}
	closedir(d);
	if (n)
		qsort(names, (size_t) n, sizeof(char*), cmp_names);
	*out = names;
	return n;
}

typedef struct {
	int cols, rows, cell_w, cell_h, top;
	int cover_w, cover_h;
} Grid;

static Grid grid(void)
{
	Grid g;
	int  sw = hal_width(), sh = hal_height();
	g.top    = BAR_H;
	g.cols   = sw / COVER_MIN_W > 0 ? sw / COVER_MIN_W : 1;
	g.rows   = (sh - 2 * BAR_H) / COVER_MIN_H > 0 ? (sh - 2 * BAR_H) / COVER_MIN_H : 1;
	g.cell_w = sw / g.cols;
	g.cell_h = (sh - 2 * BAR_H) / g.rows;
	g.cover_w = g.cell_w - 16;
	g.cover_h = g.cell_h - 2 * (FONT_H + 3) - 12;
	return g;
}

typedef struct {
	uint8_t* px;
	int      x0, y0, w, h;
} MemSink;

static void mem_row(void* ctx, int x, int y, int w, const uint8_t* g)
{
	MemSink* m = ctx;
	y -= m->y0, x -= m->x0;
	if (y < 0 || y >= m->h)
		return;
	if (x < 0)
		g -= x, w += x, x = 0;
	if (x + w > m->w)
		w = m->w - x;
	if (w > 0)
		memcpy(m->px + (size_t) y * m->w + x, g, (size_t) w);
}

static void mem_fill(void* ctx, int x, int y, int w, int h, uint8_t v)
{
	MemSink* m = ctx;
	for (int r = y; r < y + h; r++) {
		int yy = r - m->y0, xx = x - m->x0, ww = w;
		if (yy < 0 || yy >= m->h)
			continue;
		if (xx < 0)
			ww += xx, xx = 0;
		if (xx + ww > m->w)
			ww = m->w - xx;
		if (ww > 0)
			memset(m->px + (size_t) yy * m->w + xx, v, (size_t) ww);
	}
}

// Draw one library cell: cover, title and progress.
static void draw_cell(const Grid* g, int slot, const char* dir, const char* name)
{
	int  sw = hal_width();
	int  cx = (slot % g->cols) * g->cell_w, cy = g->top + (slot / g->cols) * g->cell_h;
	int  x = cx + (g->cell_w - g->cover_w) / 2, y = cy + 6;
	char path[300], title[120] = "";
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	(void) sw;

	size_t   n  = (size_t) g->cover_w * g->cover_h;
	uint8_t* px = n ? malloc(n) : NULL;
	bool     ok = false;
	if (px && store_load_thumb(name, g->cover_w, g->cover_h, px, title, sizeof(title)) == 0) {
		ok = true;
	} else if (px) {
		char   err[96];
		CBook* cb = cbook_open(path, err, sizeof(err));
		if (cb) {
			MemSink     m  = { px, 0, 0, g->cover_w, g->cover_h };
			Sink        s  = { mem_row, mem_fill, &m };
			FitLayout   fl = { 0, 0, g->cover_w, g->cover_h, true, 0, 0 };
			const char* e  = NULL;
			snprintf(title, sizeof(title), "%s", cb->b.title);
			if (render_entry(&cb->zip, cb->b.pages[0].image, layout_fit, &fl, &s, UI_WHITE, &e) == 0) {
				ok = true;
				store_save_thumb(name, g->cover_w, g->cover_h, px, title);
			} else {
				hal_log("%s: cover: %s\n", name, e ? e : "?");
			}
			cbook_close(cb);
		} else {
			hal_log("%s: %s\n", name, err);
		}
	}
	if (ok) {
		for (int r = 0; r < g->cover_h; r++)
			hal_row(x, y + r, g->cover_w, px + (size_t) r * g->cover_w);
	} else {
		hal_fill(x, y, g->cover_w, g->cover_h, 220);
		ui_text_fit(x, y + g->cover_h / 2 - FONT_H, g->cover_w, SCALE, "?", UI_BLACK, 220, 1);
	}
	free(px);

	if (!title[0]) {    // file name without ".zip"
		snprintf(title, sizeof(title), "%s", name);
		size_t len = strlen(title);
		if (len > 4)
			title[len - 4] = 0;
	}
	int ty = y + g->cover_h + 4;
	ui_text_fit(cx + 4, ty, g->cell_w - 8, 1, title, UI_BLACK, UI_WHITE, 1);

	Pos  p;
	int  np = 0;
	char prog[32];
	if (store_load_pos(name, &p, &np) == 0 && np > 0)
		snprintf(prog, sizeof(prog), "%d / %d", p.page + 1, np);
	else
		snprintf(prog, sizeof(prog), "new");
	ui_text_fit(cx + 4, ty + FONT_H + 3, g->cell_w - 8, 1, prog, UI_GRAY, UI_WHITE, 1);
}

static void draw_shelf(char** names, int n, int page, int pages)
{
	int  sw = hal_width(), sh = hal_height();
	Grid g  = grid();
	hal_fill(0, 0, sw, sh, UI_WHITE);
	bar(0, BAR_H);
	ui_text(8, 6, SCALE, "Mangle", UI_BLACK, UI_WHITE);
	const char* st = "Settings";
	ui_text(sw - 8 - ui_text_width(st, SCALE), 6, SCALE, st, UI_BLACK, UI_WHITE);
	if (pages > 1) {
		char pg[24];
		bar(sh - BAR_H, BAR_H);
		snprintf(pg, sizeof(pg), "%d/%d", page + 1, pages);
		int ty = sh - BAR_H + 8;
		ui_text(8, ty, SCALE, "<", UI_BLACK, UI_WHITE);
		ui_text_fit(0, ty, sw, SCALE, pg, UI_BLACK, UI_WHITE, 1);
		ui_text(sw - 8 - FONT_W * SCALE, ty, SCALE, ">", UI_BLACK, UI_WHITE);
	}
	if (n == 0) {
		char where[96];
		snprintf(where, sizeof(where), "Copy books made with prep.py into %s", hal_library_dir());
		ui_text_fit(0, sh / 2 - 40, sw, SCALE, "No books yet.", UI_BLACK, UI_WHITE, 1);
		ui_text_fit(0, sh / 2, sw, 1, where, UI_BLACK, UI_WHITE, 1);
		hal_flush();
		return;
	}
	hal_flush();
	int per = g.cols * g.rows;
	for (int i = 0; i < per && page * per + i < n; i++) {
		draw_cell(&g, i, hal_library_dir(), names[page * per + i]);
		hal_flush();
	}
}

// Let the user pick a book. Returns its path (malloc'd), or NULL to quit.
static char* library_pick(void)
{
	char** names = NULL;
	int    n     = scan(hal_library_dir(), &names);
	if (n < 0) {
		mkdir(hal_library_dir(), 0755);
		n = 0;
	}
	int   page   = 0;
	char* picked = NULL;
	while (!quit && !picked) {
		Grid g     = grid();
		int  per   = g.cols * g.rows;
		int  pages = n ? (n + per - 1) / per : 1;
		if (page >= pages)
			page = pages - 1;
		draw_shelf(names, n, page, pages);
		for (;;) {
			InEvent ev;
			int     rc = wait_event(&ev, -1);
			if (quit)
				break;
			if (rc == 2)
				break;    // woke up: redraw
			if (ev.kind == IN_DRAG && pages > 1 && abs(ev.dx) > SWIPE_PX && abs(ev.dx) > abs(ev.dy)) {
				page = (page + (ev.dx < 0 ? 1 : pages - 1)) % pages;
				break;
			}
			if (ev.kind != IN_TAP)
				continue;
			int sw = hal_width(), sh = hal_height();
			if (ev.y < BAR_H) {
				if (ev.x >= sw / 2) {
					settings_screen(NULL, NULL);
					break;
				}
				continue;
			}
			if (ev.y >= sh - BAR_H && pages > 1) {
				page = (page + (ev.x >= sw / 2 ? 1 : pages - 1)) % pages;
				break;
			}
			int col = ev.x / g.cell_w, row = (ev.y - g.top) / g.cell_h;
			if (col < g.cols && row < g.rows) {
				int i = page * per + row * g.cols + col;
				if (i < n) {
					char path[300];
					snprintf(path, sizeof(path), "%s/%s", hal_library_dir(), names[i]);
					picked = strdup(path);
					break;
				}
			}
		}
	}
	for (int i = 0; i < n; i++)
		free(names[i]);
	free(names);
	return picked;
}

// ---- reading -------------------------------------------------------------------------

typedef struct {
	CBook*   cb;
	Pos      pos;
	Zoom     zm;
	ZoomBase base;
	bool     have_base;
} Reader;

static void show(Reader* r)
{
	Book*       b    = &r->cb->b;
	const char* name = nav_image(b, r->pos);
	ViewLayout  v    = { { 0, 0, 0, 0 }, false, hal_width(), hal_height(), &r->zm, &r->base };
	v.upscale        = nav_rect(b, r->pos, v.rect);
	if (zoom_active(&r->zm))
		hal_log("show %s page=%d panel=%d mode=%s zoom=%.2f\n", name, r->pos.page, r->pos.panel,
			r->pos.mode == MODE_PANEL ? "panel" : "page", r->zm.z);
	else
		hal_log("show %s page=%d panel=%d mode=%s\n", name, r->pos.page, r->pos.panel,
			r->pos.mode == MODE_PANEL ? "panel" : "page");
	int64_t     t0  = hal_ms();
	const char* err = NULL;
	if (render_entry(&r->cb->zip, name, layout_view, &v, &lcd, UI_WHITE, &err) != 0) {
		char what[80];
		snprintf(what, sizeof(what), "Cannot show %s:", name);
		const char* lines[] = { what, err ? err : "unknown error" };
		ui_message(lines, 2);
		r->have_base = false;
		return;
	}
	hal_flush();
	r->have_base = true;
	hal_log("drawn in %d ms\n", (int) (hal_ms() - t0));
}

static void save(Reader* r)
{
	if (store_save_pos(r->cb->b.path, r->pos, r->cb->b.npages) != 0)
		hal_log("cannot save position\n");
}

// Menu bar over the top of the page. Returns true to go back to the library.
static bool menu(Reader* r)
{
	int         sw = hal_width();
	const char* lib = "< Library", *st = "Settings";
	char        pg[24];
	snprintf(pg, sizeof(pg), "%d/%d", r->pos.page + 1, r->cb->b.npages);
	int set_x = 8 + ui_text_width(lib, SCALE) + 24;
	bar(0, BAR_H);
	ui_text(8, 6, SCALE, lib, UI_BLACK, UI_WHITE);
	ui_text(set_x, 6, SCALE, st, UI_BLACK, UI_WHITE);
	int pw = ui_text_width(pg, SCALE);
	if (set_x + ui_text_width(st, SCALE) + 16 + pw < sw)
		ui_text(sw - 8 - pw, 6, SCALE, pg, UI_GRAY, UI_WHITE);
	hal_flush();

	for (;;) {
		InEvent ev;
		int     rc = wait_event(&ev, -1);
		if (quit)
			return true;
		if (rc == 2)
			break;
		if (ev.kind != IN_TAP && ev.kind != IN_LONG)
			continue;
		if (ev.kind == IN_TAP && ev.y < BAR_H) {
			if (ev.x < set_x - 12)
				return true;
			if (ev.x < set_x + ui_text_width(st, SCALE) + 12) {
				settings_screen(&r->cb->b, &r->pos);
				break;
			}
		}
		break;    // anywhere else closes the menu
	}
	if (!quit)
		show(r);
	return quit;
}

// Hold on a side: step the zoom 1x -> 2x -> 3x -> 1x around the finger.
static void zoom_step(Reader* r, int x, int y)
{
	if (!r->have_base)
		return;
	if (zoom_active(&r->zm) && r->zm.z >= 2.9) {
		zoom_reset(&r->zm);
	} else {
		double target = zoom_active(&r->zm) ? 3.0 : 2.0;
		double from   = zoom_active(&r->zm) ? r->zm.z : 1.0;
		zoom_pinch(&r->zm, &r->base, x, y, x, y, target / from);
	}
	show(r);
}

static void read_book(const char* path)
{
	char   err[160];
	CBook* cb = cbook_open(path, err, sizeof(err));
	if (!cb) {
		message("Cannot open book:", err);
		return;
	}
	Book* b = &cb->b;
	hal_log("%s: %d pages, %s\n", b->title, b->npages, b->rtl ? "rtl" : "ltr");
	if (b->screen_w && b->screen_w * b->screen_h > 4 * hal_width() * hal_height())
		hal_log("book made for %dx%d: pages will be slow to draw\n", b->screen_w, b->screen_h);

	Reader r = { cb, nav_start(b), { 1.0, 0, 0 }, { 0 }, false };
	int    np;
	if (store_load_pos(path, &r.pos, &np) != 0)
		r.pos = nav_start(b);
	nav_clamp(b, &r.pos);
	store_set_last(path);
	show(&r);

	Gesture g = { 0 };
	while (!quit) {
		InEvent ev;
		Action  acts[2];
		int     nact = 0;
		int     rc   = wait_event(&ev, gesture_timeout(&g, hal_ms()));
		if (quit)
			break;
		if (rc == 2) {
			g.pending = false;
			show(&r);
			continue;
		}
		int sw = hal_width(), sh = hal_height();
		if (rc == 0) {
			acts[nact] = gesture_expire(&g, hal_ms());
			nact += acts[nact] != ACT_NONE;
		} else if (ev.kind == IN_TAP) {
			nact = gesture_tap(&g, ev.x, ev.y, sw, sh, b->rtl, hal_ms(), acts);
		} else if (ev.kind == IN_LONG) {
			g.pending = false;
			if (gesture_zone(ev.x, sw) == ZONE_CENTRE)
				acts[nact++] = ACT_EXIT;
			else
				zoom_step(&r, ev.x, ev.y);
		} else if (ev.kind == IN_PINCH) {
			g.pending = false;
			if (r.have_base && zoom_pinch(&r.zm, &r.base, ev.x, ev.y, ev.x + ev.dx, ev.y + ev.dy, ev.scale))
				show(&r);
		} else if (ev.kind == IN_DRAG) {
			g.pending = false;
			if (zoom_active(&r.zm)) {
				if (r.have_base && zoom_pan(&r.zm, &r.base, ev.dx, ev.dy))
					show(&r);
			} else if (abs(ev.dx) > SWIPE_PX && abs(ev.dx) > abs(ev.dy)) {
				// Swipe: the page follows the finger, like turning paper.
				// RTL books go forward with a swipe to the right.
				bool fwd     = (ev.dx > 0) == b->rtl;
				acts[nact++] = fwd ? ACT_FORWARD : ACT_BACK;
			}
		} else if (ev.kind == IN_KEY) {
			acts[nact++] = gesture_key(&g, ev.code, b->rtl);
		}

		bool done = false;
		for (int i = 0; i < nact && !done; i++) {
			Pos old = r.pos;
			switch (acts[i]) {
				case ACT_FORWARD:
					if (nav_next(b, &r.pos)) {
						zoom_reset(&r.zm);
						show(&r);
					}
					break;
				case ACT_BACK:
					if (nav_prev(b, &r.pos)) {
						zoom_reset(&r.zm);
						show(&r);
					}
					break;
				case ACT_TOGGLE:
					// Zoomed: the double tap zooms back out instead.
					if (!zoom_active(&r.zm))
						nav_toggle(b, &r.pos);
					zoom_reset(&r.zm);
					show(&r);
					break;
				case ACT_REFRESH: show(&r); break;
				case ACT_EXIT: done = true; break;
				case ACT_MENU: done = menu(&r); break;
				case ACT_NONE: break;
			}
			if (memcmp(&old, &r.pos, sizeof(old)) != 0)
				save(&r);
		}
		if (done)
			break;
	}
	save(&r);
	if (!quit)
		store_set_last("");    // closed on purpose: start in the library next time
	cbook_close(cb);
}

void app_run(void)
{
	store_defaults(&set);
	store_load_settings(&set);
	nav_set_overview(set.overview);
	apply_display();
	if (hal_needs_calibration())
		hal_calibrate();

	// Power-on carries on with the book that was open.
	char last[300];
	if (store_get_last(last, sizeof(last)) == 0) {
		struct stat st;
		if (stat(last, &st) == 0)
			read_book(last);
	}
	while (!quit) {
		char* path = library_pick();
		if (!path)
			break;
		read_book(path);
		free(path);
	}
}
