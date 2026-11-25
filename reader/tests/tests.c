// Unit tests for the Kindle-only code: evdev touch decoding (src/touch.c),
// checked together with the shared gesture code it feeds (common/src/tin.c).
// Everything else is tested in common/tests.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tin.h"
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

static struct input_event E(int type, int code, int value)
{
	struct input_event e = { 0 };
	e.type = (unsigned short) type, e.code = (unsigned short) code, e.value = value;
	return e;
}

// Decoder plus gestures, as hal_kindle.c wires them.
typedef struct {
	Touch   t;
	Tin     g;
	InEvent ev;
	bool    got;
	int     n;    // contacts in the last frame
} Rig;

static void rig_init(Rig* r, int minx, int maxx, int miny, int maxy, int w, int h)
{
	memset(r, 0, sizeof(*r));
	touch_init(&r->t, minx, maxx, miny, maxy, w, h);
	tin_init(&r->g, (w < h ? w : h) / 20);
}

// Feed one event at time `now`. Returns true if a gesture completed.
static bool feed(Rig* r, struct input_event e, int64_t now)
{
	TPoint pts[TOUCH_MAX];
	int    n = touch_feed(&r->t, &e, pts);
	if (n < 0)
		return false;
	r->n   = n;
	r->got = tin_feed(&r->g, n, pts, now, &r->ev);
	return r->got;
}

static bool syn(Rig* r, int64_t now)
{
	return feed(r, E(EV_SYN, SYN_REPORT, 0), now);
}

// Protocol B: put contact `slot` (tracking id, -1 = up, -2 = keep) at raw (rx, ry).
static void mt(Rig* r, int slot, int id, int rx, int ry)
{
	feed(r, E(EV_ABS, ABS_MT_SLOT, slot), 0);
	if (id != -2)
		feed(r, E(EV_ABS, ABS_MT_TRACKING_ID, id), 0);
	if (id != -1) {
		feed(r, E(EV_ABS, ABS_MT_POSITION_X, rx), 0);
		feed(r, E(EV_ABS, ABS_MT_POSITION_Y, ry), 0);
	}
}

static void test_touch(void)
{
	Rig r;
	rig_init(&r, 0, 4095, 0, 4095, 1000, 2000);
	// Protocol B tap.
	feed(&r, E(EV_ABS, ABS_MT_SLOT, 0), 0);
	feed(&r, E(EV_ABS, ABS_MT_TRACKING_ID, 7), 0);
	feed(&r, E(EV_ABS, ABS_MT_POSITION_X, 1024), 0);
	feed(&r, E(EV_ABS, ABS_MT_POSITION_Y, 2048), 0);
	CHECK(!syn(&r, 0) && r.n == 1);
	feed(&r, E(EV_ABS, ABS_MT_TRACKING_ID, -1), 100);
	CHECK(syn(&r, 100) && r.ev.kind == IN_TAP && r.n == 0);
	CHECK(r.ev.x == 250 && r.ev.y == 1000);

	// Long press via BTN_TOUCH (single-touch device): fires while held.
	rig_init(&r, 0, 4095, 0, 4095, 1000, 2000);
	feed(&r, E(EV_KEY, BTN_TOUCH, 1), 0);
	feed(&r, E(EV_ABS, ABS_X, 0), 0);
	feed(&r, E(EV_ABS, ABS_Y, 0), 0);
	CHECK(!syn(&r, 0));
	CHECK(tin_waiting(&r.g));
	CHECK(!syn(&r, 300));
	CHECK(syn(&r, TIN_LONG_MS) && r.ev.kind == IN_LONG && r.ev.x == 0);
	feed(&r, E(EV_KEY, BTN_TOUCH, 0), 900);
	CHECK(!syn(&r, 900));    // the release is part of the long press
	// A swipe is a drag.
	feed(&r, E(EV_KEY, BTN_TOUCH, 1), 1000);
	feed(&r, E(EV_ABS, ABS_X, 0), 1000);
	syn(&r, 1000);
	feed(&r, E(EV_ABS, ABS_X, 3000), 1050);
	syn(&r, 1050);
	CHECK(!tin_waiting(&r.g));
	feed(&r, E(EV_KEY, BTN_TOUCH, 0), 1100);
	CHECK(syn(&r, 1100) && r.ev.kind == IN_DRAG);
	CHECK(r.ev.x == 0 && r.ev.y == 0 && r.ev.dx == 732 && r.ev.dy == 0);

	// Protocol B pinch: two fingers spread to twice their distance.
	rig_init(&r, 0, 999, 0, 1999, 1000, 2000);
	mt(&r, 0, 1, 400, 1000);
	CHECK(!syn(&r, 0));
	mt(&r, 1, 2, 600, 1000);
	CHECK(!syn(&r, 10) && r.n == 2);
	mt(&r, 0, -2, 300, 1100);
	mt(&r, 1, -2, 700, 1100);
	CHECK(!syn(&r, 50));
	mt(&r, 1, -1, 0, 0);
	CHECK(syn(&r, 80) && r.ev.kind == IN_PINCH);
	CHECK(r.ev.x == 500 && r.ev.y == 1000 && r.ev.dx == 0 && r.ev.dy == 100);
	CHECK(r.ev.scale > 1.99f && r.ev.scale < 2.01f);
	// The finger left down is not a tap or drag when it lifts.
	mt(&r, 0, -2, 100, 100);
	CHECK(!syn(&r, 90));
	mt(&r, 0, -1, 0, 0);
	CHECK(!syn(&r, 100));
	// ... and the next single touch is a tap again.
	mt(&r, 0, 3, 100, 100);
	syn(&r, 200);
	mt(&r, 0, -1, 0, 0);
	CHECK(syn(&r, 250) && r.ev.kind == IN_TAP && r.ev.x == 100 && r.ev.y == 100);
	// Single-touch ABS_X alongside MT axes doesn't clobber contact 0.
	mt(&r, 0, 4, 200, 200);
	feed(&r, E(EV_ABS, ABS_X, 900), 0);
	syn(&r, 300);
	mt(&r, 0, -1, 0, 0);
	CHECK(syn(&r, 350) && r.ev.kind == IN_TAP && r.ev.x == 200);

	// Protocol A: contact = frame carries position data.
	rig_init(&r, 0, 999, 0, 1999, 1000, 2000);
	r.t.proto_a = true;
	feed(&r, E(EV_ABS, ABS_MT_POSITION_X, 100), 0);
	feed(&r, E(EV_ABS, ABS_MT_POSITION_Y, 200), 0);
	feed(&r, E(EV_SYN, SYN_MT_REPORT, 0), 0);
	syn(&r, 0);
	feed(&r, E(EV_SYN, SYN_MT_REPORT, 0), 80);
	CHECK(syn(&r, 80) && r.ev.kind == IN_TAP && r.ev.x == 100 && r.ev.y == 200);
	// Protocol A pinch (fingers squeezed to half their distance).
	for (int f = 0; f < 3; f++) {
		int d = f == 0 ? 200 : 100;
		feed(&r, E(EV_ABS, ABS_MT_POSITION_X, 500 - d), 0);
		feed(&r, E(EV_ABS, ABS_MT_POSITION_Y, 1000), 0);
		feed(&r, E(EV_SYN, SYN_MT_REPORT, 0), 0);
		if (f < 2) {
			feed(&r, E(EV_ABS, ABS_MT_POSITION_X, 500 + d), 0);
			feed(&r, E(EV_ABS, ABS_MT_POSITION_Y, 1000), 0);
			feed(&r, E(EV_SYN, SYN_MT_REPORT, 0), 0);
		}
		bool done = syn(&r, f * 20);
		CHECK(done == (f == 2));
	}
	CHECK(r.ev.kind == IN_PINCH && r.ev.scale > 0.49f && r.ev.scale < 0.51f && r.ev.x == 500);
	feed(&r, E(EV_SYN, SYN_MT_REPORT, 0), 80);
	CHECK(!syn(&r, 80));

	// Quirks.
	Touch t;
	int   x, y;
	touch_init(&t, 0, 99, 0, 199, 100, 200);
	touch_quirks(&t, "mirror_x");
	touch_map(&t, 0, 0, &x, &y);
	CHECK(x == 99 && y == 0);
	touch_init(&t, 0, 199, 0, 99, 100, 200);
	touch_quirks(&t, "swap");
	touch_map(&t, 199, 50, &x, &y);
	CHECK(x == 50 && y == 199);
}

int main(void)
{
	test_touch();
	printf("%d/%d checks passed\n", checks - fails, checks);
	return fails ? 1 : 0;
}
