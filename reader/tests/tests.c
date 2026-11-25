// Unit tests for the pure parts of the reader (desktop build).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "book.h"
#include "cache.h"
#include "display.h"
#include "gesture.h"
#include "library.h"
#include "nav.h"
#include "state.h"
#include "stats.h"
#include "touch.h"

static int fails, checks;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		checks++;                                                                                              \
		if (!(c)) {                                                                                            \
			fails++;                                                                                       \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                   \
		}                                                                                                      \
	} while (0)

static const char* MANIFEST =
    "{\"version\":1,\"title\":\"T\",\"direction\":\"rtl\",\"screen\":[600,800],\"pages\":["
    "{\"image\":\"p001.png\",\"panels\":[]},"
    "{\"image\":\"p002.png\",\"panels\":[{\"image\":\"p002_1.png\",\"box\":[0,0,1,1]},"
    "{\"image\":\"p002_2.png\",\"box\":[0,0,1,1]}]},"
    "{\"image\":\"p003.png\",\"panels\":[{\"image\":\"p003_1.png\",\"box\":[0,0,1,1]}]},"
    "{\"image\":\"p004.png\",\"panels\":[]}]}";

static Book* fake_book(void)
{
	Book* b = calloc(1, sizeof(Book));
	pthread_mutex_init(&b->lock, NULL);
	char err[128];
	CHECK(book_parse_manifest(b, MANIFEST, strlen(MANIFEST), err, sizeof(err)) == 0);
	return b;
}

static void test_manifest(void)
{
	Book* b = fake_book();
	CHECK(b->npages == 4 && b->rtl && b->screen_w == 600 && b->screen_h == 800);
	CHECK(strcmp(b->title, "T") == 0);
	CHECK(b->pages[1].npanels == 2 && strcmp(b->pages[1].panels[1].image, "p002_2.png") == 0);

	book_close(b);
	Book bad = { 0 };
	char err[128];
	CHECK(book_parse_manifest(&bad, "{", 1, err, sizeof(err)) != 0);
	const char* v2 = "{\"version\":3,\"pages\":[{\"image\":\"a\"}]}";
	CHECK(book_parse_manifest(&bad, v2, strlen(v2), err, sizeof(err)) != 0);
	const char* nopages = "{\"version\":1,\"pages\":[]}";
	CHECK(book_parse_manifest(&bad, nopages, strlen(nopages), err, sizeof(err)) != 0);
	free(bad.title);
	const char* ltr = "{\"version\":1,\"direction\":\"ltr\",\"pages\":[{\"image\":\"a\"}]}";
	Book l = { 0 };
	CHECK(book_parse_manifest(&l, ltr, strlen(ltr), err, sizeof(err)) == 0 && !l.rtl);
	free(l.title);
	free(l.pages[0].image);
	free(l.pages);
}

static void test_rect(void)
{
	const char* m = "{\"version\":2,\"pages\":[{\"image\":\"a.png\",\"panels\":[{\"rect\":[10,20,30,40],\"box\":[0,0,1,1]},{\"rect\":[0,0,0,5]}]}]}";
	Book* b = calloc(1, sizeof(Book));
	pthread_mutex_init(&b->lock, NULL);
	char err[128];
	CHECK(book_parse_manifest(b, m, strlen(m), err, sizeof(err)) == 0);
	CHECK(b->pages[0].npanels == 1);    // the empty rect is dropped
	Pos p = { 0, 0, MODE_PANEL };
	int r[4];
	CHECK(nav_rect(b, p, r) && r[0] == 10 && r[1] == 20 && r[2] == 30 && r[3] == 40);
	CHECK(strcmp(nav_image(b, p), "a.png") == 0);
	p.panel = PANEL_OVERVIEW;
	CHECK(!nav_rect(b, p, r));

	// compose_rect: a 2x2 patch of a 4x4 image fills a 8x8 screen; left half black, right white.
	uint8_t px[16] = { 9, 9, 9, 9, 9, 0, 255, 9, 9, 0, 255, 9, 9, 9, 9, 9 };
	Image   img    = { 4, 4, px };
	Screen  s      = { .width = 8, .height = 8 };
	uint8_t out[64];
	int     rc[4] = { 1, 1, 2, 2 };
	compose_rect(&s, &img, rc, out);
	CHECK(out[3 * 8 + 0] == 0 && out[3 * 8 + 7] == 255 && out[0] == 0 && out[7] == 255);
	book_close(b);
}

#define IMG(p) (nav_image(b, p))

static void test_nav(void)
{
	Book* b = fake_book();
	Pos   p;

	// Panel mode with the page overview (default): each paneled page is
	// shown whole before its panels, in both directions.
	CHECK(nav_overview());
	const char* seq[] = { "p001.png", "p002.png", "p002_1.png", "p002_2.png",
			      "p003.png", "p003_1.png", "p004.png" };
	int         n     = (int) (sizeof(seq) / sizeof(*seq));
	p                 = nav_start(b);
	CHECK(strcmp(IMG(p), seq[0]) == 0);
	for (int i = 1; i < n; i++) {
		CHECK(nav_next(b, &p));
		CHECK(strcmp(IMG(p), seq[i]) == 0);
	}
	CHECK(!nav_next(b, &p));
	for (int i = n - 2; i >= 0; i--) {
		CHECK(nav_prev(b, &p));
		CHECK(strcmp(IMG(p), seq[i]) == 0);
	}
	CHECK(!nav_prev(b, &p));
	// A book whose first page has panels starts at that page's overview.
	b->pages[0].npanels = 1;
	b->pages[0].panels  = b->pages[2].panels;
	p                   = nav_start(b);
	CHECK(p.panel == PANEL_OVERVIEW && strcmp(IMG(p), "p001.png") == 0);
	b->pages[0].npanels = 0;
	b->pages[0].panels  = NULL;
	// A saved overview position survives clamping.
	p = (Pos){ 1, PANEL_OVERVIEW, MODE_PANEL };
	nav_clamp(b, &p);
	CHECK(p.panel == PANEL_OVERVIEW && strcmp(IMG(p), "p002.png") == 0);

	// Without the overview: straight from panel to panel across pages.
	nav_set_overview(false);
	const char* seq2[] = { "p001.png", "p002_1.png", "p002_2.png", "p003_1.png", "p004.png" };
	n                  = (int) (sizeof(seq2) / sizeof(*seq2));
	p                  = nav_start(b);
	CHECK(strcmp(IMG(p), seq2[0]) == 0);
	for (int i = 1; i < n; i++) {
		CHECK(nav_next(b, &p));
		CHECK(strcmp(IMG(p), seq2[i]) == 0);
	}
	CHECK(!nav_next(b, &p));
	for (int i = n - 2; i >= 0; i--) {
		CHECK(nav_prev(b, &p));
		CHECK(strcmp(IMG(p), seq2[i]) == 0);
	}
	CHECK(!nav_prev(b, &p));
	p = (Pos){ 1, PANEL_OVERVIEW, MODE_PANEL };    // saved with the overview on
	nav_clamp(b, &p);
	CHECK(p.panel == 0);
	nav_set_overview(true);

	// Page mode moves whole pages.
	p = (Pos){ 1, 1, MODE_PANEL };
	nav_toggle(b, &p);
	CHECK(p.mode == MODE_PAGE && p.page == 1 && strcmp(IMG(p), "p002.png") == 0);
	CHECK(nav_next(b, &p) && strcmp(IMG(p), "p003.png") == 0);
	CHECK(nav_prev(b, &p) && strcmp(IMG(p), "p002.png") == 0);
	// Back to panel mode: same page, first panel.
	nav_toggle(b, &p);
	CHECK(p.mode == MODE_PANEL && p.page == 1 && strcmp(IMG(p), "p002_1.png") == 0);

	// Clamp a stale saved position.
	p = (Pos){ 99, 7, MODE_PANEL };
	nav_clamp(b, &p);
	CHECK(p.page == 3 && p.panel == 0);
	p = (Pos){ 1, 5, 9 };
	nav_clamp(b, &p);
	CHECK(p.page == 1 && p.panel == 0 && p.mode == MODE_PANEL);
	book_close(b);
}

static void test_gesture(void)
{
	Gesture g = { 0 };
	Action  a[2];
	int     W = 900, H = 1200;
	// RTL: left = forward, right = back, immediately.
	CHECK(gesture_tap(&g, 10, 500, W, H, true, 0, a) == 1 && a[0] == ACT_FORWARD);
	CHECK(gesture_tap(&g, 890, 500, W, H, true, 0, a) == 1 && a[0] == ACT_BACK);
	// LTR mirrors.
	CHECK(gesture_tap(&g, 10, 500, W, H, false, 0, a) == 1 && a[0] == ACT_BACK);
	// Centre: waits, then single tap = forward.
	CHECK(gesture_tap(&g, 450, 500, W, H, true, 1000, a) == 0);
	CHECK(gesture_timeout(&g, 1100) == 200);
	CHECK(gesture_expire(&g, 1200) == ACT_NONE);
	CHECK(gesture_expire(&g, 1300) == ACT_FORWARD);
	CHECK(gesture_timeout(&g, 1300) == -1);
	// Double tap toggles.
	CHECK(gesture_tap(&g, 450, 500, W, H, true, 2000, a) == 0);
	CHECK(gesture_tap(&g, 460, 520, W, H, true, 2200, a) == 1 && a[0] == ACT_TOGGLE);
	CHECK(gesture_timeout(&g, 2200) == -1);
	// Side tap while centre pending: resolve forward first, then the side action.
	gesture_tap(&g, 450, 500, W, H, true, 3000, a);
	CHECK(gesture_tap(&g, 890, 500, W, H, true, 3100, a) == 2 && a[0] == ACT_FORWARD && a[1] == ACT_BACK);
	// Late second tap: first resolves as forward, second starts a new wait.
	gesture_tap(&g, 450, 500, W, H, true, 4000, a);
	CHECK(gesture_tap(&g, 450, 500, W, H, true, 4500, a) == 1 && a[0] == ACT_FORWARD);
	CHECK(g.pending);
	// Long press / keys.
	g.pending = false;
	CHECK(gesture_tap(&g, 450, 5, W, H, true, 5000, a) == 1 && a[0] == ACT_MENU);
	gesture_tap(&g, 450, 500, W, H, true, 6000, a);
	CHECK(gesture_tap(&g, 450, 5, W, H, true, 6100, a) == 2 && a[0] == ACT_FORWARD && a[1] == ACT_MENU);
	CHECK(gesture_long(&g, 450, W) == ACT_EXIT && !g.pending);
	CHECK(gesture_long(&g, 10, W) == ACT_REFRESH);
	CHECK(gesture_key(&g, KEY_PAGEDOWN, true) == ACT_FORWARD);
	CHECK(gesture_key(&g, KEY_PAGEUP, false) == ACT_BACK);
	CHECK(gesture_key(&g, KEY_LEFT, true) == ACT_FORWARD);
	CHECK(gesture_key(&g, KEY_LEFT, false) == ACT_BACK);
}

static struct input_event E(int type, int code, int value)
{
	struct input_event e = { 0 };
	e.type = (unsigned short) type, e.code = (unsigned short) code, e.value = value;
	return e;
}

static TouchResult feed(Touch* t, struct input_event e, int64_t now, int* x, int* y)
{
	return touch_feed(t, &e, now, x, y);
}

static void test_touch(void)
{
	Touch t;
	int   x = -1, y = -1;
	touch_init(&t, 0, 4095, 0, 4095, 1000, 2000);
	// Protocol B tap.
	feed(&t, E(EV_ABS, ABS_MT_SLOT, 0), 0, &x, &y);
	feed(&t, E(EV_ABS, ABS_MT_TRACKING_ID, 7), 0, &x, &y);
	feed(&t, E(EV_ABS, ABS_MT_POSITION_X, 1024), 0, &x, &y);
	feed(&t, E(EV_ABS, ABS_MT_POSITION_Y, 2048), 0, &x, &y);
	CHECK(feed(&t, E(EV_SYN, SYN_REPORT, 0), 0, &x, &y) == TOUCH_NONE);
	feed(&t, E(EV_ABS, ABS_MT_TRACKING_ID, -1), 100, &x, &y);
	CHECK(feed(&t, E(EV_SYN, SYN_REPORT, 0), 100, &x, &y) == TOUCH_TAP);
	CHECK(x == 250 && y == 1000);
	// Long press via BTN_TOUCH.
	feed(&t, E(EV_KEY, BTN_TOUCH, 1), 0, &x, &y);
	feed(&t, E(EV_ABS, ABS_X, 0), 0, &x, &y);
	feed(&t, E(EV_ABS, ABS_Y, 0), 0, &x, &y);
	feed(&t, E(EV_SYN, SYN_REPORT, 0), 0, &x, &y);
	feed(&t, E(EV_KEY, BTN_TOUCH, 0), 900, &x, &y);
	CHECK(feed(&t, E(EV_SYN, SYN_REPORT, 0), 900, &x, &y) == TOUCH_LONG);
	// A swipe is neither.
	feed(&t, E(EV_KEY, BTN_TOUCH, 1), 0, &x, &y);
	feed(&t, E(EV_ABS, ABS_X, 0), 0, &x, &y);
	feed(&t, E(EV_SYN, SYN_REPORT, 0), 0, &x, &y);
	feed(&t, E(EV_ABS, ABS_X, 3000), 50, &x, &y);
	feed(&t, E(EV_SYN, SYN_REPORT, 0), 50, &x, &y);
	feed(&t, E(EV_KEY, BTN_TOUCH, 0), 100, &x, &y);
	CHECK(feed(&t, E(EV_SYN, SYN_REPORT, 0), 100, &x, &y) == TOUCH_NONE);
	// Second finger (slot 1) is ignored.
	feed(&t, E(EV_ABS, ABS_MT_SLOT, 1), 0, &x, &y);
	feed(&t, E(EV_ABS, ABS_MT_TRACKING_ID, 9), 0, &x, &y);
	CHECK(feed(&t, E(EV_SYN, SYN_REPORT, 0), 0, &x, &y) == TOUCH_NONE && !t.down);
	t.slot = 0;
	// Protocol A: contact = frame carries position data.
	touch_init(&t, 0, 999, 0, 1999, 1000, 2000);
	t.proto_a = true;
	feed(&t, E(EV_ABS, ABS_MT_POSITION_X, 100), 0, &x, &y);
	feed(&t, E(EV_ABS, ABS_MT_POSITION_Y, 200), 0, &x, &y);
	feed(&t, E(EV_SYN, SYN_MT_REPORT, 0), 0, &x, &y);
	feed(&t, E(EV_SYN, SYN_REPORT, 0), 0, &x, &y);
	feed(&t, E(EV_SYN, SYN_MT_REPORT, 0), 80, &x, &y);
	CHECK(feed(&t, E(EV_SYN, SYN_REPORT, 0), 80, &x, &y) == TOUCH_TAP && x == 100 && y == 200);
	// Quirks.
	touch_init(&t, 0, 99, 0, 199, 100, 200);
	touch_quirks(&t, "mirror_x");
	touch_map(&t, 0, 0, &x, &y);
	CHECK(x == 99 && y == 0);
	touch_init(&t, 0, 199, 0, 99, 100, 200);
	touch_quirks(&t, "swap");
	touch_map(&t, 199, 50, &x, &y);
	CHECK(x == 50 && y == 199);
}

static void test_compose(void)
{
	Screen  s = { 4, 3, 0, 0, 0 };
	uint8_t buf[12];
	uint8_t px[2] = { 0, 0x80 };
	Image   img   = { 2, 1, px };
	compose(&s, &img, buf);
	const uint8_t want[12] = { 255, 255, 255, 255, 255, 0, 0x80, 255, 255, 255, 255, 255 };
	CHECK(memcmp(buf, want, 12) == 0);
	// Too big: shrunk to fit.
	uint8_t big[8 * 2];
	memset(big, 0, sizeof(big));
	Image bi = { 8, 2, big };
	compose(&s, &bi, buf);
	int black = 0;
	for (int i = 0; i < 12; i++)
		black += buf[i] == 0;
	CHECK(black == 4);    // 4x1 strip
}

static void test_natcmp_state(void)
{
	CHECK(natcmp("vol2.zip", "vol10.zip") < 0);
	CHECK(natcmp("Vol 10", "vol 9") > 0);
	CHECK(natcmp("a", "a") == 0);
	CHECK(natcmp("v02", "v2") == 0);

	char dir[] = "/tmp/mangle-test-XXXXXX";
	CHECK(mkdtemp(dir) != NULL);
	Saved s = { { 12, 3, MODE_PAGE }, 180 }, r;
	CHECK(state_load(dir, "/x/Book 1.zip", &r) != 0);
	CHECK(state_save(dir, "/x/Book 1.zip", &s) == 0);
	CHECK(state_load(dir, "/elsewhere/Book 1.zip", &r) == 0);
	CHECK(r.pos.page == 12 && r.pos.panel == 3 && r.pos.mode == MODE_PAGE && r.npages == 180);
	char last[256];
	CHECK(state_set_last(dir, "/x/Book 1.zip") == 0);
	CHECK(state_get_last(dir, last, sizeof(last)) == 0 && strcmp(last, "/x/Book 1.zip") == 0);
	char cmd[300];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	CHECK(system(cmd) == 0);
}

static void test_stats(void)
{
	setenv("TZ", "UTC0", 1);
	tzset();
	// Calendar helpers.
	CHECK(stats_day_number(1970, 1, 1) == 0);
	int y, m, d;
	int nov25 = stats_day_number(2026, 11, 25);
	stats_day_date(nov25, &y, &m, &d);
	CHECK(y == 2026 && m == 11 && d == 25);
	CHECK(stats_weekday(nov25) == 2);    // a Wednesday
	stats_day_date(stats_day_number(2024, 2, 29) + 1, &y, &m, &d);
	CHECK(m == 3 && d == 1);
	char t[32];
	stats_fmt_duration(t, sizeof(t), 3900);
	CHECK(strcmp(t, "1h 05m") == 0);
	stats_fmt_duration(t, sizeof(t), 59);
	CHECK(strcmp(t, "0m") == 0);

	char dir[] = "/tmp/mangle-stats-XXXXXX";
	CHECK(mkdtemp(dir) != NULL);
	time_t  T0 = (time_t) nov25 * 86400 + 10 * 3600;    // 25 Nov 2026 10:00 UTC
	StatRec r;
	// Pages seen once each; an idle gap splits the session at the last input.
	stats_begin(&r, dir, "/mnt/us/manga/Vol 1.zip", 10, 0, T0);
	stats_page(&r, 1, T0 + 30);
	stats_page(&r, 2, T0 + 60);
	stats_page(&r, 1, T0 + 90);
	stats_activity(&r, T0 + 90 + STATS_IDLE_GAP + 1);    // came back: new session on page 1
	stats_page(&r, 2, T0 + 500);
	stats_pause(&r);                                    // sleep
	stats_activity(&r, T0 + 600);                       // woke, then closed at once: too short
	stats_end(&r);
	CHECK(r.seen == NULL);
	// Too short to count.
	stats_begin(&r, dir, "Other.zip", 5, 0, T0 + 1000);
	stats_page(&r, 1, T0 + 1000 + STATS_MIN_SESSION - 1);
	stats_end(&r);
	// Over midnight: 23:30 -> 00:30.
	time_t late = (time_t) nov25 * 86400 + 23 * 3600 + 1800;
	CHECK(stats_append(dir, late, late + 3600, 20, "/x/Other.zip") == 0);
	// Two days earlier, for streaks.
	CHECK(stats_append(dir, T0 - 2 * 86400, T0 - 2 * 86400 + 600, 4, "Vol 1.zip") == 0);

	StatLog log;
	CHECK(stats_load(dir, &log) == 0);
	CHECK(log.n == 4 && log.nbooks == 2);
	if (log.n == 4) {
		CHECK(log.s[0].start == T0 && log.s[0].end == T0 + 90 && log.s[0].pages == 3);
		CHECK(log.s[1].start == T0 + 391 && log.s[1].end == T0 + 500 && log.s[1].pages == 2);
		CHECK(strcmp(log.books[log.s[0].book], "Vol 1.zip") == 0);
		CHECK(strcmp(log.books[log.s[2].book], "Other.zip") == 0);
	}
	Stats st;
	CHECK(stats_build(&log, &st) == 0);
	CHECK(st.ndays == 3 && st.sessions == 4 && st.pages == 29);
	const StatDay* day = stats_find(&st, nov25);
	CHECK(day && day->seconds == 90 + 109 + 1800 && day->pages == 25 && day->sessions == 3);
	day = stats_find(&st, nov25 + 1);
	CHECK(day && day->seconds == 1800 && day->pages == 0 && day->sessions == 0);
	CHECK(stats_find(&st, nov25 - 1) == NULL);
	CHECK(st.hour_seconds[10] == 199 + 600 && st.hour_seconds[23] == 1800 && st.hour_seconds[0] == 1800);
	CHECK(st.weekday_seconds[2] == 90 + 109 + 1800 && st.weekday_seconds[3] == 1800);
	int cur, best;
	stats_streaks(&st, nov25 + 1, &cur, &best);
	CHECK(cur == 2 && best == 2);
	stats_streaks(&st, nov25 + 2, &cur, &best);    // nothing yet today: yesterday still counts
	CHECK(cur == 2);
	stats_streaks(&st, nov25 + 3, &cur, &best);
	CHECK(cur == 0 && best == 2);

	StatMonth mo;
	stats_month(&log, &st, 2026, 11, &mo);
	CHECK(mo.days_in_month == 30 && mo.days_read == 3 && mo.sessions == 4 && mo.pages == 29 && mo.books == 2);
	CHECK(mo.best_day == 25 && mo.day_seconds[24] == 1999 && mo.day_seconds[22] == 600);
	CHECK(mo.seconds == 1999 + 1800 + 600);
	stats_month(&log, &st, 2026, 2, &mo);
	CHECK(mo.days_in_month == 28 && mo.seconds == 0 && mo.best_day == 0 && mo.books == 0);

	// The running session can be added on top.
	CHECK(stats_log_add(&log, T0 + 5000, T0 + 5060, 1, "New.zip") == 0 && log.nbooks == 3);
	stats_clear(&st);
	stats_free(&log);

	// Garbage lines are skipped.
	char path[300];
	snprintf(path, sizeof(path), "%s/stats.log", dir);
	FILE* f = fopen(path, "a");
	fputs("junk\n5 4 1 backwards.zip\n7 9 1\n", f);
	fclose(f);
	CHECK(stats_load(dir, &log) == 0 && log.n == 4);
	stats_free(&log);

	char cmd[300];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	CHECK(system(cmd) == 0);
}

// Cache against a real ZIP made by the preprocessor (path from argv).
static void test_cache(const char* zip)
{
	char  err[256];
	Book* b = book_open(zip, err, sizeof(err));
	CHECK(b != NULL);
	if (!b) {
		fprintf(stderr, "  %s\n", err);
		return;
	}
	Cache* c = cache_new(b);
	// Walk the whole book in panel mode with prefetching, twice, and check
	// every image decodes to a sane size.
	for (int round = 0; round < 2; round++) {
		Pos p = { 0, 0, MODE_PANEL };
		int n = 0;
		do {
			const char*  name = nav_image(b, p);
			const Image* img  = cache_get(c, name);
			CHECK(img && img->w > 0 && img->h > 0 && img->w <= b->screen_w && img->h <= b->screen_h);
			Pos         q       = p;
			const char* want[2] = { nav_next(b, &q) ? nav_image(b, q) : NULL, NULL };
			cache_prefetch(c, want, 1);
			n++;
		} while (nav_next(b, &p));
		CHECK(n > b->npages);
	}
	CHECK(cache_get(c, "missing.png") == NULL);
	cache_free(c);
	book_close(b);
	CHECK(book_open("/nonexistent.zip", err, sizeof(err)) == NULL);
}

int main(int argc, char** argv)
{
	test_manifest();
	test_rect();
	test_nav();
	test_gesture();
	test_touch();
	test_compose();
	test_natcmp_state();
	test_stats();
	const char* zip = argc > 1 ? argv[1] : getenv("MANGLE_TEST_ZIP");
	if (zip)
		test_cache(zip);
	else
		fprintf(stderr, "(skipping cache test: pass a book ZIP or set MANGLE_TEST_ZIP)\n");
	printf("%d/%d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
