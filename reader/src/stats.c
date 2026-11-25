#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ---- recording ---------------------------------------------------------------

static void book_name(char* out, size_t n, const char* path)
{
	const char* base = strrchr(path, '/');
	base             = base ? base + 1 : path;
	size_t i         = 0;
	for (; base[i] && i < n - 1; i++)
		out[i] = (unsigned char) base[i] < 32 ? '_' : base[i];
	out[i] = 0;
}

int stats_append(const char* dir, time_t start, time_t end, int pages, const char* book)
{
	mkdir(dir, 0755);
	char path[4200];
	snprintf(path, sizeof(path), "%s/stats.log", dir);
	FILE* f = fopen(path, "a");
	if (!f)
		return -1;
	char name[256];
	book_name(name, sizeof(name), book);
	int ok = fprintf(f, "%lld %lld %d %s\n", (long long) start, (long long) end, pages, name) > 0;
	ok &= fclose(f) == 0;
	return ok ? 0 : -1;
}

static void open_session(StatRec* r, time_t now)
{
	r->open  = true;
	r->start = r->last = now;
	r->pages = 0;
	if (r->seen)
		memset(r->seen, 0, (size_t) r->npages);
	if (r->seen && r->page >= 0 && r->page < r->npages) {
		r->seen[r->page] = 1;
		r->pages         = 1;
	}
}

static void close_session(StatRec* r)
{
	if (!r->open)
		return;
	r->open = false;
	if (r->last - r->start < STATS_MIN_SESSION)
		return;
	if (stats_append(r->dir, r->start, r->last, r->pages, r->book) != 0)
		fprintf(stderr, "mangle: cannot write reading stats in %s\n", r->dir);
}

void stats_begin(StatRec* r, const char* dir, const char* book_path, int npages, int page, time_t now)
{
	memset(r, 0, sizeof(*r));
	snprintf(r->dir, sizeof(r->dir), "%s", dir);
	book_name(r->book, sizeof(r->book), book_path);
	r->npages = npages > 0 ? npages : 0;
	r->seen   = r->npages ? calloc((size_t) r->npages, 1) : NULL;
	r->page   = page;
	open_session(r, now);
}

void stats_activity(StatRec* r, time_t now)
{
	if (r->open && (now - r->last > STATS_IDLE_GAP || now < r->last))
		close_session(r);    // walked away (or the clock jumped back)
	if (!r->open)
		open_session(r, now);
	else
		r->last = now;
}

void stats_page(StatRec* r, int page, time_t now)
{
	stats_activity(r, now);
	r->page = page;
	if (r->seen && page >= 0 && page < r->npages && !r->seen[page]) {
		r->seen[page] = 1;
		r->pages++;
	}
}

void stats_pause(StatRec* r)
{
	close_session(r);
}

void stats_end(StatRec* r)
{
	close_session(r);
	free(r->seen);
	r->seen = NULL;
}

// ---- reading the log -----------------------------------------------------------

static int book_index(StatLog* log, const char* name)
{
	for (int i = log->nbooks - 1; i >= 0; i--)
		if (strcmp(log->books[i], name) == 0)
			return i;
	if ((log->nbooks & (log->nbooks - 1)) == 0) {    // grow at powers of two
		char** b = realloc(log->books, (size_t) (log->nbooks ? log->nbooks * 2 : 8) * sizeof(char*));
		if (!b)
			return -1;
		log->books = b;
	}
	char* copy = strdup(name);
	if (!copy)
		return -1;
	log->books[log->nbooks] = copy;
	return log->nbooks++;
}

int stats_log_add(StatLog* log, time_t start, time_t end, int pages, const char* book)
{
	if (log->n == log->cap) {
		int          nc = log->cap ? log->cap * 2 : 64;
		StatSession* s  = realloc(log->s, (size_t) nc * sizeof(*s));
		if (!s)
			return -1;
		log->s   = s;
		log->cap = nc;
	}
	int bi = book_index(log, book);
	if (bi < 0)
		return -1;
	log->s[log->n++] = (StatSession){ start, end, pages, bi };
	return 0;
}

int stats_load(const char* dir, StatLog* log)
{
	memset(log, 0, sizeof(*log));
	char path[4200];
	snprintf(path, sizeof(path), "%s/stats.log", dir);
	FILE* f = fopen(path, "r");
	if (!f)
		return 0;
	int  rc = 0;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		if (len && line[len - 1] != '\n' && !feof(f)) {    // overlong line: skip the rest
			int c;
			while ((c = fgetc(f)) != EOF && c != '\n') {}
			continue;
		}
		line[strcspn(line, "\r\n")] = 0;
		long long a, b;
		int       pages, off = 0;
		if (sscanf(line, "%lld %lld %d %n", &a, &b, &pages, &off) < 3 || off == 0 || !line[off] || b < a || pages < 0)
			continue;
		if (stats_log_add(log, (time_t) a, (time_t) b, pages, line + off) != 0) {
			rc = -1;
			break;
		}
	}
	fclose(f);
	return rc;
}

void stats_free(StatLog* log)
{
	for (int i = 0; i < log->nbooks; i++)
		free(log->books[i]);
	free(log->books);
	free(log->s);
	memset(log, 0, sizeof(*log));
}

// ---- calendar -------------------------------------------------------------------

// Howard Hinnant's days_from_civil / civil_from_days.
int stats_day_number(int y, int m, int d)
{
	y -= m <= 2;
	int      era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned) (y - era * 400);
	unsigned doy = (153u * (unsigned) (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned) d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int) doe - 719468;
}

void stats_day_date(int z, int* year, int* month, int* mday)
{
	z += 719468;
	int      era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned) (z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int      y   = (int) yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp  = (5 * doy + 2) / 153;
	unsigned d   = doy - (153 * mp + 2) / 5 + 1;
	unsigned m   = mp < 10 ? mp + 3 : mp - 9;
	*year        = y + (m <= 2);
	*month       = (int) m;
	*mday        = (int) d;
}

static int local_day(time_t t, struct tm* tm)
{
	localtime_r(&t, tm);
	return stats_day_number(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

int stats_today(time_t now)
{
	struct tm tm;
	return local_day(now, &tm);
}

int stats_weekday(int day)
{
	// 1970-01-01 was a Thursday (3 with Monday = 0).
	int w = (day + 3) % 7;
	return w < 0 ? w + 7 : w;
}

static int days_in_month(int y, int m)
{
	int a = stats_day_number(y, m, 1);
	int b = m == 12 ? stats_day_number(y + 1, 1, 1) : stats_day_number(y, m + 1, 1);
	return b - a;
}

// ---- summaries -----------------------------------------------------------------

static StatDay* day_slot(Stats* st, int day, int* cap)
{
	// Sessions are logged in order, so the day is almost always the last one.
	int lo = 0, hi = st->ndays;
	if (st->ndays && st->days[st->ndays - 1].day < day)
		lo = st->ndays;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (st->days[mid].day < day)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < st->ndays && st->days[lo].day == day)
		return &st->days[lo];
	if (st->ndays == *cap) {
		int      nc = *cap ? *cap * 2 : 64;
		StatDay* d  = realloc(st->days, (size_t) nc * sizeof(*d));
		if (!d)
			return NULL;
		st->days = d;
		*cap     = nc;
	}
	memmove(&st->days[lo + 1], &st->days[lo], (size_t) (st->ndays - lo) * sizeof(*st->days));
	st->ndays++;
	st->days[lo] = (StatDay){ day, 0, 0, 0 };
	return &st->days[lo];
}

int stats_build(const StatLog* log, Stats* st)
{
	memset(st, 0, sizeof(*st));
	st->first_day = -1;
	int cap       = 0;
	for (int i = 0; i < log->n; i++) {
		const StatSession* s = &log->s[i];
		struct tm          tm;
		StatDay*           d = day_slot(st, local_day(s->start, &tm), &cap);
		if (!d)
			return -1;
		d->pages += s->pages;
		d->sessions++;
		st->pages += s->pages;
		st->sessions++;
		// Spread the time over the hours (and days) it covers.
		for (time_t t = s->start; t < s->end;) {
			int    day   = local_day(t, &tm);
			time_t next  = t - tm.tm_min * 60 - tm.tm_sec + 3600;
			time_t until = next < s->end ? next : s->end;
			int    secs  = (int) (until - t);
			StatDay* dd  = day_slot(st, day, &cap);
			if (!dd)
				return -1;
			dd->seconds += secs;
			st->hour_seconds[tm.tm_hour] += secs;
			st->weekday_seconds[stats_weekday(day)] += secs;
			st->seconds += secs;
			t = until;
		}
	}
	if (st->ndays)
		st->first_day = st->days[0].day;
	return 0;
}

void stats_clear(Stats* st)
{
	free(st->days);
	memset(st, 0, sizeof(*st));
	st->first_day = -1;
}

const StatDay* stats_find(const Stats* st, int day)
{
	int lo = 0, hi = st->ndays;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (st->days[mid].day < day)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < st->ndays && st->days[lo].day == day ? &st->days[lo] : NULL;
}

void stats_streaks(const Stats* st, int today, int* current, int* longest)
{
	*current = *longest = 0;
	int run = 0, prev = 0;
	for (int i = 0; i < st->ndays; i++) {
		int d = st->days[i].day;
		run   = (i > 0 && d == prev + 1) ? run + 1 : 1;
		prev  = d;
		if (run > *longest)
			*longest = run;
		if (d == today || d == today - 1)    // still going if read today or yesterday
			*current = run;
	}
}

void stats_month(const StatLog* log, const Stats* st, int year, int month, StatMonth* m)
{
	memset(m, 0, sizeof(*m));
	m->year          = year;
	m->month         = month;
	m->days_in_month = days_in_month(year, month);
	int first        = stats_day_number(year, month, 1);
	for (int i = 0; i < m->days_in_month; i++) {
		const StatDay* d = stats_find(st, first + i);
		if (!d)
			continue;
		m->day_seconds[i] = d->seconds;
		m->seconds += d->seconds;
		m->pages += d->pages;
		m->sessions += d->sessions;
		m->days_read += d->seconds > 0 || d->pages > 0;
		if (d->seconds > m->best_seconds) {
			m->best_seconds = d->seconds;
			m->best_day     = i + 1;
		}
	}
	uint8_t* seen = log->nbooks ? calloc((size_t) log->nbooks, 1) : NULL;
	if (seen) {
		for (int i = 0; i < log->n; i++) {
			struct tm tm;
			int       d = local_day(log->s[i].start, &tm);
			if (d >= first && d < first + m->days_in_month && !seen[log->s[i].book]) {
				seen[log->s[i].book] = 1;
				m->books++;
			}
		}
		free(seen);
	}
}

void stats_fmt_duration(char* out, size_t n, long seconds)
{
	long mins = seconds / 60;
	if (mins >= 60)
		snprintf(out, n, "%ldh %02ldm", mins / 60, mins % 60);
	else
		snprintf(out, n, "%ldm", mins);
}
