// Board layer for the Freenove "ESP32-S3 Display" 3.5" (FNK0104N): ST77922
// panel on QSPI, its capacitive touch on I2C, microSD on SDMMC. With 8 MB of
// PSRAM the screen is drawn into a framebuffer (common/src/fb.c, as on the
// Kindle) and sent whole, which also gives software rotation: the panel itself
// can't swap its axes.
#include <Arduino.h>
#include <SD_MMC.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "board.h"
#include "esp32_board.h"
#include "hal.h"
#include "st77922_init.h"    // generated at build time by fetch_init.py
extern "C" {
#include "fb.h"
}

// ---- display: ST77922 over QSPI -----------------------------------------------------------------
//
// Every transfer starts with an 8-bit opcode and a 24-bit address holding the
// register: 0x02 = write a register (data on one line), 0x32 = write pixels
// (on four lines).

static const size_t CHUNK = NATIVE_W * 32;    // pixels per DMA transfer

static spi_device_handle_t lcd;
static uint16_t*           txbuf;       // CHUNK pixels, DMA-capable
static uint16_t            lut[256];    // gray -> RGB565, byte order for the wire
static bool                panel_ok;

static void lcd_reg(uint8_t reg, const uint8_t* data, int len)
{
	spi_transaction_ext_t t;
	memset(&t, 0, sizeof(t));
	t.base.flags     = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
	t.base.cmd       = 0x02;
	t.base.addr      = (uint32_t) reg << 8;
	t.command_bits   = 8;
	t.address_bits   = 24;
	t.base.tx_buffer = len ? data : NULL;
	t.base.length    = 8 * (size_t) len;
	spi_device_polling_transmit(lcd, (spi_transaction_t*) &t);
}

static void lcd_window(int x0, int y0, int x1, int y1)    // inclusive
{
	uint8_t ca[4] = { (uint8_t) (x0 >> 8), (uint8_t) x0, (uint8_t) (x1 >> 8), (uint8_t) x1 };
	uint8_t ra[4] = { (uint8_t) (y0 >> 8), (uint8_t) y0, (uint8_t) (y1 >> 8), (uint8_t) y1 };
	lcd_reg(0x2A, ca, 4);
	lcd_reg(0x2B, ra, 4);
}

static void lcd_pixels(const uint16_t* px, size_t n)
{
	spi_transaction_ext_t t;
	memset(&t, 0, sizeof(t));
	t.base.flags     = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
	t.base.cmd       = 0x32;
	t.base.addr      = 0x3C << 8;    // memory write, continuing in the window
	t.command_bits   = 8;
	t.address_bits   = 24;
	t.base.tx_buffer = px;
	t.base.length    = 16 * n;
	spi_device_polling_transmit(lcd, (spi_transaction_t*) &t);
}

static void build_lut(void)
{
	for (int g = 0; g < 256; g++) {
		uint16_t c = (uint16_t) (((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
		lut[g]     = (uint16_t) ((c >> 8) | (c << 8));
	}
}

// fb.c hands over a whole frame in portrait (already rotated, night mode applied).
void fb_show(const uint8_t* frame, bool full)
{
	(void) full;    // an LCD has no ghosting to clear
	if (!panel_ok)
		return;
	lcd_window(0, 0, NATIVE_W - 1, NATIVE_H - 1);
	size_t total = (size_t) NATIVE_W * NATIVE_H, done = 0;
	while (done < total) {
		size_t n = total - done < CHUNK ? total - done : CHUNK;
		for (size_t i = 0; i < n; i++)
			txbuf[i] = lut[frame[done + i]];
		lcd_pixels(txbuf, n);
		done += n;
	}
}

bool hal_has_invert(void)
{
	return true;
}

void hal_set_invert(bool on)
{
	// This panel is set up with inversion on (0x21); the setting turns it off.
	if (panel_ok)
		lcd_reg(on ? 0x20 : 0x21, NULL, 0);
}

static void lcd_init(void)
{
	pinMode(LCD_CS, OUTPUT);
	digitalWrite(LCD_CS, HIGH);
	spi_bus_config_t bus = {};
	bus.data0_io_num     = LCD_D0;
	bus.data1_io_num     = LCD_D1;
	bus.sclk_io_num      = LCD_SCLK;
	bus.data2_io_num     = LCD_D2;
	bus.data3_io_num     = LCD_D3;
	bus.max_transfer_sz  = CHUNK * 2 + 16;
	bus.flags            = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_IOMUX_PINS | SPICOMMON_BUSFLAG_QUAD;
	spi_device_interface_config_t dev = {};
	dev.mode                          = 0;
	dev.clock_speed_hz                = LCD_QSPI_HZ;
	dev.spics_io_num                  = LCD_CS;
	dev.flags                         = SPI_DEVICE_HALFDUPLEX;
	dev.queue_size                    = 4;
	txbuf = (uint16_t*) heap_caps_malloc(CHUNK * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
	if (!txbuf || spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK ||
	    spi_bus_add_device(SPI2_HOST, &dev, &lcd) != ESP_OK) {
		hal_log("display: QSPI setup failed\n");
		return;
	}
	for (size_t i = 0; i < sizeof(st77922_init) / sizeof(*st77922_init); i++) {
		lcd_reg(st77922_init[i].cmd, st77922_init[i].data, st77922_init[i].len);
		if (st77922_init[i].delay_ms)
			delay(st77922_init[i].delay_ms);
	}
	// Portrait, or turned 180 degrees (rows and columns mirrored) with
	// PANEL_FLIP. 90 and 270 degrees are done by fb.c.
	uint8_t madctl = PANEL_FLIP ? 0xC0 : 0x00;
	lcd_reg(0x36, &madctl, 1);
	panel_ok = true;
}

// ---- touch ----------------------------------------------------------------------------------------
//
// Registers are 16-bit addresses. 0x0010: bit 3 set = a new report. Contacts
// from 0x0014, 7 bytes each: byte 0 bit 7 = down, X = (b0 & 0x3F) << 8 | b1,
// Y = (b2 & 0x3F) << 8 | b3, in portrait pixels. 0x0009: contacts it reports.

static int    tp_max = 2;
static int    tp_n;        // contacts in the last report
static TPoint tp_pts[2];

static bool tp_read(uint16_t reg, uint8_t* buf, int n)
{
	Wire.beginTransmission(TP_ADDR);
	Wire.write((uint8_t) (reg >> 8));
	Wire.write((uint8_t) reg);
	if (Wire.endTransmission(false) != 0)
		return false;
	if (Wire.requestFrom((int) TP_ADDR, n) != n)
		return false;
	for (int i = 0; i < n; i++)
		buf[i] = (uint8_t) Wire.read();
	return true;
}

static void touch_init(void)
{
	pinMode(TP_INT, INPUT);
	pinMode(TP_RST, OUTPUT);
	digitalWrite(TP_RST, LOW);
	delay(20);
	digitalWrite(TP_RST, HIGH);
	delay(100);
	Wire.begin(TP_SDA, TP_SCL, 400000);
	uint8_t v = 0;
	for (int i = 0; i < 50 && tp_read(0x0001, &v, 1) && (v & 0x0F); i++)    // busy after reset
		delay(10);
	if (tp_read(0x0009, &v, 1) && v > 0)
		tp_max = v < 10 ? v : 10;
	hal_log("touch: up to %d contacts\n", tp_max);
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

int board_touch(TPoint* out)
{
	uint8_t info;
	if (tp_read(0x0010, &info, 1) && (info & 0x08)) {
		uint8_t d[7 * 10];
		int     n = 0;
		if (tp_read(0x0014, d, 7 * tp_max)) {
			for (int i = 0; i < tp_max && n < 2; i++) {
				const uint8_t* p = d + 7 * i;
				if (!(p[0] & 0x80))
					continue;
				int x       = (p[0] & 0x3F) << 8 | p[1];
				int y       = (p[2] & 0x3F) << 8 | p[3];
				if (PANEL_FLIP)    // the touch panel doesn't turn with the picture
					x = NATIVE_W - 1 - x, y = NATIVE_H - 1 - y;
				tp_pts[n++] = TPoint{ clampi(x, 0, NATIVE_W - 1), clampi(y, 0, NATIVE_H - 1) };
			}
		}
		tp_n = n;
	}
	for (int i = 0; i < tp_n; i++)
		out[i] = fb_rotate_point(tp_pts[i].x, tp_pts[i].y);
	return tp_n;
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

// ---- board interface -------------------------------------------------------------------------------

void board_init(void)
{
	build_lut();
	if (fb_init(NATIVE_W, NATIVE_H) != 0)
		hal_log("no memory for the framebuffer\n");
	lcd_init();
	hal_fill(0, 0, NATIVE_W, NATIVE_H, 255);
	hal_present(true);
	touch_init();
}

void board_display_sleep(bool sleep)
{
	if (!panel_ok)
		return;
	lcd_reg(sleep ? 0x10 : 0x11, NULL, 0);    // sleep in keeps the display memory
	if (!sleep)
		delay(120);
}

void board_sleep_prepare(bool touch_wakes)
{
	(void) touch_wakes;    // touch is polled by I2C after the timer wakes the chip
}

bool board_sd_mount(void)
{
	SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
	return SD_MMC.begin("/sd", false);
}

void board_sd_end(void)
{
	SD_MMC.end();
}

uint64_t board_sd_size(void)
{
	return SD_MMC.cardSize();
}
