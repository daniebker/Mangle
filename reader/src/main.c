// Mangle: panel-by-panel manga reader for jailbroken Kindles.
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "book.h"
#include "cache.h"
#include "display.h"
#include "gesture.h"
#include "input.h"
#include "library.h"
#include "nav.h"
#include "state.h"

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
} Reader;

static void show(Reader* r, Pos p, bool force_flash)
{
	const char*  name = nav_image(r->book, p);
	const Image* img  = cache_get(r->cache, name);
	if (r->app->verbose)
		fprintf(stderr, "mangle: show %s page=%d panel=%d mode=%s\n", name, p.page, p.panel,
			p.mode == MODE_PANEL ? "panel" : "page");
	if (!img) {
		const char* lines[] = { NULL, NULL, "Cannot load image:", name };
		display_text(lines, 4, true);
	} else {
		compose(&r->app->scr, img, r->frame);
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

	Reader r = { app, b, cache_new(b), malloc((size_t) app->scr.width * app->scr.height), 0 };
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
	show(&r, pos, true);

	Gesture g = { 0 };
	while (!quit) {
		InEvent ev;
		int     rc = input_wait(&ev, gesture_timeout(&g, now_ms()));
		Action  acts[2];
		int     nact = 0;
		if (rc < 0)
			continue;
		if (rc == 0) {
			acts[nact] = gesture_expire(&g, now_ms());
			nact += acts[nact] != ACT_NONE;
		} else if (ev.kind == IN_QUIT) {
			break;
		} else if (ev.kind == IN_TAP) {
			nact = gesture_tap(&g, ev.x, ev.y, app->scr.width, app->scr.height, b->rtl, now_ms(), acts);
		} else if (ev.kind == IN_LONG) {
			acts[nact++] = gesture_long(&g, ev.x, app->scr.width);
		} else if (ev.kind == IN_KEY) {
			acts[nact++] = gesture_key(&g, ev.code, b->rtl);
		}

		bool done = false;
		for (int i = 0; i < nact && !done; i++) {
			Pos old = pos;
			switch (acts[i]) {
				case ACT_FORWARD:
					if (nav_next(b, &pos))
						show(&r, pos, false);
					break;
				case ACT_BACK:
					if (nav_prev(b, &pos))
						show(&r, pos, false);
					break;
				case ACT_TOGGLE:
					nav_toggle(b, &pos);
					show(&r, pos, true);
					break;
				case ACT_REFRESH:
					show(&r, pos, true);
					break;
				case ACT_EXIT:
					done = true;
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
		"  -f N     full (flashing) refresh every N page turns, 0 = never (default 8)\n"
		"  -O       panel mode: don't show a page whole before its panels\n"
		"  -v       log every image shown\n",
		argv0);
}

int main(int argc, char** argv)
{
	App         app     = { .state_dir = "state", .flash_every = 8 };
	const char* library = NULL;
	bool        resume  = false;
	int         c;
	while ((c = getopt(argc, argv, "l:s:rf:Ovh")) != -1) {
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
				nav_set_overview(false);
				break;
			case 'v':
				app.verbose = true;
				break;
			default:
				usage(argv[0]);
				return c == 'h' ? 0 : 2;
		}
	}
	const char* book = optind < argc ? argv[optind] : NULL;
	if (!book && !library && !resume) {
		usage(argv[0]);
		return 2;
	}

	setup_signals();
	mkdir(app.state_dir, 0755);
	if (display_open(&app.scr) != 0)
		return 1;
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
