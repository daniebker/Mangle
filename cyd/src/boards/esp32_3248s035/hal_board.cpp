// Board layer for the ESP32-3248S035 (3.5" Cheap Yellow Display, ESP32):
// ST7796 display on SPI, XPT2046 (resistive) or GT911 (capacitive) touch,
// SD card on SPI. The rest of the ESP32 side is cyd/src/esp32/esp32_hal.cpp.
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <math.h>
#include <string.h>

#ifdef TOUCH_GT911
#include <Wire.h>
#endif

#include "board.h"
#include "esp32_board.h"
#include "hal.h"
extern "C" {
#include "store.h"
#include "ui.h"
}

static SPIClass lcd_spi(HSPI);
static SPIClass sd_spi(VSPI);

static int      rotation;
static bool     night, invert_panel;
static uint16_t lut[256];    // gray -> RGB565, big-endian (as sent)

// ---- display ------------------------------------------------------------------------

static inline void lcd_begin(void)
{
	lcd_spi.beginTransaction(SPISettings(LCD_SPI_HZ, MSBFIRST, SPI_MODE0));
	digitalWrite(LCD_CS, LOW);
}

static inline void lcd_end(void)
{
	digitalWrite(LCD_CS, HIGH);
	lcd_spi.endTransaction();
}

static void lcd_cmd(uint8_t c)
{
	digitalWrite(LCD_DC, LOW);
	lcd_spi.write(c);
	digitalWrite(LCD_DC, HIGH);
}

static void lcd_cmd_data(uint8_t c, const uint8_t* d, int n)
{
	lcd_cmd(c);
	if (n)
		lcd_spi.writeBytes(d, n);
}

static void lcd_window(int x, int y, int w, int h)
{
	uint8_t ca[4] = { (uint8_t) (x >> 8), (uint8_t) x, (uint8_t) ((x + w - 1) >> 8), (uint8_t) (x + w - 1) };
	uint8_t ra[4] = { (uint8_t) (y >> 8), (uint8_t) y, (uint8_t) ((y + h - 1) >> 8), (uint8_t) (y + h - 1) };
	lcd_cmd_data(0x2A, ca, 4);
	lcd_cmd_data(0x2B, ra, 4);
	lcd_cmd(0x2C);
}

static void build_lut(void)
{
	for (int i = 0; i < 256; i++) {
		int      g   = night ? 255 - i : i;
		uint16_t c   = (uint16_t) (((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
		lut[i]       = (uint16_t) ((c >> 8) | (c << 8));    // byte order for the wire
	}
}

static void flush_rows(void);
static void touch_init(void);

void board_init(void)
{
	pinMode(LCD_CS, OUTPUT);
	pinMode(LCD_DC, OUTPUT);
	digitalWrite(LCD_CS, HIGH);
	digitalWrite(LCD_DC, HIGH);
	lcd_spi.begin(LCD_SCLK, LCD_MISO, LCD_MOSI, -1);

	// ST7796S initialisation (as used by common CYD 3.5" setups).
	struct {
		uint8_t cmd, n;
		uint8_t d[14];
		uint16_t wait;
	} seq[] = {
		{ 0x01, 0, { 0 }, 120 },                // software reset
		{ 0x11, 0, { 0 }, 120 },                // sleep out
		{ 0xF0, 1, { 0xC3 }, 0 },               // command set control: enable part 1
		{ 0xF0, 1, { 0x96 }, 0 },               // enable part 2
		{ 0x3A, 1, { 0x55 }, 0 },               // 16 bits per pixel
		{ 0xB4, 1, { 0x01 }, 0 },               // 1-dot inversion
		{ 0xB6, 3, { 0x80, 0x02, 0x3B }, 0 },   // display function control
		{ 0xE8, 8, { 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33 }, 0 },
		{ 0xC1, 1, { 0x06 }, 0 },
		{ 0xC2, 1, { 0xA7 }, 0 },
		{ 0xC5, 1, { 0x18 }, 120 },             // VCOM
		{ 0xE0, 14, { 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B }, 0 },
		{ 0xE1, 14, { 0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B }, 120 },
		{ 0xF0, 1, { 0x3C }, 0 },               // command set control: disable
		{ 0xF0, 1, { 0x69 }, 120 },
	};
	for (size_t i = 0; i < sizeof(seq) / sizeof(*seq); i++) {
		lcd_begin();
		lcd_cmd_data(seq[i].cmd, seq[i].d, seq[i].n);
		lcd_end();
		if (seq[i].wait)
			delay(seq[i].wait);
	}
	hal_set_rotation(0);
	hal_set_invert(false);
	build_lut();
	hal_fill(0, 0, NATIVE_W, NATIVE_H, 255);
	flush_rows();
	lcd_begin();
	lcd_cmd(0x29);    // display on
	lcd_end();
	touch_init();
}

int hal_width(void)
{
	return rotation & 1 ? NATIVE_H : NATIVE_W;
}

int hal_height(void)
{
	return rotation & 1 ? NATIVE_W : NATIVE_H;
}

// Rows waiting to be sent: a window [px, px+pw) x [py, py+pn).
enum { BATCH = 16 };
static uint16_t pending[NATIVE_H * BATCH];
static int      px, py, pw, pn;

static void flush_rows(void)
{
	if (!pn)
		return;
	lcd_begin();
	lcd_window(px, py, pw, pn);
	lcd_spi.writeBytes((const uint8_t*) pending, (uint32_t) pw * pn * 2);
	lcd_end();
	pn = 0;
}

void hal_set_rotation(int r)
{
	static const uint8_t mad[4] = LCD_MADCTL;
	flush_rows();
	rotation = r & 3;
	lcd_begin();
	lcd_cmd_data(0x36, &mad[rotation], 1);
	lcd_end();
}

void hal_row(int x, int y, int w, const uint8_t* g)
{
	int sw = hal_width();
	if (y < 0 || y >= hal_height())
		return;
	if (x < 0)
		g -= x, w += x, x = 0;
	if (x + w > sw)
		w = sw - x;
	if (w <= 0)
		return;
	if (pn && (x != px || w != pw || y != py + pn || pn == BATCH))
		flush_rows();
	if (!pn)
		px = x, py = y, pw = w;
	uint16_t* o = pending + (size_t) pn * w;
	for (int i = 0; i < w; i++)
		o[i] = lut[g[i]];
	pn++;
}

void hal_fill(int x, int y, int w, int h, uint8_t v)
{
	if (x < 0)
		w += x, x = 0;
	if (y < 0)
		h += y, y = 0;
	if (x + w > hal_width())
		w = hal_width() - x;
	if (y + h > hal_height())
		h = hal_height() - y;
	if (w <= 0 || h <= 0)
		return;
	flush_rows();
	uint16_t c = lut[v];
	for (int i = 0; i < NATIVE_H * BATCH; i++)
		pending[i] = c;
	lcd_begin();
	lcd_window(x, y, w, h);
	uint32_t left = (uint32_t) w * h * 2;
	while (left) {
		uint32_t k = left < sizeof(pending) ? left : sizeof(pending);
		lcd_spi.writeBytes((const uint8_t*) pending, k);
		left -= k;
	}
	lcd_end();
}

void hal_present(bool full)
{
	(void) full;    // an LCD has no ghosting to clear
	flush_rows();
}

void hal_set_night(bool on)
{
	flush_rows();
	night = on;
	build_lut();
}

bool hal_has_invert(void)
{
	return true;
}

void hal_set_invert(bool on)
{
	flush_rows();
	invert_panel = on;
	lcd_begin();
	lcd_cmd(on ? 0x21 : 0x20);
	lcd_end();
}

// ---- touch ----------------------------------------------------------------------------

// Contacts in screen coordinates for the current rotation, from portrait ones.
static TPoint rotate_point(int x, int y)
{
	return tin_rotate(rotation, NATIVE_W, NATIVE_H, x, y);
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

#ifdef TOUCH_GT911

static uint8_t gt_addr;
static int     gt_xmax = NATIVE_W, gt_ymax = NATIVE_H;
static int     gt_n;                  // contacts in the last report
static TPoint  gt_pts[2];             // portrait coordinates

static bool gt_read(uint16_t reg, uint8_t* buf, int n)
{
	Wire.beginTransmission(gt_addr);
	Wire.write((uint8_t) (reg >> 8));
	Wire.write((uint8_t) reg);
	if (Wire.endTransmission(false) != 0)
		return false;
	if (Wire.requestFrom((int) gt_addr, n) != n)
		return false;
	for (int i = 0; i < n; i++)
		buf[i] = (uint8_t) Wire.read();
	return true;
}

static void gt_write8(uint16_t reg, uint8_t v)
{
	Wire.beginTransmission(gt_addr);
	Wire.write((uint8_t) (reg >> 8));
	Wire.write((uint8_t) reg);
	Wire.write(v);
	Wire.endTransmission();
}

static void touch_init(void)
{
	// Reset with INT held low: the controller takes address 0x5D.
	pinMode(GT911_INT, OUTPUT);
	pinMode(GT911_RST, OUTPUT);
	digitalWrite(GT911_INT, LOW);
	digitalWrite(GT911_RST, LOW);
	delay(10);
	digitalWrite(GT911_RST, HIGH);
	delay(10);
	pinMode(GT911_INT, INPUT);
	delay(60);
	Wire.begin(GT911_SDA, GT911_SCL, 400000);
	uint8_t cfg[4];
	const uint8_t addrs[2] = { 0x5D, 0x14 };
	for (int i = 0; i < 2; i++) {
		gt_addr = addrs[i];
		if (gt_read(0x8048, cfg, 4)) {
			int xm = cfg[0] | cfg[1] << 8, ym = cfg[2] | cfg[3] << 8;
			if (xm > 0 && ym > 0)
				gt_xmax = xm, gt_ymax = ym;
			hal_log("GT911 at 0x%02X, %dx%d\n", gt_addr, gt_xmax, gt_ymax);
			return;
		}
	}
	hal_log("GT911 not found\n");
}

static TPoint gt_map(int x, int y)
{
	bool swap = TOUCH_SWAP_XY;
	int  xm = gt_xmax, ym = gt_ymax;
	if (xm > ym)    // reports in landscape: turn it to portrait
		swap = !swap;
	if (swap) {
		int t = x;
		x = y, y = t;
		t = xm, xm = ym, ym = t;
	}
	x = x * NATIVE_W / (xm > 0 ? xm : NATIVE_W);
	y = y * NATIVE_H / (ym > 0 ? ym : NATIVE_H);
	if (TOUCH_MIRROR_X)
		x = NATIVE_W - 1 - x;
	if (TOUCH_MIRROR_Y)
		y = NATIVE_H - 1 - y;
	return TPoint{ clampi(x, 0, NATIVE_W - 1), clampi(y, 0, NATIVE_H - 1) };
}

// Contacts now, in portrait coordinates. Keeps the last report until the
// controller has a new one.
static int touch_read(TPoint* pts)
{
	uint8_t st;
	if (gt_read(0x814E, &st, 1) && (st & 0x80)) {
		int     n = st & 0x0F;
		uint8_t d[16];
		if (n > 2)
			n = 2;
		if (n && !gt_read(0x814F, d, 8 * n))
			n = 0;
		for (int i = 0; i < n; i++)
			gt_pts[i] = gt_map(d[8 * i + 1] | d[8 * i + 2] << 8, d[8 * i + 3] | d[8 * i + 4] << 8);
		gt_n = n;
		gt_write8(0x814E, 0);
	}
	for (int i = 0; i < gt_n; i++)
		pts[i] = gt_pts[i];
	return gt_n;
}

bool hal_multitouch(void)
{
	return true;
}

bool hal_can_calibrate(void)
{
	return false;
}

bool hal_needs_calibration(void)
{
	return false;
}

void hal_calibrate(void) {}

static void touch_sleep_prepare(bool touch_wakes)
{
	(void) touch_wakes;    // the GT911 is polled by I2C after the timer wakes the chip
}

#else    // XPT2046

static int32_t cal[6];    // portrait x = (a*rx + b*ry + c) >> 16, y = (d*rx + e*ry + f) >> 16
static bool    have_cal;

static int best_two_avg(int a, int b, int c)
{
	int da = abs(a - b), db = abs(a - c), dc = abs(b - c);
	if (da <= db && da <= dc)
		return (a + b) / 2;
	if (db <= da && db <= dc)
		return (a + c) / 2;
	return (b + c) / 2;
}

// Raw reading. Returns false when nothing presses hard enough.
static bool xpt_raw(int* rx, int* ry)
{
	flush_rows();
	lcd_spi.beginTransaction(SPISettings(XPT_SPI_HZ, MSBFIRST, SPI_MODE0));
	digitalWrite(XPT_CS, LOW);
	lcd_spi.transfer(0xB1);
	int z1 = lcd_spi.transfer16(0xC1) >> 3;
	int z2 = lcd_spi.transfer16(0x91) >> 3;
	int z  = z1 + 4095 - z2;
	int d[6] = { 0 };
	if (z >= XPT_Z_MIN) {
		lcd_spi.transfer16(0x91);    // the first reading after Z is noisy
		d[0] = lcd_spi.transfer16(0xD1) >> 3;
		d[1] = lcd_spi.transfer16(0x91) >> 3;
		d[2] = lcd_spi.transfer16(0xD1) >> 3;
		d[3] = lcd_spi.transfer16(0x91) >> 3;
	}
	d[4] = lcd_spi.transfer16(0xD0) >> 3;    // last one powers down (pen IRQ on)
	d[5] = lcd_spi.transfer16(0) >> 3;
	digitalWrite(XPT_CS, HIGH);
	lcd_spi.endTransaction();
	if (z < XPT_Z_MIN)
		return false;
	*ry = best_two_avg(d[0], d[2], d[4]);
	*rx = best_two_avg(d[1], d[3], d[5]);
	return true;
}

static void touch_init(void)
{
	pinMode(XPT_CS, OUTPUT);
	digitalWrite(XPT_CS, HIGH);
	pinMode(XPT_IRQ, INPUT);
	have_cal = store_load_calib(cal, 6) == 0;
}

static int touch_read(TPoint* pts)
{
	static int miss;
	static TPoint last;
	static bool   down;
	int rx, ry;
	if (!xpt_raw(&rx, &ry)) {
		// Allow one dropped sample before calling it a release: resistive
		// panels flicker at low pressure.
		if (down && ++miss < 2) {
			pts[0] = last;
			return 1;
		}
		down = false;
		return 0;
	}
	miss = 0;
	int x = (int) (((int64_t) cal[0] * rx + (int64_t) cal[1] * ry + cal[2]) >> 16);
	int y = (int) (((int64_t) cal[3] * rx + (int64_t) cal[4] * ry + cal[5]) >> 16);
	last   = TPoint{ clampi(x, 0, NATIVE_W - 1), clampi(y, 0, NATIVE_H - 1) };
	down   = true;
	pts[0] = last;
	return 1;
}

bool hal_multitouch(void)
{
	return false;
}

bool hal_can_calibrate(void)
{
	return true;
}

bool hal_needs_calibration(void)
{
	// No calibration yet, or the screen is held down at power-on.
	int rx, ry;
	return !have_cal || xpt_raw(&rx, &ry);
}

static void crosshair(int x, int y, uint8_t v)
{
	hal_fill(x - 12, y, 25, 1, v);
	hal_fill(x, y - 12, 1, 25, v);
	hal_fill(x - 2, y - 2, 5, 5, v);
}

// Wait for a firm press and return its averaged raw position.
static void sample_point(int* rx, int* ry)
{
	for (;;) {
		int x, y;
		while (xpt_raw(&x, &y))    // let go first
			delay(20);
		while (!xpt_raw(&x, &y))
			delay(20);
		delay(80);    // settle
		long sx = 0, sy = 0;
		int  n  = 0;
		for (int i = 0; i < 16; i++) {
			if (xpt_raw(&x, &y))
				sx += x, sy += y, n++;
			delay(10);
		}
		if (n >= 12) {
			*rx = (int) (sx / n), *ry = (int) (sy / n);
			while (xpt_raw(&x, &y))
				delay(20);
			return;
		}
	}
}

void hal_calibrate(void)
{
	int saved_rot = rotation;
	hal_set_rotation(0);
	const int sx[3] = { 32, 288, 160 }, sy[3] = { 48, 240, 432 };
	int       rx[3], ry[3];
	for (int i = 0; i < 3; i++) {
		hal_fill(0, 0, NATIVE_W, NATIVE_H, UI_WHITE);
		ui_text_fit(0, 150, NATIVE_W, 2, "Touch calibration", UI_BLACK, UI_WHITE, 1);
		ui_text_fit(0, 190, NATIVE_W, 1, "Press the centre of the cross firmly", UI_BLACK, UI_WHITE, 1);
		ui_text_fit(0, 206, NATIVE_W, 1, "(a stylus or fingernail works best)", UI_GRAY, UI_WHITE, 1);
		crosshair(sx[i], sy[i], UI_BLACK);
		flush_rows();
		sample_point(&rx[i], &ry[i]);
	}
	// Solve x = a*rx + b*ry + c (and the same for y) through the three points.
	double det = (double) (rx[0] - rx[2]) * (ry[1] - ry[2]) - (double) (rx[1] - rx[2]) * (ry[0] - ry[2]);
	bool   ok  = fabs(det) > 1000;
	if (ok) {
		const int* tgt[2] = { sx, sy };
		for (int k = 0; k < 2; k++) {
			const int* t = tgt[k];
			double     a = ((t[0] - t[2]) * (double) (ry[1] - ry[2]) - (t[1] - t[2]) * (double) (ry[0] - ry[2])) / det;
			double     b = ((t[1] - t[2]) * (double) (rx[0] - rx[2]) - (t[0] - t[2]) * (double) (rx[1] - rx[2])) / det;
			double     c = t[2] - a * rx[2] - b * ry[2];
			cal[3 * k]     = (int32_t) lround(a * 65536);
			cal[3 * k + 1] = (int32_t) lround(b * 65536);
			cal[3 * k + 2] = (int32_t) lround(c * 65536);
		}
		have_cal = true;
		if (store_save_calib(cal, 6) != 0)
			hal_log("cannot save calibration\n");
	}
	hal_fill(0, 0, NATIVE_W, NATIVE_H, UI_WHITE);
	ui_text_fit(0, 220, NATIVE_W, 2, ok ? "Calibrated" : "Try again", UI_BLACK, UI_WHITE, 1);
	flush_rows();
	delay(700);
	hal_set_rotation(saved_rot);
	if (!ok)
		hal_calibrate();
}

static void touch_sleep_prepare(bool touch_wakes)
{
	// The pen IRQ line goes low on a touch: a wake-up source after idle sleep.
	if (touch_wakes) {
		gpio_wakeup_enable((gpio_num_t) XPT_IRQ, GPIO_INTR_LOW_LEVEL);
		esp_sleep_enable_gpio_wakeup();
	} else {
		gpio_wakeup_disable((gpio_num_t) XPT_IRQ);
	}
}

#endif

// ---- board interface ------------------------------------------------------------------------

int board_touch(TPoint* out)
{
	TPoint p[2];
	int    n = touch_read(p);
	for (int i = 0; i < n; i++)
		out[i] = rotate_point(p[i].x, p[i].y);
	return n;
}

void board_display_sleep(bool sleep)
{
	flush_rows();
	lcd_begin();
	lcd_cmd(sleep ? 0x10 : 0x11);    // sleep in keeps the display memory
	lcd_end();
	if (!sleep)
		delay(120);
}

void board_sleep_prepare(bool touch_wakes)
{
	touch_sleep_prepare(touch_wakes);
}

bool board_sd_mount(void)
{
	sd_spi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
	if (SD.begin(SD_CS, sd_spi, SD_SPI_HZ, "/sd"))
		return true;
	return SD.begin(SD_CS, sd_spi, 4000000, "/sd");    // slow cards and long wires
}

void board_sd_end(void)
{
	SD.end();
}

uint64_t board_sd_size(void)
{
	return SD.cardSize();
}
