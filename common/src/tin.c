#include "tin.h"

#include <math.h>
#include <string.h>

void tin_init(Tin* t, int move_px)
{
	memset(t, 0, sizeof(*t));
	t->move_px = move_px > 0 ? move_px : 1;
}

TPoint tin_rotate(int rot, int w, int h, int x, int y)
{
	switch (rot & 3) {
		case 1: return (TPoint){ y, w - 1 - x };
		case 2: return (TPoint){ w - 1 - x, h - 1 - y };
		case 3: return (TPoint){ h - 1 - y, x };
		default: return (TPoint){ x, y };
	}
}

bool tin_waiting(const Tin* t)
{
	return t->state == TIN_ONE && t->maxmove < t->move_px;
}

static double dist(const TPoint* p)
{
	return hypot(p[1].x - p[0].x, p[1].y - p[0].y);
}

bool tin_feed(Tin* t, int n, const TPoint* pts, int64_t now, InEvent* ev)
{
	switch (t->state) {
		case TIN_IDLE:
			if (n >= 2) {
				t->p0[0] = t->p1[0] = pts[0];
				t->p0[1] = t->p1[1] = pts[1];
				t->state            = TIN_PINCH;
			} else if (n == 1) {
				t->sx = t->lx = pts[0].x;
				t->sy = t->ly = pts[0].y;
				t->maxmove    = 0;
				t->t_down     = now;
				t->state      = TIN_ONE;
			}
			return false;

		case TIN_ONE:
			if (n >= 2) {    // second finger: a pinch from where the first one is now
				t->p0[0] = t->p1[0] = (TPoint){ t->lx, t->ly };
				t->p0[1] = t->p1[1] = pts[1];
				t->state            = TIN_PINCH;
				return false;
			}
			if (n == 1) {
				t->lx  = pts[0].x, t->ly = pts[0].y;
				int mv = (int) hypot(t->lx - t->sx, t->ly - t->sy);
				if (mv > t->maxmove)
					t->maxmove = mv;
				if (t->maxmove < t->move_px && now - t->t_down >= TIN_LONG_MS) {
					*ev      = (InEvent){ .kind = IN_LONG, .x = t->sx, .y = t->sy };
					t->state = TIN_WAIT_UP;
					return true;
				}
				return false;
			}
			t->state = TIN_IDLE;
			if (t->maxmove < t->move_px)
				*ev = (InEvent){ .kind = IN_TAP, .x = t->sx, .y = t->sy };
			else
				*ev = (InEvent){ .kind = IN_DRAG, .x = t->sx, .y = t->sy, .dx = t->lx - t->sx, .dy = t->ly - t->sy };
			return true;

		case TIN_PINCH:
			if (n >= 2) {
				t->p1[0] = pts[0];
				t->p1[1] = pts[1];
				return false;
			}
			t->state = n ? TIN_WAIT_UP : TIN_IDLE;
			{
				double d0 = dist(t->p0), d1 = dist(t->p1);
				int    mx0 = (t->p0[0].x + t->p0[1].x) / 2, my0 = (t->p0[0].y + t->p0[1].y) / 2;
				int    mx1 = (t->p1[0].x + t->p1[1].x) / 2, my1 = (t->p1[0].y + t->p1[1].y) / 2;
				*ev = (InEvent){ .kind  = IN_PINCH,
						 .x     = mx0,
						 .y     = my0,
						 .dx    = mx1 - mx0,
						 .dy    = my1 - my0,
						 .scale = d0 > 1 ? (float) (d1 / d0) : 1.0f };
			}
			return true;

		case TIN_WAIT_UP:
			if (n == 0)
				t->state = TIN_IDLE;
			return false;
	}
	return false;
}
