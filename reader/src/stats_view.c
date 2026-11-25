#include "stats_view.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "font.h"
#include "gesture.h"
#include "input.h"

typedef struct {
	uint8_t* f;
	int      W, H;
	int      sc, sm;     // text scale, small text scale
	int      line;       // height of a line of normal text, with spacing
	int      pad;
} Canvas;

static void rect(Canvas* c, int x, int y, int w, int h, uint8_t v)
{
	for (int j = y < 0 ? 0 : y; j < y + h && j < c->H; j++)
		for (int i = x < 0 ? 0 : x; i < x + w && i < c->W; i++)
			c->f[(size_t) j * c->W + i] = v;
}

static void outline(Canvas* c, int x, int y, int w, int h, int t, uint8_t v)
{
	rect(c, x, y, w, t, v);
	rect(c, x, y + h - t, w, t, v);
	rect(c, x, y, t, h, v);
	rect(c, x + w - t, y, t, h, v);
}

static int text_w(int scale, const char* s)
{
	return (int) strlen(s) * FONT_W * scale;
}

static void text(Canvas* c, int x, int y, int scale, const char* s, uint8_t v)
{
	font_draw(c->f, c->W, c->H, x, y, scale, s, v);
}

static void text_c(Canvas* c, int x, int w, int y, int scale, const char* s, uint8_t v)
{
	text(c, x + (w - text_w(scale, s)) / 2, y, scale, s, v);
}

static void text_r(Canvas* c, int right, int y, int scale, const char* s, uint8_t v)
{
	text(c, right - text_w(scale, s), y, scale, s, v);
}

static const char* const MONTHS[] = { "January", "February", "March",     "April",   "May",      "June",
				      "July",    "August",   "September", "October", "November", "December" };
static const char* const WDAYS[]  = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
static const char* const WDAY_LONG[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };

// ---- header and footer -------------------------------------------------------

typedef struct {
	int bar_h;
	int back_end;              // "< Back" hit area: x < back_end
	int daily_x, monthly_x;    // tab hit areas: [daily_x, monthly_x), [monthly_x, W)
	int foot_y;                // footer strip starts here
} Layout;

static Layout layout(const Canvas* c)
{
	Layout L;
	int    cw = FONT_W * c->sc;
	L.bar_h   = FONT_H * c->sc + 2 * c->sc * 8;
	L.back_end = c->pad + text_w(c->sc, "< Back") + 2 * cw;
	L.monthly_x = c->W - c->pad - text_w(c->sc, "Monthly") - 2 * cw;
	L.daily_x   = L.monthly_x - text_w(c->sc, "Daily") - 2 * cw;
	L.foot_y    = c->H - (FONT_H * c->sc + 24);
	return L;
}

static void tab(Canvas* c, const Layout* L, int x0, int x1, const char* name, bool active)
{
	int ty = (L->bar_h - FONT_H * c->sc) / 2;
	if (active)
		rect(c, x0, 0, x1 - x0, L->bar_h - 3, 0x00);
	text_c(c, x0, x1 - x0, ty, c->sc, name, active ? 0xFF : 0x00);
}

static void header(Canvas* c, const Layout* L, int which)
{
	int ty = (L->bar_h - FONT_H * c->sc) / 2;
	text(c, c->pad, ty, c->sc, "< Back", 0x00);
	tab(c, L, L->daily_x, L->monthly_x, "Daily", which == 0);
	tab(c, L, L->monthly_x, c->W, "Monthly", which == 1);
	rect(c, 0, L->bar_h - 3, c->W, 3, 0x00);
}

// ---- pieces --------------------------------------------------------------------

// A small grey label over a large value.
static int tile(Canvas* c, int x, int y, int w, const char* label, const char* value)
{
	text(c, x, y, c->sm, label, 0x55);
	char v[64];
	int  cols = w / (FONT_W * c->sc);
	snprintf(v, sizeof(v), "%.*s", cols > 0 ? cols : 1, value);
	text(c, x, y + FONT_H * c->sm + 4, c->sc, v, 0x00);
	return FONT_H * c->sm + 4 + FONT_H * c->sc + c->sc * 6;
}

// Bar chart with a title (and a note on the right), bars, and labels under
// some bars. `hi` is drawn black, the others grey.
static void chart(Canvas* c, int x, int y, int w, int h, const char* title, const char* note, const long* v,
                  int n, int hi, const char* const* labels)
{
	text(c, x, y, c->sc, title, 0x00);
	if (note)
		text_r(c, x + w, y + (FONT_H * c->sc - FONT_H * c->sm), c->sm, note, 0x55);
	int top = y + FONT_H * c->sc + c->sc * 4;
	int lab = FONT_H * c->sm + 6;
	int bh  = y + h - lab - top - 2;
	if (bh < 8)
		bh = 8;
	long max = 0;
	for (int i = 0; i < n; i++)
		if (v[i] > max)
			max = v[i];
	int slot = w / n, bw = slot * 2 / 3;
	if (bw < 2)
		bw = slot > 1 ? slot - 1 : 1;
	int x0 = x + (w - slot * n) / 2;
	for (int i = 0; i < n; i++) {
		int bx = x0 + i * slot + (slot - bw) / 2;
		if (max > 0 && v[i] > 0) {
			int hgt = (int) ((long long) v[i] * bh / max);
			if (hgt < 3)
				hgt = 3;
			rect(c, bx, top + bh - hgt, bw, hgt, i == hi ? 0x00 : 0x88);
		}
		if (labels && labels[i])
			text_c(c, x0 + i * slot - slot, 3 * slot, top + bh + 6, c->sm, labels[i], i == hi ? 0x00 : 0x55);
	}
	rect(c, x, top + bh, w, 2, 0x00);
	if (max == 0)
		text_c(c, x, w, top + bh / 2 - FONT_H * c->sm / 2, c->sm, "no reading yet", 0x88);
}

// Normal text if it fits the width, else small.
static int fit_scale(const Canvas* c, int w, const char* s)
{
	return text_w(c->sc, s) <= w ? c->sc : c->sm;
}

static int argmax(const long* v, int n)
{
	int best = -1;
	for (int i = 0; i < n; i++)
		if (v[i] > 0 && (best < 0 || v[i] > v[best]))
			best = i;
	return best;
}

// ---- daily ------------------------------------------------------------------------

static void draw_daily(Canvas* c, const Layout* L, const Stats* st, time_t now)
{
	int today = stats_today(now);
	int x = c->pad, w = c->W - 2 * c->pad, y = L->bar_h + c->pad;
	char a[96], b[32];

	int yr, mo, md;
	stats_day_date(today, &yr, &mo, &md);
	snprintf(a, sizeof(a), "%s %d %s %d", WDAY_LONG[stats_weekday(today)], md, MONTHS[mo - 1], yr);
	text(c, x, y, c->sc, a, 0x00);
	y += c->line + c->sc * 2;

	// Today, this week (from Monday), streaks.
	const StatDay* td  = stats_find(st, today);
	long           wks = 0, wkp = 0;
	for (int d = today - stats_weekday(today); d <= today; d++) {
		const StatDay* e = stats_find(st, d);
		if (e)
			wks += e->seconds, wkp += e->pages;
	}
	int cur, best;
	stats_streaks(st, today, &cur, &best);
	int tw = w / 3;
	stats_fmt_duration(a, sizeof(a), td ? td->seconds : 0);
	tile(c, x, y, tw, "Today", a);
	snprintf(a, sizeof(a), "%d", td ? td->pages : 0);
	tile(c, x + tw, y, tw, "Pages today", a);
	snprintf(a, sizeof(a), "%d day%s", cur, cur == 1 ? "" : "s");
	y += tile(c, x + 2 * tw, y, tw, "Streak", a);
	stats_fmt_duration(a, sizeof(a), wks);
	tile(c, x, y, tw, "This week", a);
	snprintf(a, sizeof(a), "%ld", wkp);
	tile(c, x + tw, y, tw, "Pages this week", a);
	snprintf(a, sizeof(a), "%d day%s", best, best == 1 ? "" : "s");
	y += tile(c, x + 2 * tw, y, tw, "Best streak", a);
	rect(c, x, y, w, 1, 0xAA);
	y += c->sc * 6;

	// Three charts share the rest of the screen.
	int h = (L->foot_y - c->sc * 4 - y) / 3;

	long        days[14];
	char        dl[14][4];
	const char* dlab[14];
	for (int i = 0; i < 14; i++) {
		int            d = today - 13 + i;
		const StatDay* e = stats_find(st, d);
		days[i]          = e ? e->seconds : 0;
		stats_day_date(d, &yr, &mo, &md);
		snprintf(dl[i], sizeof(dl[i]), "%d", md);
		dlab[i] = (i % 2 == 1) ? dl[i] : NULL;    // every other day, ending today
	}
	int  dmax = argmax(days, 14);
	long mx   = dmax >= 0 ? days[dmax] : 0;
	if (mx > 0) {
		stats_fmt_duration(b, sizeof(b), mx);
		snprintf(a, sizeof(a), "max %s", b);
	}
	chart(c, x, y, w, h, "Last 14 days", mx > 0 ? a : NULL, days, 14, 13, dlab);
	y += h;

	const char* hlab[24] = { 0 };
	hlab[0] = "0", hlab[6] = "6", hlab[12] = "12", hlab[18] = "18", hlab[23] = "23";
	int peak = argmax(st->hour_seconds, 24);
	if (peak >= 0)
		snprintf(a, sizeof(a), "most at %02d-%02dh", peak, (peak + 1) % 24);
	chart(c, x, y, w, h, "Time of day", peak >= 0 ? a : NULL, st->hour_seconds, 24, peak, hlab);
	y += h;

	int pw = argmax(st->weekday_seconds, 7);
	if (pw >= 0)
		snprintf(a, sizeof(a), "most on %s", WDAY_LONG[pw]);
	chart(c, x, y, w, h, "Day of week", pw >= 0 ? a : NULL, st->weekday_seconds, 7, pw, WDAYS);

	// Footer: all-time totals.
	rect(c, 0, L->foot_y, c->W, 2, 0x00);
	stats_fmt_duration(b, sizeof(b), st->seconds);
	snprintf(a, sizeof(a), "All time: %s, %ld pages", b, st->pages);
	text_c(c, 0, c->W, L->foot_y + 12, fit_scale(c, c->W - 2 * c->pad, a), a, 0x00);
}

// ---- monthly -----------------------------------------------------------------------

static uint8_t shade(int seconds)
{
	if (seconds <= 0)
		return 0xFF;
	if (seconds < 15 * 60)
		return 0xDD;
	if (seconds < 30 * 60)
		return 0xAA;
	if (seconds < 60 * 60)
		return 0x77;
	return 0x33;
}

static void compact(char* out, size_t n, int seconds)
{
	int m = seconds / 60;
	if (m < 60)
		snprintf(out, n, "%dm", m);
	else
		snprintf(out, n, "%d:%02d", m / 60, m % 60);
}

static void draw_monthly(Canvas* c, const Layout* L, const StatLog* log, const Stats* st, int mi, int cur_mi,
                         int first_mi, time_t now)
{
	int year = mi / 12, month = mi % 12 + 1;
	int x = c->pad, w = c->W - 2 * c->pad, y = L->bar_h + c->pad;
	char a[96], b[32];
	StatMonth m;
	stats_month(log, st, year, month, &m);

	snprintf(a, sizeof(a), "%s %d", MONTHS[month - 1], year);
	text_c(c, 0, c->W, y, c->sc, a, 0x00);
	y += c->line;

	// Calendar: Monday first, one cell per day, darker = more reading.
	int first = stats_day_number(year, month, 1);
	int fw    = stats_weekday(first);
	int rows  = (fw + m.days_in_month + 6) / 7;
	int cw    = w / 7;
	int gx    = x + (w - 7 * cw) / 2;
	for (int i = 0; i < 7; i++)
		text_c(c, gx + i * cw, cw, y, c->sm, WDAYS[i], 0x55);
	y += FONT_H * c->sm + 6;

	int tile_h  = FONT_H * c->sm + 4 + FONT_H * c->sc + c->sc * 6;
	int below   = (FONT_H * c->sm + 16) + 2 * tile_h + 2 * c->line + c->sc * 8;
	int ch      = (L->foot_y - y - below) / rows;
	if (ch > cw)
		ch = cw;
	if (ch < FONT_H * c->sm + 4)
		ch = FONT_H * c->sm + 4;
	int today = stats_today(now);
	for (int d = 0; d < m.days_in_month; d++) {
		int     k  = fw + d;
		int     cx = gx + (k % 7) * cw, cy = y + (k / 7) * ch;
		uint8_t v  = shade(m.day_seconds[d]);
		rect(c, cx + 1, cy + 1, cw - 2, ch - 2, v);
		outline(c, cx, cy, cw, ch, 1, 0xAA);
		uint8_t ink = v <= 0x77 ? 0xFF : 0x00;
		snprintf(a, sizeof(a), "%d", d + 1);
		int num = ch >= FONT_H * c->sc + FONT_H * c->sm + 8 ? c->sc : c->sm;
		text(c, cx + c->sc * 2, cy + c->sc * 2, num, a, ink);
		if (m.day_seconds[d] >= 60 && ch >= FONT_H * (num + c->sm) + 8) {
			compact(a, sizeof(a), m.day_seconds[d]);
			if (text_w(c->sm, a) < cw - 4)
				text_r(c, cx + cw - c->sc * 2, cy + ch - FONT_H * c->sm - c->sc * 2, c->sm, a, ink);
		}
		if (first + d == today)
			outline(c, cx, cy, cw, ch, c->sc + 1, 0x00);
	}
	y += rows * ch + c->sc * 4;

	// Legend.
	static const struct {
		int         secs;
		const char* label;
	} LEG[] = { { 60, "<15m" }, { 15 * 60, "<30m" }, { 30 * 60, "<1h" }, { 60 * 60, "1h+" } };
	int box = FONT_H * c->sm, lx = x;
	for (size_t i = 0; i < sizeof(LEG) / sizeof(*LEG); i++) {
		rect(c, lx, y, box, box, shade(LEG[i].secs));
		outline(c, lx, y, box, box, 1, 0x88);
		text(c, lx + box + 4, y, c->sm, LEG[i].label, 0x55);
		lx += box + 4 + text_w(c->sm, LEG[i].label) + 3 * FONT_W * c->sm;
	}
	y += box + 16;

	// Totals.
	int tw = w / 3;
	stats_fmt_duration(a, sizeof(a), m.seconds);
	tile(c, x, y, tw, "Reading time", a);
	snprintf(a, sizeof(a), "%d", m.pages);
	tile(c, x + tw, y, tw, "Pages", a);
	snprintf(a, sizeof(a), "%d / %d", m.days_read, m.days_in_month);
	y += tile(c, x + 2 * tw, y, tw, "Days read", a);
	snprintf(a, sizeof(a), "%d", m.sessions);
	tile(c, x, y, tw, "Sessions", a);
	snprintf(a, sizeof(a), "%d", m.books);
	tile(c, x + tw, y, tw, "Books", a);
	stats_fmt_duration(a, sizeof(a), m.days_read ? m.seconds / m.days_read : 0);
	y += tile(c, x + 2 * tw, y, tw, "Per reading day", a);

	if (m.best_day) {
		stats_fmt_duration(b, sizeof(b), m.best_seconds);
		snprintf(a, sizeof(a), "Best day: %s %d, %s", WDAY_LONG[stats_weekday(first + m.best_day - 1)], m.best_day, b);
		text(c, x, y, fit_scale(c, w, a), a, 0x00);
	}
	y += c->line;
	if (mi > first_mi) {
		StatMonth p;
		int       pm = mi - 1;
		stats_month(log, st, pm / 12, pm % 12 + 1, &p);
		long diff = (long) m.seconds - p.seconds;
		stats_fmt_duration(b, sizeof(b), diff < 0 ? -diff : diff);
		snprintf(a, sizeof(a), "vs %s: %c%s", MONTHS[pm % 12], diff < 0 ? '-' : '+', b);
		text(c, x, y, fit_scale(c, w, a), a, 0x55);
	}

	// Footer: month navigation.
	rect(c, 0, L->foot_y, c->W, 2, 0x00);
	int fy = L->foot_y + 12;
	if (mi > first_mi)
		text(c, c->pad, fy, c->sc, "< prev", 0x00);
	if (mi < cur_mi) {
		text_r(c, c->W - c->pad, fy, c->sc, "next >", 0x00);
		text_c(c, 0, c->W, fy, c->sc, "this month", 0x00);
	}
}

// ---- screen ------------------------------------------------------------------------

bool stats_screen(const Screen* s, const char* state_dir, const StatRec* live, uint8_t* buf,
                  volatile sig_atomic_t* quit)
{
	StatLog log;
	Stats   st;
	if (stats_load(state_dir, &log) != 0)
		fprintf(stderr, "mangle: stats log only partly loaded (out of memory)\n");
	if (live && live->open && live->last > live->start)
		stats_log_add(&log, live->start, live->last, live->pages, live->book);
	if (stats_build(&log, &st) != 0)
		fprintf(stderr, "mangle: out of memory building stats\n");

	Canvas c = { buf, s->width, s->height, s->width >= 900 ? 3 : 2, 0, 0, 0 };
	c.sm     = c.sc - 1;
	c.line   = FONT_H * c.sc + c.sc * 6;
	c.pad    = c.sc * 8;
	Layout L = layout(&c);

	time_t now = time(NULL);
	int    yr, mo, md;
	stats_day_date(stats_today(now), &yr, &mo, &md);
	int cur_mi   = yr * 12 + mo - 1;
	int first_mi = cur_mi;
	if (st.first_day >= 0) {
		stats_day_date(st.first_day, &yr, &mo, &md);
		if (yr * 12 + mo - 1 < first_mi)
			first_mi = yr * 12 + mo - 1;
	}
	int  mi = cur_mi, which = 0;
	bool flash = true, q = false;

	for (;;) {
		memset(buf, 0xFF, (size_t) c.W * c.H);
		header(&c, &L, which);
		if (which == 0)
			draw_daily(&c, &L, &st, now);
		else
			draw_monthly(&c, &L, &log, &st, mi, cur_mi, first_mi, now);
		display_frame(buf, flash);
		flash = false;

		InEvent ev;
		int     rc = input_wait(&ev, -1);
		if (*quit) {
			q = true;
			break;
		}
		if (rc <= 0)
			continue;
		if (ev.kind == IN_QUIT) {
			q = true;
			break;
		}
		if (ev.kind == IN_KEY) {
			Gesture g = { 0 };
			Action  a = gesture_key(&g, ev.code, false);
			if (a == ACT_EXIT)
				break;
			if (which == 1 && a == ACT_FORWARD && mi < cur_mi)
				mi++;
			else if (which == 1 && a == ACT_BACK && mi > first_mi)
				mi--;
			continue;
		}
		if (ev.kind == IN_LONG)
			break;
		if (ev.kind == IN_DRAG || ev.kind == IN_PINCH)
			continue;
		if (ev.y < L.bar_h) {
			if (ev.x < L.back_end)
				break;
			int to = ev.x >= L.monthly_x ? 1 : ev.x >= L.daily_x ? 0 : which;
			flash  = to != which;
			which  = to;
			mi     = cur_mi;
		} else if (which == 1 && ev.y >= L.foot_y) {
			if (ev.x < c.W / 3 && mi > first_mi)
				mi--;
			else if (ev.x >= c.W * 2 / 3 && mi < cur_mi)
				mi++;
			else if (ev.x >= c.W / 3 && ev.x < c.W * 2 / 3)
				mi = cur_mi;
		}
	}
	stats_clear(&st);
	stats_free(&log);
	return q;
}
