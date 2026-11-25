// Reading statistics: a session log in the state folder ("stats.log") and
// the summaries the Stats screen shows. Pure logic plus file I/O, no drawing.
//
// stats.log has one line per reading session, appended when it ends:
//   START END PAGES BOOK
// START/END are Unix times (seconds, UTC), PAGES is the number of different
// pages seen in the session, BOOK is the book's file name (rest of the line).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// No input for this long ends a session at the last input; the next input
// starts a new one. Sleeping (suspend) ends the session the same way.
#define STATS_IDLE_GAP 300
// Sessions shorter than this (an accidental open) aren't recorded.
#define STATS_MIN_SESSION 10

// ---- recording ---------------------------------------------------------------

typedef struct {
	char     dir[4096];
	char     book[256];
	int      npages;
	uint8_t* seen;     // npages flags: page seen in the current session
	int      pages;    // pages seen in the current session
	int      page;     // page on screen
	bool     open;     // a session is running
	time_t   start, last;    // session start, last input
} StatRec;

// Start recording a book (a session starts at once, on `page`).
void stats_begin(StatRec* r, const char* dir, const char* book_path, int npages, int page, time_t now);
// The user did something (any input). Restarts the session after an idle gap.
void stats_activity(StatRec* r, time_t now);
// A page is on screen (counts as activity).
void stats_page(StatRec* r, int page, time_t now);
// End the session at the last input (sleep); the next activity starts a new one.
void stats_pause(StatRec* r);
// Close the book: end the session and free the recorder.
void stats_end(StatRec* r);

// Append one session line. Returns 0 on success.
int stats_append(const char* dir, time_t start, time_t end, int pages, const char* book);

// ---- reading the log -----------------------------------------------------------

typedef struct {
	time_t start, end;
	int    pages;
	int    book;    // index into StatLog.books
} StatSession;

typedef struct {
	StatSession* s;
	int          n;
	int          cap;
	char**       books;    // distinct book names
	int          nbooks;
} StatLog;

// Load stats.log (a missing file is an empty log). Returns 0, or -1 on out of memory.
int  stats_load(const char* dir, StatLog* log);
void stats_free(StatLog* log);
// Add a session (e.g. the one still running). Returns 0, or -1 on out of memory.
int  stats_log_add(StatLog* log, time_t start, time_t end, int pages, const char* book);

// ---- summaries -----------------------------------------------------------------

// Days are counted in the local calendar: day 0 is 1970-01-01.
int  stats_day_number(int year, int month, int mday);    // month 1..12
void stats_day_date(int day, int* year, int* month, int* mday);
int  stats_today(time_t now);
int  stats_weekday(int day);    // 0 = Monday

typedef struct {
	int day;
	int seconds;     // reading time on this day (sessions over midnight are split)
	int pages;       // pages from sessions that started on this day
	int sessions;    // sessions that started on this day
} StatDay;

typedef struct {
	StatDay* days;    // sorted by day, only days with reading
	int      ndays;
	long     hour_seconds[24];    // reading time by hour of day
	long     weekday_seconds[7];  // by weekday, Monday first
	long     seconds, pages;
	int      sessions;
	int      first_day;           // first day with reading (or -1)
} Stats;

int            stats_build(const StatLog* log, Stats* st);    // 0, or -1 on out of memory
void           stats_clear(Stats* st);
const StatDay* stats_find(const Stats* st, int day);    // NULL: no reading that day

// Current streak: days in a row with reading, ending today (or yesterday, if
// nothing has been read yet today). Longest: the best run ever.
void stats_streaks(const Stats* st, int today, int* current, int* longest);

typedef struct {
	int year, month;
	int days_in_month;
	int seconds, pages, sessions;
	int days_read;
	int books;           // different books read
	int best_day;        // day of the month with the most reading (0 = none)
	int best_seconds;
	int day_seconds[31]; // per day of the month
} StatMonth;

void stats_month(const StatLog* log, const Stats* st, int year, int month, StatMonth* m);

// "1h 05m", "12m", "0m".
void stats_fmt_duration(char* out, size_t n, long seconds);
