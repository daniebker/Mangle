#include "zoom.h"

#include <math.h>

ZoomBase zoom_base(const int rect[4], bool upscale, int sw, int sh)
{
	ZoomBase b = { rect[0], rect[1], rect[2] > 0 ? rect[2] : 1, rect[3] > 0 ? rect[3] : 1, 1.0, sw, sh };
	double   fx = (double) sw / b.bw, fy = (double) sh / b.bh;
	b.s0        = fx < fy ? fx : fy;
	if (!upscale && b.s0 > 1)
		b.s0 = 1;
	return b;
}

void zoom_reset(Zoom* zm)
{
	*zm = (Zoom){ 1.0, 0, 0 };
}

bool zoom_active(const Zoom* zm)
{
	return zm->z > 1.0;
}

double zoom_scale(const Zoom* zm, const ZoomBase* b)
{
	return b->s0 * (zm->z > 1.0 ? zm->z : 1.0);
}

static double clamp_axis(double c, int lo, int len, double half)
{
	if (len <= 2 * half)
		return lo + len / 2.0;
	if (c < lo + half)
		return lo + half;
	if (c > lo + len - half)
		return lo + len - half;
	return c;
}

void zoom_clamp(Zoom* zm, const ZoomBase* b)
{
	if (!zoom_active(zm)) {
		zoom_reset(zm);
		return;
	}
	if (zm->z > ZOOM_MAX)
		zm->z = ZOOM_MAX;
	double s = zoom_scale(zm, b);
	zm->cx   = clamp_axis(zm->cx, b->bx, b->bw, b->sw / (2 * s));
	zm->cy   = clamp_axis(zm->cy, b->by, b->bh, b->sh / (2 * s));
}

bool zoom_pinch(Zoom* zm, const ZoomBase* b, int mx0, int my0, int mx1, int my1, double scale)
{
	Zoom old = *zm;
	if (!zoom_active(zm))
		zm->cx = b->bx + b->bw / 2.0, zm->cy = b->by + b->bh / 2.0, zm->z = 1.0;
	if (!(scale > 0))    // also catches NaN
		scale = 1;
	double s0 = zoom_scale(zm, b);
	// Image point under the starting midpoint.
	double px = zm->cx + (mx0 - b->sw / 2.0) / s0;
	double py = zm->cy + (my0 - b->sh / 2.0) / s0;

	double z = zm->z * scale;
	if (z > ZOOM_MAX)
		z = ZOOM_MAX;
	if (z < ZOOM_SNAP) {
		zoom_reset(zm);
		return zoom_active(&old);
	}
	zm->z     = z;
	double s1 = zoom_scale(zm, b);
	zm->cx    = px - (mx1 - b->sw / 2.0) / s1;
	zm->cy    = py - (my1 - b->sh / 2.0) / s1;
	zoom_clamp(zm, b);
	return !zoom_active(&old) || fabs(zm->z - old.z) > 1e-6 || fabs(zm->cx - old.cx) > 0.5 ||
	       fabs(zm->cy - old.cy) > 0.5;
}

bool zoom_pan(Zoom* zm, const ZoomBase* b, int dx, int dy)
{
	if (!zoom_active(zm))
		return false;
	double ox = zm->cx, oy = zm->cy, s = zoom_scale(zm, b);
	zm->cx -= dx / s;
	zm->cy -= dy / s;
	zoom_clamp(zm, b);
	return fabs(zm->cx - ox) > 0.5 || fabs(zm->cy - oy) > 0.5;
}
