#include "stats_view.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "hal.h"
#include "ui.h"

typedef struct {
	int W, H;
	int sc, sm;     // text scale, small text scale
	int hs;         // header text scale
	int ts, vs;     // titles and tile values (smaller on small screens)
	int line;       // height of a line of normal text, with spacing
	int pad;
	int tcols;      // tiles per row
	bool shrt;      // short screen (landscape CYD): side-by-side layouts
} Canvas;

static void rect(int x, int y, int w, int h, uint8_t v)
{
	hal_fill(x, y, w, h, v);
}

static void outline(int x, int y, int w, int h, int t, uint8_t v)
{
	rect(x, y, w, t, v);
	rect(x, y + h - t, w, t, v);
	rect(x, y, t, h, v);
	rect(x + w - t, y, t, h, v);
}

static int text_w(int scale, const char* s)
{
	return ui_text_width(s, scale);
}

static void text(int x, int y, int scale, const char* s, uint8_t v, uint8_t bg)
{
	ui_text(x, y, scale, s, v, bg);
}

static void text_c(int x, int w, int y, int scale, const char* s, uint8_t v, uint8_t bg)
{
	text(x + (w - text_w(scale, s)) / 2, y, scale, s, v, bg);
}

static void text_r(int right, int y, int scale, const char* s, uint8_t v, uint8_t bg)
{
	text(right - text_w(scale, s), y, scale, s, v, bg);
}

static const char* const MONTHS[] = { "January", "February", "March",     "April",   "May",      "June",
				      "July",    "August",   "September", "October", "November", "December" };
static const char* const WDAYS[]  = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
static const char* const WDAY_LONG[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };

static Canvas canvas(void)
{
	Canvas c;
	c.W     = hal_width();
	c.H     = hal_height();
	c.sc    = c.W >= 900 ? 3 : 2;
	c.sm    = c.sc > 1 ? c.sc - 1 : 1;
	bool compact = c.H < 600;
	c.ts    = compact ? c.sm : c.sc;
	c.vs    = c.ts;
	c.line  = FONT_H * c.ts + c.sc * 6;
	c.pad   = c.sc * 8;
	c.tcols = c.W >= 450 ? 3 : 2;
	int cw  = FONT_W * c.sc;
	c.shrt  = c.H < 400;
	c.hs    = !c.shrt && 2 * c.pad + text_w(c.sc, "< Back") + text_w(c.sc, "DailyMonthly") + 6 * cw <= c.W ? c.sc : c.sm;
	return c;
}

// ---- header ----------------------------------------------------------------------

static void layout(StatsView* v, const Canvas* c)
{
	int cw       = FONT_W * c->hs;
	v->w         = c->W;
	v->bar_h     = FONT_H * c->hs + 2 * c->sc * (c->shrt ? 4 : 8);
	v->back_end  = c->pad + text_w(c->hs, "< Back") + 2 * cw;
	v->monthly_x = c->W - c->pad - text_w(c->hs, "Monthly") - 2 * cw;
	v->daily_x   = v->monthly_x - text_w(c->hs, "Daily") - 2 * cw;
	v->H_foot    = c->shrt ? 6 : 12;
	v->foot_y    = c->H - (FONT_H * c->ts + 2 * v->H_foot);
}

static void tab(const Canvas* c, const StatsView* v, int x0, int x1, const char* name, bool active)
{
	int ty = (v->bar_h - FONT_H * c->hs) / 2;
	if (active)
		rect(x0, 0, x1 - x0, v->bar_h - 3, 0x00);
	text_c(x0, x1 - x0, ty, c->hs, name, active ? 0xFF : 0x00, active ? 0x00 : 0xFF);
}

static void header(const Canvas* c, const StatsView* v)
{
	int ty = (v->bar_h - FONT_H * c->hs) / 2;
	text(c->pad, ty, c->hs, "< Back", 0x00, 0xFF);
	tab(c, v, v->daily_x, v->monthly_x, "Daily", v->which == 0);
	tab(c, v, v->monthly_x, c->W, "Monthly", v->which == 1);
	rect(0, v->bar_h - 3, c->W, 3, 0x00);
}

// ---- pieces --------------------------------------------------------------------

static int tile_h(const Canvas* c)
{
	return FONT_H * c->sm + 4 + FONT_H * c->vs + c->sc * 4;
}

// A small grey label over a large value.
static void tile(const Canvas* c, int x, int y, int w, const char* label, const char* value)
{
	char l[64], v[64];
	int  lc = w / (FONT_W * c->sm), vc = w / (FONT_W * c->vs);
	snprintf(l, sizeof(l), "%.*s", lc > 1 ? lc - 1 : 1, label);
	snprintf(v, sizeof(v), "%.*s", vc > 0 ? vc : 1, value);
	text(x, y, c->sm, l, 0x55, 0xFF);
	text(x, y + FONT_H * c->sm + 4, c->vs, v, 0x00, 0xFF);
}

// Tiles in rows of c->tcols. Returns the height used.
static int tiles(const Canvas* c, int cols, int x, int y, int w, const char* const* labels, char values[][32], int n)
{
	int tw = w / cols, rows = (n + cols - 1) / cols;
	for (int i = 0; i < n; i++)
		tile(c, x + (i % cols) * tw, y + (i / cols) * tile_h(c), tw, labels[i], values[i]);
	return rows * tile_h(c);
}

// Bar chart with a title (and a note on the right), bars, and labels under
// some bars. `hi` is drawn black, the others grey.
static void chart(const Canvas* c, int x, int y, int w, int h, const char* title, const char* note, const long* v,
                  int n, int hi, const char* const* labels)
{
	text(x, y, c->ts, title, 0x00, 0xFF);
	if (note && text_w(c->ts, title) + text_w(c->sm, note) + FONT_W * c->ts <= w)
		text_r(x + w, y + (FONT_H * c->ts - FONT_H * c->sm), c->sm, note, 0x55, 0xFF);
	int top = y + FONT_H * c->ts + c->sc * 3;
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
			rect(bx, top + bh - hgt, bw, hgt, i == hi ? 0x00 : 0x88);
		}
		if (labels && labels[i])
			text_c(x0 + i * slot - slot, 3 * slot, top + bh + 6, c->sm, labels[i], i == hi ? 0x00 : 0x55, 0xFF);
	}
	rect(x, top + bh, w, 2, 0x00);
	if (max == 0)
		text_c(x, w, top + bh / 2 - FONT_H * c->sm / 2, c->sm, "no reading yet", 0x88, 0xFF);
}

// Normal text if it fits the width, else small.
static int fit_scale(const Canvas* c, int w, const char* s)
{
	return text_w(c->ts, s) <= w ? c->ts : c->sm;
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

static void draw_daily(const Canvas* c, const StatsView* v)
{
	const Stats* st    = &v->st;
	int          today = stats_today(v->now);
	int          x = c->pad, w = c->W - 2 * c->pad, y = v->bar_h + c->pad;
	char         a[96], b[32];

	int yr, mo, md;
	stats_day_date(today, &yr, &mo, &md);
	snprintf(a, sizeof(a), "%s %d %s %d", WDAY_LONG[stats_weekday(today)], md, MONTHS[mo - 1], yr);
	text(x, y, text_w(c->ts, a) <= w ? c->ts : c->sm, a, 0x00, 0xFF);
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
	const char* labels[6] = { "Today", "Pages today", "Streak", "This week", "Pages this week", "Best streak" };
	char        vals[6][32];
	stats_fmt_duration(vals[0], 32, td ? td->seconds : 0);
	snprintf(vals[1], 32, "%d", td ? td->pages : 0);
	snprintf(vals[2], 32, "%d day%s", cur, cur == 1 ? "" : "s");
	stats_fmt_duration(vals[3], 32, wks);
	snprintf(vals[4], 32, "%ld", wkp);
	snprintf(vals[5], 32, "%d day%s", best, best == 1 ? "" : "s");
	if (c->tcols == 2) {    // pair today with this week, streak with best
		const char* l2[6] = { labels[0], labels[3], labels[1], labels[4], labels[2], labels[5] };
		char        v2[6][32];
		int         ord[6] = { 0, 3, 1, 4, 2, 5 };
		for (int i = 0; i < 6; i++)
			memcpy(v2[i], vals[ord[i]], 32);
		y += tiles(c, 2, x, y, w, l2, v2, 6);
	} else {
		y += tiles(c, 3, x, y, w, labels, vals, 6);
	}
	rect(x, y, w, 1, 0xAA);
	y += c->sc * 6;

	// Three charts share the rest of the screen (on a short screen the last
	// two side by side).
	int h = (v->foot_y - c->sc * 4 - y) / (c->shrt ? 2 : 3);

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
	int cw2 = c->shrt ? (w - c->pad) / 2 : w;
	chart(c, x, y, cw2, h, "Time of day", peak >= 0 ? a : NULL, st->hour_seconds, 24, peak, hlab);
	int x3 = x;
	if (c->shrt)
		x3 = x + cw2 + c->pad;
	else
		y += h;

	int pw = argmax(st->weekday_seconds, 7);
	if (pw >= 0)
		snprintf(a, sizeof(a), "most on %s", WDAY_LONG[pw]);
	chart(c, x3, y, cw2, h, "Day of week", pw >= 0 ? a : NULL, st->weekday_seconds, 7, pw, WDAYS);

	// Footer: all-time totals.
	rect(0, v->foot_y, c->W, 2, 0x00);
	stats_fmt_duration(b, sizeof(b), st->seconds);
	snprintf(a, sizeof(a), "All time: %s, %ld pages", b, st->pages);
	text_c(0, c->W, v->foot_y + (v->H_foot), fit_scale(c, c->W - 2 * c->pad, a), a, 0x00, 0xFF);
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

static void draw_monthly(const Canvas* c, const StatsView* v)
{
	int  mi = v->mi, year = mi / 12, month = mi % 12 + 1;
	int  x = c->pad, w = c->W - 2 * c->pad, y = v->bar_h + c->pad;
	char a[96], b[32];
	StatMonth m;
	stats_month(&v->log, &v->st, year, month, &m);

	snprintf(a, sizeof(a), "%s %d", MONTHS[month - 1], year);
	text_c(0, c->W, y, c->ts, a, 0x00, 0xFF);
	y += c->line;

	// Calendar: Monday first, one cell per day, darker = more reading. On a
	// short screen it takes the left part and the totals go on the right.
	int first = stats_day_number(year, month, 1);
	int fw    = stats_weekday(first);
	int rows  = (fw + m.days_in_month + 6) / 7;
	int calw  = c->shrt ? w * 11 / 20 : w;
	int cw    = calw / 7;
	int gx    = x + (calw - 7 * cw) / 2;
	int ytop  = y;
	for (int i = 0; i < 7; i++)
		text_c(gx + i * cw, cw, y, c->sm, WDAYS[i], 0x55, 0xFF);
	y += FONT_H * c->sm + 6;

	int trows = (6 + c->tcols - 1) / c->tcols;
	int below = (FONT_H * c->sm + 16) + (c->shrt ? 0 : trows * tile_h(c) + 2 * c->line) + c->sc * 8;
	int ch    = (v->foot_y - y - below) / rows;
	if (ch > cw)
		ch = cw;
	if (ch < FONT_H * c->sm + 4)
		ch = FONT_H * c->sm + 4;
	int today = stats_today(v->now);
	for (int d = 0; d < m.days_in_month; d++) {
		int     k  = fw + d;
		int     cx = gx + (k % 7) * cw, cy = y + (k / 7) * ch;
		uint8_t s  = shade(m.day_seconds[d]);
		rect(cx + 1, cy + 1, cw - 2, ch - 2, s);
		outline(cx, cy, cw, ch, 1, 0xAA);
		uint8_t ink = s <= 0x77 ? 0xFF : 0x00;
		snprintf(a, sizeof(a), "%d", d + 1);
		int num = ch >= FONT_H * c->sc + FONT_H * c->sm + 8 ? c->sc : c->sm;
		text(cx + c->sc * 2, cy + c->sc * 2, num, a, ink, s);
		if (m.day_seconds[d] >= 60 && ch >= FONT_H * (num + c->sm) + 8) {
			compact(a, sizeof(a), m.day_seconds[d]);
			if (text_w(c->sm, a) < cw - 4)
				text_r(cx + cw - c->sc * 2, cy + ch - FONT_H * c->sm - c->sc * 2, c->sm, a, ink, s);
		}
		if (first + d == today)
			outline(cx, cy, cw, ch, c->sc + 1, 0x00);
	}
	y += rows * ch + c->sc * 4;

	// Legend.
	static const struct {
		int         secs;
		const char* label;
	} LEG[] = { { 60, "<15m" }, { 15 * 60, "<30m" }, { 30 * 60, "<1h" }, { 60 * 60, "1h+" } };
	int box = FONT_H * c->sm, lx = x;
	for (size_t i = 0; i < sizeof(LEG) / sizeof(*LEG); i++) {
		rect(lx, y, box, box, shade(LEG[i].secs));
		outline(lx, y, box, box, 1, 0x88);
		text(lx + box + 4, y, c->sm, LEG[i].label, 0x55, 0xFF);
		lx += box + 4 + text_w(c->sm, LEG[i].label) + 3 * FONT_W * c->sm;
	}
	y += box + 16;
	int tx = x, tw = w, cols = c->tcols;
	if (c->shrt)
		tx = x + calw + c->pad, tw = w - calw - c->pad, cols = 2, y = ytop;

	// Totals.
	const char* labels[6] = { "Reading time", "Pages", "Days read", "Sessions", "Books", "Per reading day" };
	char        vals[6][32];
	stats_fmt_duration(vals[0], 32, m.seconds);
	snprintf(vals[1], 32, "%d", m.pages);
	snprintf(vals[2], 32, "%d / %d", m.days_read, m.days_in_month);
	snprintf(vals[3], 32, "%d", m.sessions);
	snprintf(vals[4], 32, "%d", m.books);
	stats_fmt_duration(vals[5], 32, m.days_read ? m.seconds / m.days_read : 0);
	y += tiles(c, cols, tx, y, tw, labels, vals, 6);

	if (m.best_day) {
		stats_fmt_duration(b, sizeof(b), m.best_seconds);
		snprintf(a, sizeof(a), "Best day: %s %d, %s", WDAY_LONG[stats_weekday(first + m.best_day - 1)], m.best_day, b);
		if (text_w(c->sm, a) > tw)
			snprintf(a, sizeof(a), "Best: %d, %s", m.best_day, b);
		text(tx, y, fit_scale(c, tw, a), a, 0x00, 0xFF);
	}
	y += c->line;
	if (mi > v->first_mi) {
		StatMonth p;
		int       pm = mi - 1;
		stats_month(&v->log, &v->st, pm / 12, pm % 12 + 1, &p);
		long diff = (long) m.seconds - p.seconds;
		stats_fmt_duration(b, sizeof(b), diff < 0 ? -diff : diff);
		snprintf(a, sizeof(a), "vs %s: %c%s", MONTHS[pm % 12], diff < 0 ? '-' : '+', b);
		text(tx, y, fit_scale(c, tw, a), a, 0x55, 0xFF);
	}

	// Footer: month navigation.
	rect(0, v->foot_y, c->W, 2, 0x00);
	int fy = v->foot_y + v->H_foot;
	if (mi > v->first_mi)
		text(c->pad, fy, c->ts, "< prev", 0x00, 0xFF);
	if (mi < v->cur_mi) {
		text_r(c->W - c->pad, fy, c->ts, "next >", 0x00, 0xFF);
		text_c(0, c->W, fy, fit_scale(c, c->W / 2, "this month"), "this month", 0x00, 0xFF);
	}
}

// ---- screen ------------------------------------------------------------------------

void stats_view_open(StatsView* v, const char* dir, const StatRec* live, time_t now)
{
	memset(v, 0, sizeof(*v));
	if (stats_load(dir, &v->log) != 0)
		hal_log("stats log only partly loaded (out of memory)\n");
	if (live && live->open && live->last > live->start)
		stats_log_add(&v->log, live->start, live->last, live->pages, live->book);
	if (stats_build(&v->log, &v->st) != 0)
		hal_log("out of memory building stats\n");
	v->now = now;
	int yr, mo, md;
	stats_day_date(stats_today(now), &yr, &mo, &md);
	v->cur_mi = v->first_mi = v->mi = yr * 12 + mo - 1;
	if (v->st.first_day >= 0) {
		stats_day_date(v->st.first_day, &yr, &mo, &md);
		if (yr * 12 + mo - 1 < v->first_mi)
			v->first_mi = yr * 12 + mo - 1;
	}
}

void stats_view_close(StatsView* v)
{
	stats_clear(&v->st);
	stats_free(&v->log);
}

void stats_view_draw(StatsView* v)
{
	Canvas c = canvas();
	layout(v, &c);
	hal_fill(0, 0, c.W, c.H, 0xFF);
	header(&c, v);
	if (v->which == 0)
		draw_daily(&c, v);
	else
		draw_monthly(&c, v);
}

SvResult stats_view_tap(StatsView* v, int x, int y)
{
	if (y < v->bar_h) {
		if (x < v->back_end)
			return SV_CLOSE;
		int to = x >= v->monthly_x ? 1 : x >= v->daily_x ? 0 : v->which;
		if (to == v->which)
			return SV_NONE;
		v->which = to;
		v->mi    = v->cur_mi;
		return SV_TAB;
	}
	if (v->which == 1 && y >= v->foot_y) {
		int old = v->mi;
		if (x < v->w / 3 && v->mi > v->first_mi)
			v->mi--;
		else if (x >= v->w * 2 / 3 && v->mi < v->cur_mi)
			v->mi++;
		else if (x >= v->w / 3 && x < v->w * 2 / 3)
			v->mi = v->cur_mi;
		return v->mi != old ? SV_REDRAW : SV_NONE;
	}
	return SV_NONE;
}

SvResult stats_view_step(StatsView* v, int dir)
{
	if (v->which != 1)
		return SV_NONE;
	int old = v->mi;
	if (dir > 0 && v->mi < v->cur_mi)
		v->mi++;
	else if (dir < 0 && v->mi > v->first_mi)
		v->mi--;
	return v->mi != old ? SV_REDRAW : SV_NONE;
}
