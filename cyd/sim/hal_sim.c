// Desktop stand-in for the CYD hardware: keeps the screen in memory, writes it
// as a PGM file after each completed draw (MANGLE_SIM_DIR, if set), and reads
// input commands from stdin like the Kindle simulator:
//   tap X Y | long X Y | drag X Y DX DY | pinch X Y SCALE [DX DY] | sleep MS | quit
// Books come from MANGLE_SIM_LIBRARY (default ./manga), state from
// MANGLE_SIM_STATE (default ./manga/.mangle).
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "hal.h"

#define NATIVE_W 320
#define NATIVE_H 480

uint8_t* sim_fb;    // in the current rotation (used by the tests)
#define fb sim_fb
static int      rot;
static int      frames;
static bool     dirty, night;
static int      touched;    // simulated "sleep" wake swallows a command

int hal_width(void) { return rot & 1 ? NATIVE_H : NATIVE_W; }
int hal_height(void) { return rot & 1 ? NATIVE_W : NATIVE_H; }

void hal_set_rotation(int r)
{
	rot = r & 3;
	memset(fb, 255, NATIVE_W * NATIVE_H);
}

void hal_row(int x, int y, int w, const uint8_t* g)
{
	if (y < 0 || y >= hal_height())
		return;
	if (x < 0)
		g -= x, w += x, x = 0;
	if (x + w > hal_width())
		w = hal_width() - x;
	if (w <= 0)
		return;
	memcpy(fb + (size_t) y * hal_width() + x, g, (size_t) w);
	dirty = true;
}

void hal_fill(int x, int y, int w, int h, uint8_t v)
{
	for (int r = y; r < y + h; r++) {
		int xx = x, ww = w;
		if (r < 0 || r >= hal_height())
			continue;
		if (xx < 0)
			ww += xx, xx = 0;
		if (xx + ww > hal_width())
			ww = hal_width() - xx;
		if (ww > 0)
			memset(fb + (size_t) r * hal_width() + xx, v, (size_t) ww);
	}
	dirty = true;
}

void hal_flush(void)
{
	if (!dirty)
		return;
	dirty           = false;
	const char* dir = getenv("MANGLE_SIM_DIR");
	frames++;
	if (!dir)
		return;
	char path[512];
	snprintf(path, sizeof(path), "%s/frame%04d.pgm", dir, frames);
	FILE* f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P5\n%d %d\n255\n", hal_width(), hal_height());
	for (int i = 0; i < hal_width() * hal_height(); i++)
		fputc(night ? 255 - fb[i] : fb[i], f);
	fclose(f);
	fprintf(stderr, "frame %s\n", path);
}

void hal_set_night(bool on) { night = on; }
void hal_set_invert(bool on) { (void) on; }
void hal_backlight(int pct) { fprintf(stderr, "backlight %d\n", pct); }
bool hal_multitouch(void) { return getenv("MANGLE_SIM_RESISTIVE") == NULL; }
bool hal_needs_calibration(void) { return false; }
bool hal_calibrate(void) { return false; }

const char* hal_library_dir(void)
{
	const char* d = getenv("MANGLE_SIM_LIBRARY");
	return d ? d : "manga";
}

const char* hal_state_dir(void)
{
	static char buf[512];
	const char* d = getenv("MANGLE_SIM_STATE");
	if (d)
		return d;
	snprintf(buf, sizeof(buf), "%s/.mangle", hal_library_dir());
	return buf;
}

int64_t hal_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void hal_log(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("mangle: ", stderr);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

void hal_halt(void)
{
	exit(1);
}

// ---- input -------------------------------------------------------------------------

static char   buf[4096];
static size_t len;

static bool next_line(char* out, size_t outlen)
{
	char* nl = memchr(buf, '\n', len);
	if (!nl)
		return false;
	size_t n = (size_t) (nl - buf);
	if (n >= outlen)
		n = outlen - 1;
	memcpy(out, buf, n);
	out[n] = 0;
	len -= (size_t) (nl - buf) + 1;
	memmove(buf, nl + 1, len);
	return true;
}

int hal_wait(InEvent* ev, int timeout_ms)
{
	hal_flush();
	int64_t end = timeout_ms >= 0 ? hal_ms() + timeout_ms : 0;
	for (;;) {
		char line[256];
		while (next_line(line, sizeof(line))) {
			int   a, b, c, d;
			float f;
			if (sscanf(line, "tap %d %d", &a, &b) == 2) {
				*ev = (InEvent){ .kind = IN_TAP, .x = a, .y = b };
				return 1;
			}
			if (sscanf(line, "long %d %d", &a, &b) == 2) {
				*ev = (InEvent){ .kind = IN_LONG, .x = a, .y = b };
				return 1;
			}
			if (sscanf(line, "drag %d %d %d %d", &a, &b, &c, &d) == 4) {
				*ev = (InEvent){ IN_DRAG, a, b, 0, c, d, 1.0f };
				return 1;
			}
			int np = sscanf(line, "pinch %d %d %f %d %d", &a, &b, &f, &c, &d);
			if (np == 3 || np == 5) {
				*ev = (InEvent){ IN_PINCH, a, b, 0, np == 5 ? c : 0, np == 5 ? d : 0, f };
				return 1;
			}
			if (sscanf(line, "sleep %d", &a) == 1) {
				struct timespec ts = { a / 1000, (a % 1000) * 1000000L };
				while (nanosleep(&ts, &ts) && errno == EINTR) {}
				if (timeout_ms >= 0 && hal_ms() >= end)
					return 0;
				continue;
			}
			if (strncmp(line, "quit", 4) == 0) {
				*ev = (InEvent){ .kind = IN_QUIT };
				return 1;
			}
		}
		int left = -1;
		if (timeout_ms >= 0) {
			int64_t l = end - hal_ms();
			left      = l > 0 ? (int) l : 0;
		}
		struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
		int           r = poll(&p, 1, left);
		if (r < 0)
			return -1;
		if (r == 0)
			return 0;
		ssize_t n = read(STDIN_FILENO, buf + len, sizeof(buf) - 1 - len);
		if (n <= 0) {
			*ev = (InEvent){ .kind = IN_QUIT };
			return 1;
		}
		len += (size_t) n;
	}
}

void hal_sleep_until_touch(void)
{
	fprintf(stderr, "mangle: screen off\n");
	InEvent ev;
	while (hal_wait(&ev, -1) == 1 && ev.kind != IN_QUIT && ev.kind != IN_TAP && ev.kind != IN_LONG) {}
	touched++;
	fprintf(stderr, "mangle: screen on\n");
}

#ifndef SIM_NO_MAIN
int main(void)
{
	fb = malloc(NATIVE_W * NATIVE_H);
	if (!fb)
		return 1;
	memset(fb, 255, NATIVE_W * NATIVE_H);
	app_run();
	free(fb);
	return 0;
}
#endif
