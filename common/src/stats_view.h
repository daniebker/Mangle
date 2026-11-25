// Stats screen: daily reading habits and a monthly overview, drawn through
// the HAL. The app runs the event loop and passes taps and keys in.
#pragma once

#include <stdbool.h>
#include <time.h>

#include "stats.h"

typedef struct {
	StatLog log;
	Stats   st;
	time_t  now;
	int     which;                  // 0 = daily, 1 = monthly
	int     mi, cur_mi, first_mi;   // months as year * 12 + month - 1
	// Layout of the last draw (for hit tests).
	int bar_h, back_end, daily_x, monthly_x, foot_y, w;
	int H_foot;    // footer text offset
} StatsView;

// Load the log; `live` (may be NULL) is the session in progress, counted too.
void stats_view_open(StatsView* v, const char* dir, const StatRec* live, time_t now);
void stats_view_close(StatsView* v);
void stats_view_draw(StatsView* v);

typedef enum { SV_NONE, SV_REDRAW, SV_TAB, SV_CLOSE } SvResult;
SvResult stats_view_tap(StatsView* v, int x, int y);
// forward/back (page-turn keys): previous/next month on the monthly tab.
SvResult stats_view_step(StatsView* v, int dir);
