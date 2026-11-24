// evdev input backend (Kindle). Finds the touchscreen and any page-turn
// buttons under /dev/input, grabs them exclusively (EVIOCGRAB) so the paused
// Kindle UI doesn't see our taps, and decodes taps/long presses.
//
//   MANGLE_TOUCH_DEV=/dev/input/event1    force the touch device
//   MANGLE_TOUCH=swap,mirror_x,mirror_y   coordinate quirks for the model
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "input.h"
#include "touch.h"

#define MAXDEV 8
#define BITS_PER_LONG (sizeof(long) * 8)
#define NLONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define TEST_BIT(bit, arr) (((arr)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

typedef struct {
	int  fd;
	bool touch;
	bool grabbed;
} Dev;

static Dev   devs[MAXDEV];
static int   ndev;
static Touch touch;

static bool has_key(int fd, int code)
{
	unsigned long bits[NLONGS(KEY_CNT)] = { 0 };
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0)
		return false;
	return TEST_BIT(code, bits);
}

static bool has_abs(int fd, int code)
{
	unsigned long bits[NLONGS(ABS_CNT)] = { 0 };
	if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0)
		return false;
	return TEST_BIT(code, bits);
}

static bool is_touch(int fd)
{
	return has_abs(fd, ABS_MT_POSITION_X) || (has_abs(fd, ABS_X) && has_key(fd, BTN_TOUCH));
}

static bool has_page_keys(int fd)
{
	static const int keys[] = { KEY_PAGEUP, KEY_PAGEDOWN, KEY_NEXTSONG, KEY_PREVIOUSSONG, KEY_LEFT, KEY_RIGHT };
	for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); i++)
		if (has_key(fd, keys[i]))
			return true;
	return false;
}

static void setup_touch(int fd, int w, int h)
{
	int                  ax = has_abs(fd, ABS_MT_POSITION_X) ? ABS_MT_POSITION_X : ABS_X;
	int                  ay = ax == ABS_X ? ABS_Y : ABS_MT_POSITION_Y;
	struct input_absinfo ix = { 0 }, iy = { 0 };
	ioctl(fd, EVIOCGABS(ax), &ix);
	ioctl(fd, EVIOCGABS(ay), &iy);
	touch_init(&touch, ix.minimum, ix.maximum, iy.minimum, iy.maximum, w, h);
	touch.proto_a = !has_abs(fd, ABS_MT_SLOT) && !has_key(fd, BTN_TOUCH);
	touch_quirks(&touch, getenv("MANGLE_TOUCH"));
	fprintf(stderr, "mangle: touch x %d..%d y %d..%d%s\n", ix.minimum, ix.maximum, iy.minimum, iy.maximum,
		touch.proto_a ? " (protocol A)" : "");
}

static void add(int fd, bool is_touch_dev, const char* path)
{
	char name[128] = "?";
	ioctl(fd, EVIOCGNAME(sizeof(name)), name);
	Dev* d     = &devs[ndev++];
	d->fd      = fd;
	d->touch   = is_touch_dev;
	// Don't grab a device carrying the power button: the Kindle must still
	// be able to sleep.
	d->grabbed = !has_key(fd, KEY_POWER) && ioctl(fd, EVIOCGRAB, 1) == 0;
	fprintf(stderr, "mangle: input %s \"%s\"%s%s\n", path, name, is_touch_dev ? " touch" : " keys",
		d->grabbed ? " (grabbed)" : "");
}

int input_open(int w, int h)
{
	const char* forced = getenv("MANGLE_TOUCH_DEV");
	bool        have_touch = false;
	DIR*        dir        = opendir("/dev/input");
	if (!dir) {
		perror("mangle: /dev/input");
		return -1;
	}
	struct dirent* de;
	while ((de = readdir(dir)) && ndev < MAXDEV) {
		if (strncmp(de->d_name, "event", 5) != 0)
			continue;
		char path[300];
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		bool t = forced ? strcmp(forced, path) == 0 : (!have_touch && is_touch(fd));
		if (t) {
			setup_touch(fd, w, h);
			have_touch = true;
			add(fd, true, path);
		} else if (has_page_keys(fd)) {
			add(fd, false, path);
		} else {
			close(fd);
		}
	}
	closedir(dir);
	if (!have_touch)
		fprintf(stderr, "mangle: no touchscreen found (set MANGLE_TOUCH_DEV)\n");
	return ndev ? 0 : -1;
}

void input_release_grabs(void)
{
	for (int i = 0; i < ndev; i++)
		if (devs[i].grabbed)
			ioctl(devs[i].fd, EVIOCGRAB, 0);
}

void input_close(void)
{
	input_release_grabs();
	for (int i = 0; i < ndev; i++)
		close(devs[i].fd);
	ndev = 0;
}

// Pending decoded event (one read() can yield several input_events).
static bool    have_pending;
static InEvent pending;

static bool drain(Dev* d)
{
	struct input_event evs[64];
	ssize_t            n;
	while ((n = read(d->fd, evs, sizeof(evs))) > 0) {
		for (size_t i = 0; i < (size_t) n / sizeof(*evs); i++) {
			const struct input_event* e = &evs[i];
			if (d->touch) {
				int         x, y;
				TouchResult r = touch_feed(&touch, e, now_ms(), &x, &y);
				if (r != TOUCH_NONE && !have_pending) {
					pending      = (InEvent){ r == TOUCH_TAP ? IN_TAP : IN_LONG, x, y, 0 };
					have_pending = true;
				}
			} else if (e->type == EV_KEY && e->value == 1 && !have_pending) {
				pending      = (InEvent){ IN_KEY, 0, 0, e->code };
				have_pending = true;
			}
		}
	}
	return have_pending;
}

int input_wait(InEvent* ev, int timeout_ms)
{
	int64_t end = timeout_ms >= 0 ? now_ms() + timeout_ms : 0;
	for (;;) {
		if (have_pending) {
			*ev          = pending;
			have_pending = false;
			return 1;
		}
		struct pollfd p[MAXDEV];
		for (int i = 0; i < ndev; i++)
			p[i] = (struct pollfd){ devs[i].fd, POLLIN, 0 };
		int left = -1;
		if (timeout_ms >= 0) {
			int64_t l = end - now_ms();
			left      = l > 0 ? (int) l : 0;
		}
		int r = poll(p, (nfds_t) ndev, left);
		if (r < 0)
			return errno == EINTR ? -1 : -1;
		if (r == 0)
			return 0;
		for (int i = 0; i < ndev; i++)
			if (p[i].revents & POLLIN)
				drain(&devs[i]);
	}
}
