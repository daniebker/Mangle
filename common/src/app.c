#include "app.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "book.h"
#include "cache.h"
#include "font.h"
#include "gesture.h"
#include "hal.h"
#include "nav.h"
#include "render.h"
#include "stats.h"
#include "stats_view.h"
#include "ui.h"
#include "zoom.h"

#define SWIPE_PX 40    // horizontal drag that turns the page (or the shelf)

static const AppConfig* cfg;
static Settings         set;
static bool             quit;

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

static const Sink screen = { lcd_row, lcd_fill, NULL };

// Text scale for the user interface, and the height of bars and rows.
static int sc(void)
{
	return hal_width() >= 900 ? 3 : 2;
}

static int bar_h(void)
{
	return FONT_H * sc() + 6 * sc();
}

static void apply_display(void)
{
	hal_set_rotation(set.rotation);
	hal_set_night(set.night);
	if (hal_has_invert())
		hal_set_invert(set.invert);
	if (hal_has_backlight())
		hal_backlight(set.brightness);
}

static int idle_ms(void)
{
	if (set.sleep_min <= 0)
		return -1;
	return cfg->idle_ms > 0 ? cfg->idle_ms : set.sleep_min * 60000;
}

static void save_settings(void)
{
	if (store_save_settings(&set) != 0)
		hal_log("cannot save settings in %s\n", store_dir());
}

// Called just before sleeping, e.g. to put the book's cover on screen.
typedef void (*SleepHook)(void* ctx);

// Wait for an event, sleeping after the idle time. Returns 1 with an event,
// 0 on timeout (only when timeout_ms >= 0), and 2 after waking from sleep (the
// screen must be redrawn).
// Sleep now: the hook first (e.g. the cover on screen), then the device.
static void go_to_sleep(SleepHook hook, void* hctx, bool by_button)
{
	hal_log("sleep%s\n", by_button ? " (button)" : "");
	if (hook)
		hook(hctx);
	if (!hal_sleep(by_button)) {
		// Can't sleep here: wait for any input instead.
		InEvent ev;
		while (!quit && hal_wait(&ev, -1) != 1) {}
		if (ev.kind == IN_QUIT)
			quit = true;
	}
}

static int wait_event(InEvent* ev, int timeout_ms, SleepHook hook, void* hctx)
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
		if (rc == 1 && ev->kind == IN_SLEEP) {
			go_to_sleep(hook, hctx, true);
			return 2;
		}
		if (rc == 1) {
			if (ev->kind == IN_QUIT)
				quit = true;
			return 1;
		}
		if (idle >= 0 && hal_ms() - start >= idle) {
			go_to_sleep(hook, hctx, false);
			return 2;
		}
		if (timeout_ms >= 0 && hal_ms() - start >= timeout_ms)
			return 0;
	}
}

static void message(const char* a, const char* b)
{
	const char* lines[] = { a, "", b, "", "", "Tap to continue." };
	hal_log("%s %s\n", a, b);
	for (;;) {
		ui_message(lines, 6);
		hal_present(true);
		InEvent ev;
		int     rc;
		do {
			rc = wait_event(&ev, -1, NULL, NULL);
		} while (!quit && rc == 1 && ev.kind != IN_TAP && ev.kind != IN_LONG && ev.kind != IN_KEY);
		if (quit || rc == 1)
			return;
	}
}

// White bar with a line under it (top) or over it (bottom).
static void bar(int y, int h, bool top)
{
	int sw = hal_width();
	hal_fill(0, y, sw, h, UI_WHITE);
	hal_fill(0, top ? y + h - 3 : y, sw, top ? 3 : 2, UI_BLACK);
}

// A row of text buttons on a bar: hit areas are [x0[i], x1[i]).
typedef struct {
	int         n;
	const char* label[4];
	int         x0[4], x1[4];
} Buttons;

// Lay out buttons right-aligned (or from `left` when right is false).
static void buttons_layout(Buttons* b, int left, bool right)
{
	int s = sc(), gap = 2 * FONT_W * s, w = 0;
	for (int i = 0; i < b->n; i++)
		w += ui_text_width(b->label[i], s) + (i ? gap : 0);
	int x = right ? hal_width() - FONT_W * s - w : left;
	for (int i = 0; i < b->n; i++) {
		int tw   = ui_text_width(b->label[i], s);
		b->x0[i] = x - gap / 2, b->x1[i] = x + tw + gap / 2;
		x += tw + gap;
	}
}

static void buttons_draw(const Buttons* b, int y)
{
	int s = sc(), gap = 2 * FONT_W * s;
	for (int i = 0; i < b->n; i++)
		ui_text(b->x0[i] + gap / 2, y, s, b->label[i], UI_BLACK, UI_WHITE);
}

static int buttons_hit(const Buttons* b, int x)
{
	for (int i = 0; i < b->n; i++)
		if (x >= b->x0[i] && x < b->x1[i])
			return i;
	return -1;
}

static int buttons_left(const Buttons* b)
{
	return b->n ? b->x0[0] : hal_width();
}

// ---- stats screen ------------------------------------------------------------------

static void stats_screen(const StatRec* live)
{
	if (!hal_clock_ok()) {
		message("Reading statistics need the date and time.",
			"On the CYD, put wifi.txt (network name, password) in the state folder on the SD card.");
		return;
	}
	StatsView v;
	stats_view_open(&v, store_dir(), live, time(NULL));
	bool full = true;
	while (!quit) {
		stats_view_draw(&v);
		hal_present(full);
		full = false;
		InEvent  ev;
		SvResult r = SV_NONE;
		while (!quit && r == SV_NONE) {
			int rc = wait_event(&ev, -1, NULL, NULL);
			if (rc == 2) {
				r = SV_TAB;
				break;
			}
			if (rc != 1 || quit)
				continue;
			if (ev.kind == IN_TAP)
				r = stats_view_tap(&v, ev.x, ev.y);
			else if (ev.kind == IN_LONG)
				r = SV_CLOSE;
			else if (ev.kind == IN_KEY) {
				Gesture g = { 0 };
				Action  a = gesture_key(&g, ev.code, false);
				r = a == ACT_EXIT ? SV_CLOSE : a == ACT_FORWARD ? stats_view_step(&v, 1)
						 : a == ACT_BACK ? stats_view_step(&v, -1) : SV_NONE;
			}
		}
		if (r == SV_CLOSE)
			break;
		full = r == SV_TAB;
	}
	stats_view_close(&v);
}

// ---- settings ----------------------------------------------------------------------

enum { S_BACK, S_OVERVIEW, S_SLEEP, S_BRIGHT, S_ROT, S_NIGHT, S_INVERT, S_CALIB, S_COUNT };

static bool setting_shown(int i)
{
	switch (i) {
		case S_BRIGHT: return hal_has_backlight();
		case S_INVERT: return hal_has_invert();
		case S_CALIB: return hal_can_calibrate();
		default: return true;
	}
}

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

// Change setting i. The book and position (may be NULL) are kept valid.
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
		case S_CALIB: hal_calibrate(); return;
	}
	save_settings();
}

static void settings_screen(const Book* b, Pos* pos)
{
	int rows[S_COUNT], n = 0;
	for (int i = 0; i < S_COUNT; i++)
		if (setting_shown(i))
			rows[n++] = i;
	bool full = true;
	while (!quit) {
		int sw = hal_width(), sh = hal_height(), s = sc(), bh = bar_h();
		int row_h = (sh - bh) / (n - 1);
		if (row_h > FONT_H * s + 2 * s * 8)
			row_h = FONT_H * s + 2 * s * 8;
		hal_fill(0, 0, sw, sh, UI_WHITE);
		for (int k = 0; k < n; k++) {
			char label[48];
			setting_label(rows[k], label, sizeof(label));
			int y = k == 0 ? 0 : bh + (k - 1) * row_h;
			int h = k == 0 ? bh : row_h;
			ui_text_fit(FONT_W * s, y + (h - FONT_H * s) / 2, sw - 2 * FONT_W * s, s, label, UI_BLACK, UI_WHITE, 0);
			hal_fill(0, y + h - (k == 0 ? 3 : 1), sw, k == 0 ? 3 : 1, k == 0 ? UI_BLACK : UI_GRAY);
		}
		hal_present(full);
		full = false;

		InEvent ev;
		int     rc;
		do {
			rc = wait_event(&ev, -1, NULL, NULL);
		} while (!quit && rc == 1 && ev.kind != IN_TAP && ev.kind != IN_KEY);
		if (quit)
			return;
		if (rc == 2) {
			full = true;
			continue;
		}
		if (ev.kind == IN_KEY) {
			Gesture g = { 0 };
			if (gesture_key(&g, ev.code, false) == ACT_EXIT)
				return;
			continue;
		}
		if (ev.y < bh)
			return;
		int k = 1 + (ev.y - bh) / row_h;
		if (k < n) {
			setting_tap(rows[k], b, pos);
			full = rows[k] == S_ROT || rows[k] == S_NIGHT || rows[k] == S_INVERT || rows[k] == S_CALIB;
		}
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

int app_natcmp(const char* a, const char* b)
{
	return natcmp(a, b);
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

// The shelf: covers standing on planks, title and progress under each.
typedef struct {
	int cols, rows, per;
	int top, bottom;          // header height, footer start
	int cell_w, cell_h;
	int cover_w, cover_h;     // largest cover box in a cell
	int cs;                   // caption text scale
	Buttons head;             // Stats, Settings, Exit
	int     b_stats, b_settings, b_exit;
} Shelf;

static Shelf shelf_layout(void)
{
	Shelf L;
	int   sw = hal_width(), sh = hal_height(), s = sc();
	L.cs     = s - 1;
	L.top    = bar_h();
	L.bottom = sh - bar_h();
	L.cols   = sw / 160;
	if (L.cols < 1)
		L.cols = 1;
	if (L.cols > 3)
		L.cols = 3;
	L.cell_w      = sw / L.cols;
	L.cover_w     = L.cell_w - 8 * s;
	int caption   = 2 * (FONT_H * L.cs + 3) + 6 * s;
	int natural   = L.cover_w * 3 / 2 + caption;
	int avail     = L.bottom - L.top;
	L.rows        = (avail + natural / 2) / natural;
	if (L.rows < 1)
		L.rows = 1;
	L.cell_h  = avail / L.rows;
	L.cover_h = L.cell_h - caption;
	if (L.cover_h > L.cover_w * 3 / 2)
		L.cover_h = L.cover_w * 3 / 2;
	L.per = L.cols * L.rows;

	L.head.n = 0;
	L.b_stats = L.head.n, L.head.label[L.head.n++] = "Stats";
	L.b_settings = L.head.n, L.head.label[L.head.n++] = "Settings";
	L.b_exit = -1;
	if (hal_can_exit())
		L.b_exit = L.head.n, L.head.label[L.head.n++] = "Exit";
	buttons_layout(&L.head, 0, true);
	return L;
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

// The cover of a book (page 1) as a thumbnail of at most w x h, from the
// cache or rendered now. Fills *tw/*th with its size (0 on failure).
static uint8_t* cover(const char* dir, const char* name, int w, int h, int* tw, int* th, char* title, int tlen)
{
	char        path[320];
	struct stat st;
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	long     stamp = stat(path, &st) == 0 ? (long) st.st_size : 0;
	uint8_t* px    = malloc((size_t) w * h);
	*tw = *th = 0;
	if (!px)
		return NULL;
	// Cached thumbnails are stored at the box size, picture centred on white.
	if (store_load_thumb(name, stamp, w, h, px, title, tlen) == 0) {
		*tw = w, *th = h;
		return px;
	}
	char  err[96];
	Book* b = book_open(path, err, sizeof(err));
	if (!b) {
		hal_log("%s: %s\n", name, err);
		free(px);
		return NULL;
	}
	MemSink     m  = { px, 0, 0, w, h };
	Sink        s  = { mem_row, mem_fill, &m };
	FitLayout   fl = { 0, 0, w, h, true, 0, 0 };
	const char* e  = NULL;
	snprintf(title, (size_t) tlen, "%s", b->title);
	if (render_entry(&b->zip, b->pages[0].image, layout_fit, &fl, &s, UI_WHITE, &e) == 0) {
		*tw = w, *th = h;
		store_save_thumb(name, stamp, w, h, px, title);
	} else {
		hal_log("%s: cover: %s\n", name, e ? e : "?");
		free(px);
		px = NULL;
	}
	book_close(b);
	return px;
}

static void draw_cell(const Shelf* L, int slot, const char* name)
{
	int  s  = sc();
	int  cx = (slot % L->cols) * L->cell_w, cy = L->top + (slot / L->cols) * L->cell_h;
	int  base = cy + 4 * s + L->cover_h;    // the plank: covers stand on it
	char title[120] = "";
	int  tw, th;
	uint8_t* px = cover(cfg->library, name, L->cover_w, L->cover_h, &tw, &th, title, sizeof(title));
	int      x  = cx + (L->cell_w - L->cover_w) / 2;
	if (px) {
		// Trim the white around the picture (thumbnails keep the box size).
		int x0 = tw, x1 = 0, y0 = th, y1 = 0;
		for (int r = 0; r < th; r++)
			for (int c = 0; c < tw; c++)
				if (px[(size_t) r * tw + c] != UI_WHITE) {
					if (c < x0) x0 = c;
					if (c > x1) x1 = c;
					if (r < y0) y0 = r;
					if (r > y1) y1 = r;
				}
		if (x1 < x0)
			x0 = 0, x1 = tw - 1, y0 = 0, y1 = th - 1;
		int pw = x1 - x0 + 1, ph = y1 - y0 + 1;
		int dx = cx + (L->cell_w - pw) / 2, dy = base - ph;
		hal_fill(dx + 2 * s, dy + 2 * s, pw, ph, 0x99);    // drop shadow
		for (int r = 0; r < ph; r++)
			hal_row(dx, dy + r, pw, px + (size_t) (y0 + r) * tw + x0);
		hal_fill(dx - 1, dy - 1, pw + 2, 1, UI_BLACK);
		hal_fill(dx - 1, dy + ph, pw + 2, 1, UI_BLACK);
		hal_fill(dx - 1, dy - 1, 1, ph + 2, UI_BLACK);
		hal_fill(dx + pw, dy - 1, 1, ph + 2, UI_BLACK);
		free(px);
	} else {
		int bw = L->cover_w * 2 / 3, bh = L->cover_h * 2 / 3;
		hal_fill(x + (L->cover_w - bw) / 2, base - bh, bw, bh, 0xDD);
		ui_text_fit(x, base - bh / 2 - FONT_H, L->cover_w, s, "?", UI_BLACK, 0xDD, 1);
	}
	hal_fill(cx, base + 2, L->cell_w, 2 * s, 0x55);    // plank

	if (!title[0]) {    // file name without ".zip"
		snprintf(title, sizeof(title), "%s", name);
		size_t len = strlen(title);
		if (len > 4)
			title[len - 4] = 0;
	}
	int ty = base + 2 + 2 * s + 3;
	ui_text_fit(cx + 4, ty, L->cell_w - 8, L->cs, title, UI_BLACK, UI_WHITE, 1);
	Pos  p;
	int  np = 0;
	char prog[32];
	if (store_load_pos(name, &p, &np) == 0 && np > 0)
		snprintf(prog, sizeof(prog), "%d / %d", p.page + 1, np);
	else
		snprintf(prog, sizeof(prog), "new");
	ui_text_fit(cx + 4, ty + FONT_H * L->cs + 3, L->cell_w - 8, L->cs, prog, 0x55, UI_WHITE, 1);
}

static void draw_shelf(const Shelf* L, char** names, int n, int page, int pages, bool full)
{
	int sw = hal_width(), sh = hal_height(), s = sc();
	int ty = (L->top - FONT_H * s) / 2;
	hal_fill(0, 0, sw, sh, UI_WHITE);
	bar(0, L->top, true);
	if (ui_text_width("Mangle", s) + 2 * FONT_W * s < buttons_left(&L->head))
		ui_text(FONT_W * s, ty, s, "Mangle", UI_BLACK, UI_WHITE);
	buttons_draw(&L->head, ty);

	bar(L->bottom, sh - L->bottom, false);
	int fy = L->bottom + (sh - L->bottom - FONT_H * s) / 2;
	if (pages > 1) {
		bool wide = sw >= 500;
		char pg[32];
		snprintf(pg, sizeof(pg), "%d / %d", page + 1, pages);
		ui_text_fit(0, fy, sw, s, pg, UI_BLACK, UI_WHITE, 1);
		if (page > 0)
			ui_text(FONT_W * s, fy, s, wide ? "< prev" : "<", UI_BLACK, UI_WHITE);
		if (page + 1 < pages) {
			const char* nx = wide ? "next >" : ">";
			ui_text(sw - FONT_W * s - ui_text_width(nx, s), fy, s, nx, UI_BLACK, UI_WHITE);
		}
	}
	if (n <= 0) {
		char where[200];
		snprintf(where, sizeof(where), "Make books with prep.py and copy them to %s", cfg->library);
		ui_text_fit(0, sh / 2 - 2 * FONT_H * s, sw, s, n < 0 ? "Cannot open the folder" : "No books yet", UI_BLACK,
			    UI_WHITE, 1);
		ui_text_fit(0, sh / 2, sw, s - 1, where, 0x55, UI_WHITE, 1);
	}
	for (int i = 0; i < L->per && page * L->per + i < n; i++)
		draw_cell(L, i, names[page * L->per + i]);
	hal_present(full);
}

// Let the user pick a book. Returns its path (malloc'd), or NULL to exit.
static char* library_pick(void)
{
	char** names = NULL;
	int    n     = scan(cfg->library, &names);
	int    page  = 0;
	char*  picked = NULL;
	bool   full   = true, leave = false;
	while (!quit && !picked && !leave) {
		Shelf L     = shelf_layout();
		int   pages = n > 0 ? (n + L.per - 1) / L.per : 1;
		if (page >= pages)
			page = pages - 1;
		draw_shelf(&L, names, n, page, pages, full);
		full = false;
		for (;;) {
			InEvent ev;
			int     rc = wait_event(&ev, -1, NULL, NULL);
			if (quit)
				break;
			if (rc == 2) {
				full = true;
				break;
			}
			int sw = hal_width();
			if (ev.kind == IN_KEY) {
				Gesture g = { 0 };
				Action  a = gesture_key(&g, ev.code, false);
				if (a == ACT_EXIT && hal_can_exit()) {
					leave = true;
					break;
				}
				int to = a == ACT_FORWARD ? page + 1 : a == ACT_BACK ? page - 1 : page;
				if (to >= 0 && to < pages && to != page) {
					page = to;
					break;
				}
				continue;
			}
			if (ev.kind == IN_DRAG && abs(ev.dx) > SWIPE_PX && abs(ev.dx) > abs(ev.dy)) {
				int to = page + (ev.dx < 0 ? 1 : -1);
				if (to >= 0 && to < pages) {
					page = to;
					break;
				}
				continue;
			}
			if (ev.kind != IN_TAP)
				continue;
			if (ev.y < L.top) {
				int b = buttons_hit(&L.head, ev.x);
				if (b == L.b_stats) {
					stats_screen(NULL);
					full = true;
					break;
				}
				if (b == L.b_settings) {
					settings_screen(NULL, NULL);
					full = true;
					break;
				}
				if (b >= 0 && b == L.b_exit) {
					leave = true;
					break;
				}
				continue;
			}
			if (ev.y >= L.bottom) {
				int to = page + (ev.x >= sw / 2 ? 1 : -1);
				if (to >= 0 && to < pages) {
					page = to;
					break;
				}
				continue;
			}
			int col = ev.x / L.cell_w, row = (ev.y - L.top) / L.cell_h;
			int i   = page * L.per + row * L.cols + col;
			if (col < L.cols && row < L.rows && i < n) {
				char path[320];
				snprintf(path, sizeof(path), "%s/%s", cfg->library, names[i]);
				picked = strdup(path);
				break;
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
	Book*    book;
	Cache*   cache;     // NULL unless prefetching
	Pos      pos;
	Zoom     zm;
	ZoomBase base;
	bool     have_base;
	int      turns;
	bool     stats_on;
	StatRec  stats;
} Reader;

// Draw the current view. full = flashing refresh (e-ink).
static void show(Reader* r, bool full)
{
	Book*       b    = r->book;
	const char* name = nav_image(b, r->pos);
	ViewLayout  v    = { { 0, 0, 0, 0 }, false, hal_width(), hal_height(), &r->zm, &r->base };
	v.upscale        = nav_rect(b, r->pos, v.rect);
	if (r->stats_on)
		stats_page(&r->stats, r->pos.page, time(NULL));
	if (cfg->verbose) {
		if (zoom_active(&r->zm))
			hal_log("show %s page=%d panel=%d mode=%s zoom=%.2f\n", name, r->pos.page, r->pos.panel,
				r->pos.mode == MODE_PANEL ? "panel" : "page", r->zm.z);
		else
			hal_log("show %s page=%d panel=%d mode=%s\n", name, r->pos.page, r->pos.panel,
				r->pos.mode == MODE_PANEL ? "panel" : "page");
	}
	int64_t     t0  = hal_ms();
	const char* err = NULL;
	int         rc;
	if (r->cache) {
		const Image* img = cache_get(r->cache, name);
		rc               = img ? render_image(img, layout_view, &v, &screen, UI_WHITE, &err) : -1;
		if (!img)
			err = "cannot decode it";
	} else {
		rc = render_entry(&b->zip, name, layout_view, &v, &screen, UI_WHITE, &err);
	}
	r->have_base = rc == 0;
	if (rc != 0) {
		char what[300];
		snprintf(what, sizeof(what), "Cannot show %s:", name);
		const char* lines[] = { what, err ? err : "unknown error" };
		hal_log("%s %s\n", what, err ? err : "");
		ui_message(lines, 2);
		hal_present(true);
	} else {
		r->turns++;
		bool flash = full || (cfg->flash_every > 0 && r->turns % cfg->flash_every == 0);
		hal_present(flash);
		if (cfg->verbose)
			hal_log("drawn in %d ms\n", (int) (hal_ms() - t0));
	}

	if (r->cache) {
		// Prefetch where the reader is likely to go next: forward first, then back.
		const char* want[2] = { NULL, NULL };
		Pos         q       = r->pos;
		if (nav_next(b, &q))
			want[0] = nav_image(b, q);
		q = r->pos;
		if (nav_prev(b, &q))
			want[1] = nav_image(b, q);
		cache_prefetch(r->cache, want, 2);
	}
}

static void save(Reader* r)
{
	if (store_save_pos(r->book->path, r->pos, r->book->npages) != 0)
		hal_log("cannot save the position in %s\n", store_dir());
}

// Is this mostly a blank page?
static bool mostly_white(const uint8_t* px, int n)
{
	int dark = 0;
	for (int i = 0; i < n; i++)
		dark += px[i] < 200;
	return dark * 100 < n;
}

// Before sleeping: stop the session clock and show the cover (page 1, or
// page 2 if the first is blank), which stays on e-ink while asleep.
static void sleep_cover(void* ctx)
{
	Reader* r = ctx;
	if (r->stats_on)
		stats_pause(&r->stats);
	Book* b    = r->book;
	int   sw   = hal_width(), sh = hal_height();
	int   page = 0;
	// A small preview of the first page decides whether it is blank.
	enum { PW = 60, PH = 80 };
	uint8_t     prev[PW * PH];
	MemSink     m  = { prev, 0, 0, PW, PH };
	Sink        s  = { mem_row, mem_fill, &m };
	FitLayout   fp = { 0, 0, PW, PH, true, 0, 0 };
	const char* e;
	if (b->npages > 1 && render_entry(&b->zip, b->pages[0].image, layout_fit, &fp, &s, UI_WHITE, &e) == 0 &&
	    mostly_white(prev, PW * PH))
		page = 1;
	FitLayout fl = { 0, 0, sw, sh, false, 0, 0 };
	if (render_entry(&b->zip, b->pages[page].image, layout_fit, &fl, &screen, UI_WHITE, &e) == 0)
		hal_present(true);
}

// Menu bar over the top of the page. Returns true to close the book.
static bool menu(Reader* r)
{
	int     sw = hal_width(), s = sc(), bh = bar_h();
	Buttons m  = { 0 };
	m.label[m.n++] = cfg->library ? "< Library" : "< Exit";
	m.label[m.n++] = "Settings";
	m.label[m.n++] = "Stats";
	buttons_layout(&m, FONT_W * s, false);
	char pg[24];
	snprintf(pg, sizeof(pg), "%d/%d", r->pos.page + 1, r->book->npages);
	int ty = (bh - FONT_H * s) / 2;
	bar(0, bh, true);
	buttons_draw(&m, ty);
	int pw = ui_text_width(pg, s);
	if (m.x1[m.n - 1] + pw + FONT_W * s < sw)
		ui_text(sw - FONT_W * s - pw, ty, s, pg, 0x55, UI_WHITE);
	hal_present(false);

	bool close = false;
	for (;;) {
		InEvent ev;
		int     rc = wait_event(&ev, -1, sleep_cover, r);
		if (quit)
			return true;
		if (rc == 2)
			break;
		if (ev.kind == IN_KEY) {
			Gesture g = { 0 };
			close     = gesture_key(&g, ev.code, false) == ACT_EXIT;
			break;
		}
		if (ev.kind != IN_TAP && ev.kind != IN_LONG)
			continue;
		if (ev.kind == IN_TAP && ev.y < bh) {
			int b = buttons_hit(&m, ev.x);
			if (b == 0)
				close = true;
			else if (b == 1)
				settings_screen(r->book, &r->pos);
			else if (b == 2)
				stats_screen(r->stats_on ? &r->stats : NULL);
		}
		break;    // anything else closes the menu
	}
	if (!close && !quit)
		show(r, true);    // repaint the page over the bar
	return close || quit;
}

// Hold on a side: zoom 1x -> 2x -> 3x -> back to fitted, around the finger.
static void zoom_step(Reader* r, int x, int y)
{
	if (!r->have_base)
		return;
	bool full = false;
	if (zoom_active(&r->zm) && r->zm.z >= 2.9) {
		zoom_reset(&r->zm);
		full = true;    // back to the fitted view: clear e-ink ghosting too
	} else {
		double target = zoom_active(&r->zm) ? 3.0 : 2.0;
		double from   = zoom_active(&r->zm) ? r->zm.z : 1.0;
		zoom_pinch(&r->zm, &r->base, x, y, x, y, target / from);
	}
	show(r, full);
}

// Read a book until it is closed. Returns false if it couldn't be opened.
static bool read_book(const char* path)
{
	char  err[160];
	Book* b = book_open(path, err, sizeof(err));
	if (!b) {
		message("Cannot open book:", err);
		return false;
	}
	hal_log("%s: %d pages, %s\n", b->title, b->npages, b->rtl ? "rtl" : "ltr");
	if (b->screen_w && b->screen_w * b->screen_h > 4 * hal_width() * hal_height())
		hal_log("book made for %dx%d: pages take longer to draw\n", b->screen_w, b->screen_h);

	Reader r;
	memset(&r, 0, sizeof(r));
	r.book = b;
	zoom_reset(&r.zm);
	if (cfg->prefetch)
		r.cache = cache_new(b);
	int np;
	if (store_load_pos(path, &r.pos, &np) != 0)
		r.pos = nav_start(b);
	nav_clamp(b, &r.pos);
	store_set_last(path, true);
	r.stats_on = hal_clock_ok();
	if (r.stats_on)
		stats_begin(&r.stats, store_dir(), path, b->npages, r.pos.page, time(NULL));
	show(&r, true);

	Gesture g = { 0 };
	while (!quit) {
		InEvent ev = { 0 };
		Action  acts[2];
		int     nact = 0;
		int     rc   = wait_event(&ev, gesture_timeout(&g, hal_ms()), sleep_cover, &r);
		if (quit)
			break;
		if (rc == 2) {
			g.pending = false;
			show(&r, true);
			continue;
		}
		if (rc == 1 && r.stats_on)
			stats_activity(&r.stats, time(NULL));
		int sw = hal_width(), sh = hal_height();
		if (rc == 0) {
			acts[nact] = gesture_expire(&g, hal_ms());
			nact += acts[nact] != ACT_NONE;
		} else if (ev.kind == IN_TAP) {
			nact = gesture_tap(&g, ev.x, ev.y, sw, sh, b->rtl, hal_ms(), acts);
		} else if (ev.kind == IN_LONG) {
			acts[nact++] = gesture_long(&g, ev.x, sw);
		} else if (ev.kind == IN_KEY) {
			acts[nact++] = gesture_key(&g, ev.code, b->rtl);
		} else if (ev.kind == IN_PINCH) {
			// Drops a pending centre tap: it was part of the same gesture.
			g.pending = false;
			if (r.have_base && zoom_pinch(&r.zm, &r.base, ev.x, ev.y, ev.x + ev.dx, ev.y + ev.dy, ev.scale))
				show(&r, !zoom_active(&r.zm));
		} else if (ev.kind == IN_DRAG) {
			g.pending = false;
			if (zoom_active(&r.zm)) {
				if (r.have_base && zoom_pan(&r.zm, &r.base, ev.dx, ev.dy))
					show(&r, false);
			} else if (abs(ev.dx) > SWIPE_PX && abs(ev.dx) > abs(ev.dy)) {
				// Swipe: the page follows the finger, like paper. RTL books go
				// forward with a swipe to the right.
				acts[nact++] = (ev.dx > 0) == b->rtl ? ACT_FORWARD : ACT_BACK;
			}
		}

		bool done = false;
		for (int i = 0; i < nact && !done; i++) {
			Pos old = r.pos;
			switch (acts[i]) {
				case ACT_FORWARD:
					if (nav_next(b, &r.pos)) {
						zoom_reset(&r.zm);
						show(&r, false);
					}
					break;
				case ACT_BACK:
					if (nav_prev(b, &r.pos)) {
						zoom_reset(&r.zm);
						show(&r, false);
					}
					break;
				case ACT_TOGGLE:
					// Zoomed: the double tap zooms back out instead.
					if (!zoom_active(&r.zm))
						nav_toggle(b, &r.pos);
					zoom_reset(&r.zm);
					show(&r, true);
					break;
				case ACT_ZOOM: zoom_step(&r, ev.x, ev.y); break;
				case ACT_REFRESH: show(&r, true); break;
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
	if (r.stats_on)
		stats_end(&r.stats);
	// Closed by the user: the CYD starts in the library next time. Stopped
	// from outside (power off, signal): it reopens the book.
	store_set_last(path, quit);
	cache_free(r.cache);
	book_close(b);
	return true;
}

void app_run(const AppConfig* c)
{
	cfg  = c;
	quit = false;
	store_init(c->state);
	mkdir(c->state, 0755);
	set = c->defaults;
	store_load_settings(&set);
	nav_set_overview(set.overview);
	apply_display();
	if (hal_needs_calibration())
		hal_calibrate();

	char        last[320];
	bool        open = false;
	const char* first = c->book;
	if (!first && c->resume != RESUME_NONE && store_get_last(last, sizeof(last), &open) == 0 &&
	    (c->resume == RESUME_LAST || open)) {
		struct stat st;
		if (stat(last, &st) == 0)
			first = last;
	}
	if (first) {
		read_book(first);
		if (!c->library)
			return;
	}
	while (c->library && !quit) {
		char* path = library_pick();
		if (!path)
			break;
		read_book(path);
		free(path);
	}
}
