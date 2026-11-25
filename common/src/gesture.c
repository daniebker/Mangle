#include "gesture.h"

#include <linux/input.h>    // key codes (common/src/compat has them where Linux headers are missing)

Zone gesture_zone(int x, int w)
{
	if (x < w / 3)
		return ZONE_LEFT;
	if (x >= w - w / 3)
		return ZONE_RIGHT;
	return ZONE_CENTRE;
}

int gesture_tap(Gesture* g, int x, int y, int w, int h, bool rtl, int64_t now, Action out[2])
{
	int  n = 0;
	Zone z = gesture_zone(x, w);

	if (y < h / MENU_ZONE_DIV) {
		if (g->pending) {
			g->pending = false;
			out[n++]   = ACT_FORWARD;
		}
		out[n++] = ACT_MENU;
		return n;
	}

	if (z == ZONE_CENTRE) {
		if (g->pending && now <= g->deadline) {
			g->pending = false;
			out[n++]   = ACT_TOGGLE;
		} else {
			if (g->pending)    // stale: resolve it before starting a new wait
				out[n++] = ACT_FORWARD;
			g->pending  = true;
			g->deadline = now + DOUBLE_TAP_MS;
		}
		return n;
	}

	if (g->pending) {
		g->pending = false;
		out[n++]   = ACT_FORWARD;
	}
	bool left = z == ZONE_LEFT;
	out[n++]  = (left == rtl) ? ACT_FORWARD : ACT_BACK;
	return n;
}

Action gesture_long(Gesture* g, int x, int w)
{
	g->pending = false;
	return gesture_zone(x, w) == ZONE_CENTRE ? ACT_EXIT : ACT_ZOOM;
}

Action gesture_key(Gesture* g, int code, bool rtl)
{
	g->pending = false;
	switch (code) {
		// Page-turn buttons (Oasis etc.) are semantic: next/previous page.
		case KEY_PAGEDOWN:
		case KEY_NEXTSONG:
		case KEY_NEXT:
		case KEY_SPACE:
			return ACT_FORWARD;
		case KEY_PAGEUP:
		case KEY_PREVIOUSSONG:
		case KEY_PREVIOUS:
		case KEY_BACKSPACE:
			return ACT_BACK;
		// Arrow keys follow the screen: in RTL, left moves forward.
		case KEY_LEFT:
			return rtl ? ACT_FORWARD : ACT_BACK;
		case KEY_RIGHT:
			return rtl ? ACT_BACK : ACT_FORWARD;
		case KEY_ENTER:
		case KEY_KPENTER:
		case KEY_SELECT:
			return ACT_TOGGLE;
		case KEY_HOME:
		case KEY_HOMEPAGE:
		case KEY_MENU:
		case KEY_BACK:
		case KEY_ESC:
		case KEY_Q:
			return ACT_EXIT;
		case KEY_R:
			return ACT_REFRESH;
		default:
			return ACT_NONE;
	}
}

int gesture_timeout(const Gesture* g, int64_t now)
{
	if (!g->pending)
		return -1;
	int64_t left = g->deadline - now;
	return left > 0 ? (int) left : 0;
}

Action gesture_expire(Gesture* g, int64_t now)
{
	if (g->pending && now >= g->deadline) {
		g->pending = false;
		return ACT_FORWARD;
	}
	return ACT_NONE;
}
