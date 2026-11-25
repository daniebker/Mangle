// Desktop simulator HAL: runs the real app with frames written as PGM files
// and input read from stdin, so every device's behaviour can be tested here.
//
//   MANGLE_SIM_DEVICE=kindle|cyd35r|cyd35c|freenove35   what to behave like (default kindle)
//   MANGLE_SIM_SCREEN=WxH                    physical screen (default 600x800)
//   MANGLE_SIM_DIR=frames                    write frame_NNNN.pgm there (unset = don't)
//   MANGLE_SIM_NOCLOCK=1                     no date (a CYD without Wi-Fi time)
//
// stdout gets one line per screen: "FRAME n full|partial path", plus SCREEN,
// SUSPEND, BACKLIGHT. Input, one command per line:
//   tap X Y | long X Y | drag X Y DX DY | pinch X Y SCALE [DX DY] | key CODE
//   button (the ESP32 boards' BOOT button: sleep)
//   sleep MS (pause; lets timeouts fire) | quit (EOF also quits)
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fb.h"
#include "hal.h"

static int  pw = 600, ph = 800, frames;
static bool cyd, multitouch = true;
static volatile int quit_flag;

int hal_open(void)
{
	const char* d = getenv("MANGLE_SIM_DEVICE");
	if (d && (strncmp(d, "cyd", 3) == 0 || strcmp(d, "freenove35") == 0)) {
		cyd        = true;    // an ESP32 board: LCD, no exit, invert panel setting
		multitouch = strcmp(d, "cyd35r") != 0;
		pw = 320, ph = 480;
	}
	const char* e = getenv("MANGLE_SIM_SCREEN");
	if (e && sscanf(e, "%dx%d", &pw, &ph) != 2)
		return -1;
	if (fb_init(pw, ph) != 0)
		return -1;
	printf("SCREEN %dx%d %s\n", pw, ph, d ? d : "kindle");
	fflush(stdout);
	return 0;
}

void hal_close(void)
{
	fb_free();
}

void hal_quit(void)
{
	quit_flag = 1;
}

void hal_release(void) {}

void fb_show(const uint8_t* frame, bool full)
{
	frames++;
	const char* dir  = getenv("MANGLE_SIM_DIR");
	char        path[4096] = "";
	if (dir) {
		snprintf(path, sizeof(path), "%s/frame_%04d.pgm", dir, frames);
		FILE* f = fopen(path, "wb");
		if (f) {
			fprintf(f, "P5\n%d %d\n255\n", pw, ph);
			fwrite(frame, 1, (size_t) pw * ph, f);
			fclose(f);
		}
	}
	printf("FRAME %d %s %s\n", frames, full ? "full" : "partial", path);
	fflush(stdout);
}

bool hal_has_backlight(void)
{
	return true;
}

void hal_backlight(int pct)
{
	printf("BACKLIGHT %d\n", pct);
	fflush(stdout);
}

bool hal_has_invert(void)
{
	return cyd;
}

void hal_set_invert(bool on)
{
	(void) on;
}

bool hal_multitouch(void)
{
	return multitouch;
}

bool hal_can_calibrate(void)
{
	return cyd && !multitouch;
}

bool hal_needs_calibration(void)
{
	return false;
}

void hal_calibrate(void)
{
	printf("CALIBRATE\n");
	fflush(stdout);
}

bool hal_can_exit(void)
{
	return !cyd;
}

bool hal_clock_ok(void)
{
	return !getenv("MANGLE_SIM_NOCLOCK");
}

bool hal_sleep(bool by_button)
{
	printf("SUSPEND%s\n", by_button ? " button" : "");
	fflush(stdout);
	return true;
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
	int64_t end = timeout_ms >= 0 ? hal_ms() + timeout_ms : 0;
	for (;;) {
		if (quit_flag) {
			*ev = (InEvent){ .kind = IN_QUIT };
			return 1;
		}
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
			if ((np == 3 || np == 5) && multitouch) {
				*ev = (InEvent){ IN_PINCH, a, b, 0, np == 5 ? c : 0, np == 5 ? d : 0, f };
				return 1;
			}
			if (strncmp(line, "button", 6) == 0 && cyd) {
				*ev = (InEvent){ .kind = IN_SLEEP };
				return 1;
			}
			if (sscanf(line, "key %d", &a) == 1) {
				*ev = (InEvent){ .kind = IN_KEY, .code = a };
				return 1;
			}
			if (sscanf(line, "sleep %d", &a) == 1) {
				struct timespec ts = { a / 1000, (a % 1000) * 1000000L };
				while (nanosleep(&ts, &ts) && errno == EINTR) {}
				// A sleep is "time passing": let the caller's timeout fire.
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
			continue;    // a signal: quit_flag is checked at the top
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
