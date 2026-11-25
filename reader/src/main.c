// Mangle: panel-by-panel manga reader for jailbroken Kindles.
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "book.h"
#include "cache.h"
#include "display.h"
#include "font.h"
#include "gesture.h"
#include "input.h"
#include "library.h"
#include "light.h"
#include "nav.h"
#include "settings.h"
#include "state.h"
#include "stats.h"
#include "stats_view.h"
#include "zoom.h"

static volatile sig_atomic_t quit;

static void on_quit(int sig)
{
	(void) sig;
	quit = 1;
}

static void on_crash(int sig)
{
	// The kernel drops grabs when we die anyway; releasing them first is
	// just belt and braces. The launch script resumes the Kindle UI.
	input_release_grabs();
	signal(sig, SIG_DFL);
	raise(sig);
}

static void setup_signals(void)
{
	struct sigaction sa = { 0 };
	sa.sa_handler       = on_quit;    // no SA_RESTART: let poll() return EINTR
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	int crash[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL };
	for (size_t i = 0; i < sizeof(crash) / sizeof(*crash); i++)
		signal(crash[i], on_crash);
}

typedef struct {
	Screen      scr;
	const char* state_dir;
	int         flash_every;    // full refresh every N turns
	bool        verbose;
	bool        library;    // started with a library folder
	Settings    settings;
	int         idle_ms;    // inactivity before the sleep screen
	int         light_max;  // highest front light level, 0 = no front light
} App;

static void message(const char* a, const char* b)
{
	const char* lines[] = { NULL, NULL, a, NULL, b, NULL, "Tap anywhere to continue." };
	display_text(lines, 7, true);
	InEvent ev;
	while (!quit && input_wait(&ev, -1) != 1) {}
}

typedef struct {
	App*     app;
	Book*    book;
	Cache*   cache;
	uint8_t* frame;
	int      turns;
	StatRec  stats;
	Zoom     zm;          // pinch zoom of the current view; reset when moving on
	ZoomBase base;        // what the last show() fitted to the screen
	bool     have_base;
} Reader;

static void show(Reader* r, Pos p, bool force_flash)
{
	const char*  name = nav_image(r->book, p);
	const Image* img  = cache_get(r->cache, name);
	stats_page(&r->stats, p.page, time(NULL));
	if (r->app->verbose)
	{
		if (zoom_active(&r->zm))
			fprintf(stderr, "mangle: show %s page=%d panel=%d mode=%s zoom=%.2f\n", name, p.page, p.panel,
				p.mode == MODE_PANEL ? "panel" : "page", r->zm.z);
		else
			fprintf(stderr, "mangle: show %s page=%d panel=%d mode=%s\n", name, p.page, p.panel,
				p.mode == MODE_PANEL ? "panel" : "page");
	}
	if (!img) {
		const char* lines[] = { NULL, NULL, "Cannot load image:", name };
		r->have_base         = false;
		display_text(lines, 4, true);
	} else {
		const Screen* scr = &r->app->scr;
		int           rect[4];
		bool          panel = nav_rect(r->book, p, rect);
		if (!panel)
			rect[0] = rect[1] = 0, rect[2] = img->w, rect[3] = img->h;
		r->base      = zoom_base(rect, panel, scr->width, scr->height);
		r->have_base = true;
		zoom_clamp(&r->zm, &r->base);
		if (!zoom_active(&r->zm) || compose_view(scr, img, &r->base, &r->zm, r->frame) != 0) {
			if (panel)
				compose_rect(scr, img, rect, r->frame);
			else
				compose(scr, img, r->frame);
		}
		r->turns++;
		bool flash = force_flash || (r->app->flash_every > 0 && r->turns % r->app->flash_every == 0);
		display_frame(r->frame, flash);
	}

	// Prefetch where the reader is likely to go next: forward first, then back.
	const char* want[3] = { NULL, NULL, NULL };
	Pos         q       = p;
	if (nav_next(r->book, &q))
		want[0] = nav_image(r->book, q);
	q = p;
	if (nav_prev(r->book, &q))
		want[1] = nav_image(r->book, q);
	cache_prefetch(r->cache, want, 2);
}

static void fill_rows(uint8_t* f, const Screen* s, int y0, int y1, uint8_t v)
{
	for (int y = y0; y < y1 && y < s->height; y++)
		memset(f + (size_t) y * s->width, v, s->width);
}

static int ui_scale(const Screen* s)
{
	return s->width >= 900 ? 3 : 2;
}

// Draw an outlined box.
static void draw_box(uint8_t* f, const Screen* s, int x0, int y0, int x1, int y1, int t)
{
	for (int y = y0; y < y1 && y < s->height; y++) {
		if (y < 0)
			continue;
		uint8_t* row = f + (size_t) y * s->width;
		if (y < y0 + t || y >= y1 - t) {
			for (int x = x0 < 0 ? 0 : x0; x < x1 && x < s->width; x++)
				row[x] = 0;
		} else {
			for (int x = x0 < 0 ? 0 : x0; x < x0 + t && x < s->width; x++)
				row[x] = 0;
			for (int x = x1 - t < 0 ? 0 : x1 - t; x < x1 && x < s->width; x++)
				row[x] = 0;
		}
	}
}

// Set the front light, keeping *level in step. Returns true if it changed.
static bool change_light(App* app, int* level, int want)
{
	if (want < 0)
		want = 0;
	if (want > app->light_max)
		want = app->light_max;
	if (want == *level)
		return false;
	if (light_set(want) != 0) {
		fprintf(stderr, "mangle: cannot set the front light to %d\n", want);
		return false;
	}
	if (app->verbose)
		fprintf(stderr, "mangle: light %d\n", want);
	*level = want;
	return true;
}

// Settings screen. Returns true if the program should quit.
static bool settings_screen(Reader* r, Pos* pos)
{
	App*          app   = r->app;
	const Screen* s     = &app->scr;
	int           scale = ui_scale(s);
	int           row_h = FONT_H * scale + 2 * scale * 8;
	uint8_t*      f     = r->frame;
	int           pad   = scale * 8;
	// Front light: a row with the level and [-] [+] buttons, then a bar
	// that sets the level where it's tapped.
	int  light  = app->light_max > 0 ? light_get() : -1;
	int  btn_w  = row_h * 3 / 2;
	int  plus_x = s->width - pad - btn_w;
	int  minus_x = plus_x - pad - btn_w;
	int  bar_x0 = pad, bar_x1 = s->width - pad;
	bool flash  = true;
	if (light > app->light_max)
		light = app->light_max;

	for (;;) {
		fill_rows(f, s, 0, s->height, 255);
		font_draw(f, s->width, s->height, pad, scale * 8, scale, "< Back", 0);
		fill_rows(f, s, row_h - 3, row_h, 0);
		char line[64];
		snprintf(line, sizeof(line), "Power off after %d min idle: %s", app->idle_ms / 60000 ? app->idle_ms / 60000 : 1,
			 app->settings.sleep ? "ON" : "OFF");
		font_draw(f, s->width, s->height, pad, row_h + scale * 8, scale, line, 0);
		fill_rows(f, s, 2 * row_h - 1, 2 * row_h, 128);
		snprintf(line, sizeof(line), "Panel mode page preview: %s", app->settings.overview ? "ON" : "OFF");
		font_draw(f, s->width, s->height, pad, 2 * row_h + scale * 8, scale, line, 0);
		fill_rows(f, s, 3 * row_h - 1, 3 * row_h, 128);
		if (app->light_max <= 0) {
			font_draw(f, s->width, s->height, pad, 3 * row_h + scale * 8, scale, "Front light: not available", 0);
			fill_rows(f, s, 4 * row_h - 1, 4 * row_h, 128);
		} else {
			if (light >= 0)
				snprintf(line, sizeof(line), "Front light: %d/%d", light, app->light_max);
			else
				snprintf(line, sizeof(line), "Front light: ?");
			font_draw(f, s->width, s->height, pad, 3 * row_h + scale * 8, scale, line, 0);
			int by0 = 3 * row_h + scale * 3, by1 = 4 * row_h - scale * 3, ty = 3 * row_h + scale * 8;
			draw_box(f, s, minus_x, by0, minus_x + btn_w, by1, scale);
			draw_box(f, s, plus_x, by0, plus_x + btn_w, by1, scale);
			font_draw(f, s->width, s->height, minus_x + (btn_w - FONT_W * scale) / 2, ty, scale, "-", 0);
			font_draw(f, s->width, s->height, plus_x + (btn_w - FONT_W * scale) / 2, ty, scale, "+", 0);
			// Level bar: outline, filled up to the current level.
			int y0 = 4 * row_h + row_h / 4, y1 = 5 * row_h - row_h / 4;
			draw_box(f, s, bar_x0, y0, bar_x1, y1, scale);
			if (light > 0) {
				int fill = bar_x0 + (int) ((int64_t) (bar_x1 - bar_x0) * light / app->light_max);
				for (int y = y0; y < y1; y++)
					memset(f + (size_t) y * s->width + bar_x0, 0, (size_t) (fill - bar_x0));
			}
			fill_rows(f, s, 5 * row_h - 1, 5 * row_h, 128);
		}
		display_frame(f, flash);
		flash = true;

		for (;;) {
			InEvent ev;
			if (quit)
				return true;
			int rc = input_wait(&ev, -1);
			if (rc <= 0)
				continue;
			if (ev.kind == IN_QUIT)
				return true;
			if (ev.kind == IN_KEY) {
				Gesture g = { 0 };
				if (gesture_key(&g, ev.code, false) == ACT_EXIT)
					return false;
				continue;
			}
			if (ev.kind != IN_TAP)
				continue;
			if (ev.y < row_h)
				return false;
			if (ev.y < 2 * row_h) {
				app->settings.sleep = !app->settings.sleep;
				if (settings_save(app->state_dir, &app->settings) != 0)
					fprintf(stderr, "mangle: cannot save settings: %s\n", strerror(errno));
				break;
			}
			if (ev.y < 3 * row_h) {
				app->settings.overview = !app->settings.overview;
				nav_set_overview(app->settings.overview);
				nav_clamp(r->book, pos);    // the overview may no longer exist here
				if (settings_save(app->state_dir, &app->settings) != 0)
					fprintf(stderr, "mangle: cannot save settings: %s\n", strerror(errno));
				break;
			}
			if (app->light_max <= 0)
				continue;
			// Light changes redraw with a fast partial refresh.
			int cur = light < 0 ? 0 : light;
			if (ev.y < 4 * row_h) {
				int want = ev.x >= plus_x ? cur + 1 : ev.x >= minus_x ? cur - 1 : cur;
				if (change_light(app, &light, want)) {
					flash = false;
					break;
				}
				continue;
			}
			if (ev.y < 5 * row_h && ev.x >= bar_x0 - pad && ev.x < bar_x1 + pad) {
				int x    = ev.x < bar_x0 ? bar_x0 : ev.x > bar_x1 ? bar_x1 : ev.x;
				int want = (int) (((int64_t) (x - bar_x0) * app->light_max * 2 + (bar_x1 - bar_x0)) /
						  (2 * (bar_x1 - bar_x0)));
				if (change_light(app, &light, want)) {
					flash = false;
					break;
				}
			}
		}
	}
}

// Menu bar across the top of the current page. Returns true if the reader
// should go back to the library (or quit, when there is no library).
static bool menu(Reader* r, Pos* pos)
{
	const Screen* s     = &r->app->scr;
	int           scale = ui_scale(s);
	int           bar_h = FONT_H * scale + 2 * scale * 6;
	int           cw    = FONT_W * scale;
	uint8_t*      f     = r->frame;

	const char* lib   = r->app->library ? "< Library" : "< Exit";
	const char* set   = "Settings";
	const char* sta   = "Stats";
	int         lib_x = cw, set_x = lib_x + (int) strlen(lib) * cw + 4 * cw;
	int         sta_x   = set_x + (int) strlen(set) * cw + 4 * cw;
	int         sta_end = sta_x + (int) strlen(sta) * cw + 2 * cw;

	fill_rows(f, s, 0, bar_h, 255);
	fill_rows(f, s, bar_h - 3, bar_h, 0);
	font_draw(f, s->width, s->height, lib_x, scale * 6, scale, lib, 0);
	font_draw(f, s->width, s->height, set_x, scale * 6, scale, set, 0);
	font_draw(f, s->width, s->height, sta_x, scale * 6, scale, sta, 0);
	display_frame(f, false);

	bool leave = false;
	while (!quit) {
		InEvent ev;
		int     rc = input_wait(&ev, -1);
		if (rc <= 0)
			continue;
		if (ev.kind == IN_QUIT) {
			leave = true;
			break;
		}
		if (ev.kind == IN_TAP) {
			if (ev.y < bar_h && ev.x >= set_x - 2 * cw && ev.x < sta_x - 2 * cw) {
				leave = settings_screen(r, pos);
				break;
			}
			if (ev.y < bar_h && ev.x >= sta_x - 2 * cw && ev.x < sta_end) {
				leave = stats_screen(s, r->app->state_dir, &r->stats, f, &quit);
				break;
			}
			leave = ev.y < bar_h && ev.x < set_x - 2 * cw;
			break;
		}
		if (ev.kind == IN_KEY) {
			Gesture g = { 0 };
			leave     = gesture_key(&g, ev.code, false) == ACT_EXIT;
			break;
		}
	}
	if (!leave)
		show(r, *pos, true);    // repaint the page over the bar
	return leave;
}

// Is this image (almost) a blank page?
static bool mostly_white(const Image* img)
{
	size_t n = (size_t) img->w * img->h, dark = 0;
	for (size_t i = 0; i < n; i++)
		dark += img->px[i] < 200;
	return dark * 100 < n;    // under 1% ink
}

// Put the book's cover on screen (page 0, or page 1 if page 0 is blank), then
// suspend the device until it is woken. If suspending isn't possible, wait for
// any touch or key instead. Returns true if the program should quit.
static bool sleep_screen(Reader* r)
{
	Book* b = r->book;
	Image img;
	bool  ok = b->npages > 0 && book_load_image(b, b->pages[0].image, &img) == 0;
	if (ok && mostly_white(&img) && b->npages > 1) {
		Image second;
		if (book_load_image(b, b->pages[1].image, &second) == 0) {
			image_free(&img);
			img = second;
		}
	}
	if (ok) {
		compose(&r->app->scr, &img, r->frame);
		image_free(&img);
		display_frame(r->frame, true);
	}
	if (r->app->verbose)
		fprintf(stderr, "mangle: sleep\n");
	if (display_suspend() == 0)
		return quit;
	fprintf(stderr, "mangle: cannot suspend: %s\n", strerror(errno));
	while (!quit) {
		InEvent ev;
		int     rc = input_wait(&ev, -1);
		if (rc <= 0)
			continue;
		return ev.kind == IN_QUIT;
	}
	return true;
}

static void save(Reader* r, Pos p)
{
	Saved s = { p, r->book->npages };
	if (state_save(r->app->state_dir, r->book->path, &s) != 0)
		fprintf(stderr, "mangle: cannot save state in %s: %s\n", r->app->state_dir, strerror(errno));
}

static void read_book(App* app, const char* path)
{
	char  err[256] = "";
	Book* b        = book_open(path, err, sizeof(err));
	if (!b) {
		message("Cannot open book:", err);
		return;
	}
	fprintf(stderr, "mangle: %s: %d pages, %s\n", b->title, b->npages, b->rtl ? "rtl" : "ltr");
	if (b->screen_w && (b->screen_w != app->scr.width || b->screen_h != app->scr.height))
		fprintf(stderr, "mangle: book made for %dx%d, screen is %dx%d\n", b->screen_w, b->screen_h,
			app->scr.width, app->scr.height);

	Reader r = { app, b, cache_new(b), malloc((size_t) app->scr.width * app->scr.height), 0, { .seen = NULL },
		     { 1.0, 0, 0 }, { 0 }, false };
	if (!r.cache || !r.frame) {
		fprintf(stderr, "mangle: out of memory\n");
		goto out;
	}

	Pos   pos = nav_start(b);
	Saved sv;
	if (state_load(app->state_dir, path, &sv) == 0)
		pos = sv.pos;
	nav_clamp(b, &pos);
	state_set_last(app->state_dir, path);
	stats_begin(&r.stats, app->state_dir, path, b->npages, pos.page, time(NULL));
	show(&r, pos, true);

	Gesture g        = { 0 };
	int64_t last_act = now_ms();
	while (!quit) {
		InEvent ev;
		int     wait = gesture_timeout(&g, now_ms());
		if (app->settings.sleep && app->idle_ms > 0) {
			int64_t left = last_act + app->idle_ms - now_ms();
			int     idle = left > 0 ? (int) left : 0;
			if (wait < 0 || idle < wait)
				wait = idle;
		}
		int rc = input_wait(&ev, wait);
		Action  acts[2];
		int     nact = 0;
		if (rc < 0)
			continue;
		if (rc == 0) {
			acts[nact] = gesture_expire(&g, now_ms());
			nact += acts[nact] != ACT_NONE;
			if (nact == 0 && app->settings.sleep && app->idle_ms > 0 && now_ms() - last_act >= app->idle_ms) {
				g.pending = false;
				stats_pause(&r.stats);    // asleep: not reading
				if (sleep_screen(&r))
					break;
				show(&r, pos, true);
				last_act = now_ms();
				continue;
			}
		} else if (ev.kind == IN_QUIT) {
			break;
		} else if (ev.kind == IN_TAP) {
			nact = gesture_tap(&g, ev.x, ev.y, app->scr.width, app->scr.height, b->rtl, now_ms(), acts);
		} else if (ev.kind == IN_LONG) {
			acts[nact++] = gesture_long(&g, ev.x, app->scr.width);
		} else if (ev.kind == IN_KEY) {
			acts[nact++] = gesture_key(&g, ev.code, b->rtl);
		} else if (ev.kind == IN_PINCH) {
			// Zoom around the fingers. Drops a pending centre tap: it was
			// part of the same gesture, not a page turn.
			g.pending = false;
			if (r.have_base &&
			    zoom_pinch(&r.zm, &r.base, ev.x, ev.y, ev.x + ev.dx, ev.y + ev.dy, ev.scale))
				show(&r, pos, false);
		} else if (ev.kind == IN_DRAG && zoom_active(&r.zm)) {
			g.pending = false;
			if (r.have_base && zoom_pan(&r.zm, &r.base, ev.dx, ev.dy))
				show(&r, pos, false);
		}

		if (rc > 0) {
			last_act = now_ms();
			stats_activity(&r.stats, time(NULL));
		}
		bool done = false;
		for (int i = 0; i < nact && !done; i++) {
			Pos old = pos;
			switch (acts[i]) {
				case ACT_FORWARD:
					if (nav_next(b, &pos)) {
						zoom_reset(&r.zm);
						show(&r, pos, false);
					}
					break;
				case ACT_BACK:
					if (nav_prev(b, &pos)) {
						zoom_reset(&r.zm);
						show(&r, pos, false);
					}
					break;
				case ACT_TOGGLE:
					// Zoomed: the double tap zooms back out instead.
					if (!zoom_active(&r.zm))
						nav_toggle(b, &pos);
					zoom_reset(&r.zm);
					show(&r, pos, true);
					break;
				case ACT_REFRESH:
					show(&r, pos, true);
					break;
				case ACT_EXIT:
					done = true;
					break;
				case ACT_MENU:
					done = menu(&r, &pos);
					break;
				case ACT_NONE:
					break;
			}
			if (memcmp(&old, &pos, sizeof(pos)) != 0)
				save(&r, pos);
		}
		if (done)
			break;
	}
	save(&r, pos);
	stats_end(&r.stats);
out:
	cache_free(r.cache);
	free(r.frame);
	book_close(b);
}

static void usage(const char* argv0)
{
	fprintf(stderr,
		"usage: %s [options] [BOOK.zip]\n"
		"  -l DIR   library folder: show a book list (and return to it after reading)\n"
		"  -s DIR   folder for resume state (default: ./state)\n"
		"  -r       reopen the last book read\n"
		"  -t SEC   sleep screen (book cover) after SEC idle seconds (default 120)\n"
		"  -f N     full (flashing) refresh every N page turns, 0 = never (default 8)\n"
		"  -O       panel mode: no page preview unless the Settings menu turns it on\n"
		"  -v       log every image shown\n",
		argv0);
}

int main(int argc, char** argv)
{
	bool        overview = true;    // -O turns the default off; the saved setting wins
	App         app     = { .state_dir = "state", .flash_every = 8, .idle_ms = 120000, .settings = { .sleep = true } };
	const char* library = NULL;
	bool        resume  = false;
	int         c;
	while ((c = getopt(argc, argv, "l:s:rf:t:Ovh")) != -1) {
		switch (c) {
			case 'l':
				library = optarg;
				break;
			case 's':
				app.state_dir = optarg;
				break;
			case 'r':
				resume = true;
				break;
			case 'f':
				app.flash_every = atoi(optarg);
				break;
			case 'O':
				overview = false;
				break;
			case 't':
				app.idle_ms = atoi(optarg) * 1000;
				break;
			case 'v':
				app.verbose = true;
				break;
			default:
				usage(argv[0]);
				return c == 'h' ? 0 : 2;
		}
	}
	app.library = library != NULL;
	const char* book = optind < argc ? argv[optind] : NULL;
	if (!book && !library && !resume) {
		usage(argv[0]);
		return 2;
	}

	setup_signals();
	mkdir(app.state_dir, 0755);
	app.settings.overview = overview;
	settings_load(app.state_dir, &app.settings);
	nav_set_overview(app.settings.overview);
	if (display_open(&app.scr) != 0)
		return 1;
	app.light_max = light_open();
	if (input_open(app.scr.width, app.scr.height) != 0) {
		display_close();
		return 1;
	}

	char last[4096];
	if (!book && resume && state_get_last(app.state_dir, last, sizeof(last)) == 0 && access(last, R_OK) == 0)
		book = last;

	while (!quit) {
		char* picked = NULL;
		if (!book) {
			if (!library)
				break;
			picked = library_pick(&app.scr, library, app.state_dir, &quit);
			if (!picked)
				break;
			book = picked;
		}
		read_book(&app, book);
		free(picked);
		book = NULL;
		if (!library)
			break;
	}

	input_close();
	display_close();
	return 0;
}
