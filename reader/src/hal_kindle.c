// Kindle HAL: FBInk for the e-ink framebuffer, evdev for the touchscreen and
// page-turn buttons, the frontlight in sysfs, suspend to RAM for sleep.
// Drawing goes through common/src/fb.c (a memory framebuffer); gestures
// through common/src/tin.c, as on every other device.
//
//   MANGLE_WFM=gc16|gl16|reagl|du|auto     waveform for normal page turns
//   MANGLE_TOUCH_DEV=/dev/input/event1    force the touch device
//   MANGLE_TOUCH=swap,mirror_x,mirror_y   coordinate quirks for the model
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "fb.h"
#include "fbink.h"
#include "hal.h"
#include "tin.h"
#include "touch.h"

// ---- display -------------------------------------------------------------------------

static int         fbfd = -1;
static FBInkConfig cfg;
static int         scr_w, scr_h;

static WFM_MODE_INDEX_T partial_wfm(void)
{
	// GC16 without flashing is the safe default for gray manga art.
	const char* w = getenv("MANGLE_WFM");
	if (!w)
		return WFM_GC16;
	if (!strcmp(w, "gl16"))
		return WFM_GL16;
	if (!strcmp(w, "reagl"))
		return WFM_REAGL;
	if (!strcmp(w, "du"))
		return WFM_DU;
	if (!strcmp(w, "auto"))
		return WFM_AUTO;
	return WFM_GC16;
}

static int display_open(void)
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.is_quiet     = true;
	cfg.ignore_alpha = true;
	fbfd             = fbink_open();
	if (fbfd < 0) {
		fprintf(stderr, "mangle: fbink_open failed\n");
		return -1;
	}
	if (fbink_init(fbfd, &cfg) < 0) {
		fprintf(stderr, "mangle: fbink_init failed\n");
		fbink_close(fbfd);
		fbfd = -1;
		return -1;
	}
	FBInkState st = { 0 };
	fbink_get_state(&cfg, &st);
	// view_* excludes hidden/overscan rows on some devices; it's what we can draw on.
	scr_w = (int) st.view_width;
	scr_h = (int) st.view_height;
	fprintf(stderr, "mangle: %s, %dx%d, rota %d\n", st.device_name, scr_w, scr_h, st.current_rota);
	return 0;
}

void fb_show(const uint8_t* frame, bool full)
{
	FBInkConfig c = cfg;
	c.is_flashing = full;
	c.wfm_mode    = full ? WFM_GC16 : partial_wfm();
	// Older FBInk releases take a non-const pointer; the data isn't modified.
	fbink_print_raw_data(fbfd, (unsigned char*) frame, scr_w, scr_h, (size_t) scr_w * scr_h, 0, 0, &c);
}

bool hal_has_invert(void)
{
	return false;
}

void hal_set_invert(bool on)
{
	(void) on;
}

// ---- frontlight ------------------------------------------------------------------------

static char bl_path[300];
static int  bl_max;

static void backlight_find(void)
{
	DIR* d = opendir("/sys/class/backlight");
	if (!d)
		return;
	struct dirent* de;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		char p[300];
		snprintf(p, sizeof(p), "/sys/class/backlight/%s/max_brightness", de->d_name);
		FILE* f = fopen(p, "r");
		int   m = 0;
		if (f) {
			if (fscanf(f, "%d", &m) != 1)
				m = 0;
			fclose(f);
		}
		snprintf(p, sizeof(p), "/sys/class/backlight/%s/brightness", de->d_name);
		if (m > 0 && access(p, W_OK) == 0) {
			snprintf(bl_path, sizeof(bl_path), "%s", p);
			bl_max = m;
			break;
		}
	}
	closedir(d);
}

bool hal_has_backlight(void)
{
	return bl_max > 0;
}

void hal_backlight(int pct)
{
	FILE* f = bl_max > 0 ? fopen(bl_path, "w") : NULL;
	if (!f)
		return;
	fprintf(f, "%d\n", (pct * bl_max + 50) / 100);
	fclose(f);
}

// ---- input -------------------------------------------------------------------------------

#define MAXDEV 8
#define BITS_PER_LONG (sizeof(long) * 8)
#define NLONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define TEST_BIT(bit, arr) (((arr)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

typedef struct {
	int  fd;
	bool touch;
	bool grabbed;
} Dev;

static Dev          devs[MAXDEV];
static int          ndev;
static Touch        touch;
static Tin          tin;
static int          last_n;    // contacts in the last touch frame
static TPoint       last_pts[TOUCH_MAX];
static volatile int quit_flag;

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

static bool multi;    // the touchscreen reports more than one contact

static void setup_touch(int fd)
{
	int                  ax = has_abs(fd, ABS_MT_POSITION_X) ? ABS_MT_POSITION_X : ABS_X;
	int                  ay = ax == ABS_X ? ABS_Y : ABS_MT_POSITION_Y;
	struct input_absinfo ix = { 0 }, iy = { 0 };
	ioctl(fd, EVIOCGABS(ax), &ix);
	ioctl(fd, EVIOCGABS(ay), &iy);
	touch_init(&touch, ix.minimum, ix.maximum, iy.minimum, iy.maximum, scr_w, scr_h);
	touch.proto_a = !has_abs(fd, ABS_MT_SLOT) && !has_key(fd, BTN_TOUCH);
	touch_quirks(&touch, getenv("MANGLE_TOUCH"));
	multi = ax == ABS_MT_POSITION_X;
	fprintf(stderr, "mangle: touch x %d..%d y %d..%d%s\n", ix.minimum, ix.maximum, iy.minimum, iy.maximum,
		touch.proto_a ? " (protocol A)" : "");
}

static void add(int fd, bool is_touch_dev, const char* path)
{
	char name[128] = "?";
	ioctl(fd, EVIOCGNAME(sizeof(name)), name);
	Dev* d   = &devs[ndev++];
	d->fd    = fd;
	d->touch = is_touch_dev;
	// Don't grab a device carrying the power button: the Kindle must still
	// be able to sleep.
	d->grabbed = !has_key(fd, KEY_POWER) && ioctl(fd, EVIOCGRAB, 1) == 0;
	fprintf(stderr, "mangle: input %s \"%s\"%s%s\n", path, name, is_touch_dev ? " touch" : " keys",
		d->grabbed ? " (grabbed)" : "");
}

static int input_open(void)
{
	const char* forced     = getenv("MANGLE_TOUCH_DEV");
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
			setup_touch(fd);
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
	// A press that moves less than 1/20 of the screen is a tap.
	tin_init(&tin, (scr_w < scr_h ? scr_w : scr_h) / 20);
	return ndev ? 0 : -1;
}

void hal_release(void)
{
	for (int i = 0; i < ndev; i++)
		if (devs[i].grabbed)
			ioctl(devs[i].fd, EVIOCGRAB, 0);
}

static void input_close(void)
{
	hal_release();
	for (int i = 0; i < ndev; i++)
		close(devs[i].fd);
	ndev = 0;
}

bool hal_multitouch(void)
{
	return multi;
}

// Pending decoded event (one read() can yield several).
static bool    have_pending;
static InEvent pending;

static void feed_tin(int n, const TPoint* pts)
{
	TPoint  r[TOUCH_MAX];
	InEvent ev;
	for (int i = 0; i < n; i++)
		r[i] = fb_rotate_point(pts[i].x, pts[i].y);
	if (tin_feed(&tin, n, r, hal_ms(), &ev) && !have_pending) {
		pending      = ev;
		have_pending = true;
	}
}

static void drain(Dev* d)
{
	struct input_event evs[64];
	ssize_t            n;
	while ((n = read(d->fd, evs, sizeof(evs))) > 0) {
		for (size_t i = 0; i < (size_t) n / sizeof(*evs); i++) {
			const struct input_event* e = &evs[i];
			if (d->touch) {
				TPoint pts[TOUCH_MAX];
				int    k = touch_feed(&touch, e, pts);
				if (k >= 0) {
					last_n = k;
					memcpy(last_pts, pts, sizeof(pts));
					feed_tin(k, pts);
				}
			} else if (e->type == EV_KEY && e->value == 1 && !have_pending) {
				pending      = (InEvent){ .kind = IN_KEY, .code = e->code };
				have_pending = true;
			}
		}
	}
}

int hal_wait(InEvent* ev, int timeout_ms)
{
	int64_t end = timeout_ms >= 0 ? hal_ms() + timeout_ms : 0;
	for (;;) {
		if (quit_flag) {
			*ev = (InEvent){ .kind = IN_QUIT };
			return 1;
		}
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
			int64_t l = end - hal_ms();
			left      = l > 0 ? (int) l : 0;
		}
		// A finger held still sends nothing: look again soon so a long
		// press fires while it is still down.
		bool hold = tin_waiting(&tin);
		if (hold && (left < 0 || left > 50))
			left = 50;
		int r = poll(p, (nfds_t) ndev, left);
		if (r < 0)
			continue;    // a signal: quit_flag is checked at the top
		if (r == 0) {
			if (hold)
				feed_tin(last_n, last_pts);
			if (!have_pending && timeout_ms >= 0 && hal_ms() >= end)
				return 0;
			continue;
		}
		for (int i = 0; i < ndev; i++)
			if (p[i].revents & POLLIN)
				drain(&devs[i]);
	}
}

// ---- system ----------------------------------------------------------------------------

bool hal_can_calibrate(void)
{
	return false;
}

bool hal_needs_calibration(void)
{
	return false;
}

void hal_calibrate(void) {}

bool hal_can_exit(void)
{
	return true;
}

bool hal_clock_ok(void)
{
	return time(NULL) > 1577836800;    // after 2020: the Kindle keeps its clock
}

bool hal_sleep(void)
{
	// Let the e-ink panel finish its last refresh before the CPU stops.
	sleep(2);
	sync();
	FILE* f = fopen("/sys/power/state", "w");
	if (!f)
		return false;
	// Blocks inside the write until something (the power button) wakes the device.
	int rc = fputs("mem", f) < 0 ? -1 : 0;
	if (fclose(f) != 0)
		rc = -1;
	if (rc != 0) {
		fprintf(stderr, "mangle: cannot suspend: %s\n", strerror(errno));
		return false;
	}
	tin_init(&tin, tin.move_px);
	return true;
}

void hal_quit(void)
{
	quit_flag = 1;
}

int hal_open(void)
{
	if (display_open() != 0)
		return -1;
	if (fb_init(scr_w, scr_h) != 0 || input_open() != 0) {
		fbink_close(fbfd);
		return -1;
	}
	backlight_find();
	return 0;
}

void hal_close(void)
{
	input_close();
	fb_free();
	if (fbfd >= 0)
		fbink_close(fbfd);
	fbfd = -1;
}
