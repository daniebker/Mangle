// Desktop simulator input: one command per line on stdin.
//   tap X Y      tap at screen coordinates
//   long X Y     long press
//   drag X Y DX DY            one-finger drag from X,Y by DX,DY
//   pinch X Y SCALE [DX DY]   two-finger pinch around X,Y (SCALE > 1 spreads
//                             the fingers = zoom in); the midpoint moves by DX,DY
//   key CODE     key press (linux/input.h code, e.g. 109 = KEY_PAGEDOWN)
//   sleep MS     pause before reading the next command
//   quit         same as a signal: exit cleanly
// EOF also quits.
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "input.h"

int  input_open(int w, int h) { (void) w, (void) h; return 0; }
void input_close(void) {}
void input_release_grabs(void) {}

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

int input_wait(InEvent* ev, int timeout_ms)
{
	int64_t end = timeout_ms >= 0 ? now_ms() + timeout_ms : 0;
	for (;;) {
		char line[256];
		while (next_line(line, sizeof(line))) {
			int a, b;
			if (sscanf(line, "tap %d %d", &a, &b) == 2) {
				*ev = (InEvent){ .kind = IN_TAP, .x = a, .y = b };
				return 1;
			}
			if (sscanf(line, "long %d %d", &a, &b) == 2) {
				*ev = (InEvent){ .kind = IN_LONG, .x = a, .y = b };
				return 1;
			}
			int   c, d;
			float f;
			if (sscanf(line, "drag %d %d %d %d", &a, &b, &c, &d) == 4) {
				*ev = (InEvent){ IN_DRAG, a, b, 0, c, d, 1.0f };
				return 1;
			}
			int np = sscanf(line, "pinch %d %d %f %d %d", &a, &b, &f, &c, &d);
			if (np == 3 || np == 5) {
				*ev = (InEvent){ IN_PINCH, a, b, 0, np == 5 ? c : 0, np == 5 ? d : 0, f };
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
				if (timeout_ms >= 0 && now_ms() >= end)
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
			int64_t l = end - now_ms();
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
