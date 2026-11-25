// The parts of the HAL that every ESP32 board shares: backlight (PWM), the
// input loop (polled touch -> gestures), light sleep, the clock (set over
// Wi-Fi at power-on), logging and start-up. The board itself (display, touch
// controller, SD card) is in cyd/src/boards/<name>/.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "board.h"
#include "esp32_board.h"
#include "hal.h"
extern "C" {
#include "ui.h"
}

static Tin tin;
static int bl_pct = 80;

// ---- backlight ---------------------------------------------------------------------------

static void backlight_raw(int pct)
{
	int duty = pct * 255 / 100;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
	ledcWrite(LCD_BL, duty);
#else
	ledcWrite(0, duty);
#endif
}

static void backlight_init(void)
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
	ledcAttach(LCD_BL, 5000, 8);
#else
	ledcSetup(0, 5000, 8);
	ledcAttachPin(LCD_BL, 0);
#endif
	backlight_raw(0);
}

bool hal_has_backlight(void)
{
	return true;
}

void hal_backlight(int pct)
{
	bl_pct = pct < 5 ? 5 : pct > 100 ? 100 : pct;
	backlight_raw(bl_pct);
}

// ---- input ---------------------------------------------------------------------------------

int hal_wait(InEvent* ev, int timeout_ms)
{
	int64_t end = timeout_ms >= 0 ? hal_ms() + timeout_ms : 0;
	for (;;) {
		TPoint p[2];
		int    n = board_touch(p);
		if (tin_feed(&tin, n, p, hal_ms(), ev))
			return 1;
		if (timeout_ms >= 0 && hal_ms() >= end)
			return 0;
		delay(10);
	}
}

// ---- sleep -----------------------------------------------------------------------------------

bool hal_sleep(void)
{
	backlight_raw(0);
	board_display_sleep(true);
	for (;;) {
		TPoint p[2];
		board_sleep_prepare();
		esp_sleep_enable_timer_wakeup(150 * 1000);
		esp_light_sleep_start();
		if (board_touch(p) > 0)
			break;
	}
	// Swallow the waking touch.
	TPoint p[2];
	while (board_touch(p) > 0)
		delay(20);
	tin_init(&tin, TAP_PX);
	board_display_sleep(false);
	backlight_raw(bl_pct);
	return true;
}

// ---- system -----------------------------------------------------------------------------------

bool hal_can_exit(void)
{
	return false;
}

bool hal_clock_ok(void)
{
	return time(NULL) > 1577836800;    // after 2020: set over Wi-Fi at power-on
}

int64_t hal_ms(void)
{
	return (int64_t) millis();
}

void hal_log(const char* fmt, ...)
{
	char    buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	Serial.print("mangle: ");
	Serial.print(buf);
}

static void show_message(const char* const* lines, int n)
{
	ui_message(lines, n);
	hal_present(true);
}

// The boards have no clock that survives power-off. If the state folder has
// wifi.txt (network name, password, and optionally a POSIX time zone such as
// CET-1CEST,M3.5.0,M10.5.0/3, one per line), connect once at power-on, set
// the time, and turn Wi-Fi off again. Without it, reading statistics are off.
static void clock_sync(void)
{
	FILE* f = fopen(STATE_DIR "/wifi.txt", "r");
	if (!f)
		return;
	char ssid[64] = "", pass[96] = "", tz[64] = "UTC0";
	if (fgets(ssid, sizeof(ssid), f) && fgets(pass, sizeof(pass), f) && !fgets(tz, sizeof(tz), f))
		snprintf(tz, sizeof(tz), "UTC0");
	fclose(f);
	ssid[strcspn(ssid, "\r\n")] = 0;
	pass[strcspn(pass, "\r\n")] = 0;
	tz[strcspn(tz, "\r\n")]     = 0;
	if (!ssid[0])
		return;
	const char* lines[] = { "Setting the clock", "", ssid };
	show_message(lines, 3);
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, pass);
	for (int i = 0; i < 100 && WiFi.status() != WL_CONNECTED; i++)
		delay(100);
	if (WiFi.status() == WL_CONNECTED) {
		configTzTime(tz[0] ? tz : "UTC0", "pool.ntp.org", "time.google.com");
		struct tm tm;
		if (getLocalTime(&tm, 8000))
			hal_log("clock set: %04d-%02d-%02d %02d:%02d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				tm.tm_hour, tm.tm_min);
		else
			hal_log("no time from the network\n");
	} else {
		hal_log("cannot join Wi-Fi network %s\n", ssid);
	}
	WiFi.disconnect(true);
	WiFi.mode(WIFI_OFF);    // frees the radio's memory for the reader
}

void esp32_init(void)
{
	backlight_init();
	board_init();
	tin_init(&tin, TAP_PX);
	hal_backlight(bl_pct);
	hal_log("free heap %u, largest block %u, PSRAM %u\n", (unsigned) ESP.getFreeHeap(),
		(unsigned) ESP.getMaxAllocHeap(), (unsigned) ESP.getPsramSize());
	while (!board_sd_mount()) {
		const char* lines[] = { "No SD card", "", "Insert a FAT32 card with your books in /manga" };
		show_message(lines, 3);
		board_sd_end();
		delay(2000);
	}
	mkdir(LIBRARY_DIR, 0755);
	mkdir(STATE_DIR, 0755);
	hal_log("SD card: %llu MB\n", (unsigned long long) (board_sd_size() / (1024 * 1024)));
	clock_sync();
}
