// Pins and options for the Freenove "ESP32-S3 Display" 3.5" (FNK0104N):
// ESP32-S3R8 (8 MB PSRAM), 16 MB flash, ST77922 320x480 IPS panel on QSPI with
// its capacitive touch on I2C, microSD on SDMMC (4-bit), native USB.
// From Freenove's schematic and examples (github.com/Freenove/Freenove_ESP32_S3_Display).
#pragma once

// Display: ST77922 on QSPI (SPI2, IO-MUX pins).
#define LCD_SCLK 12
#define LCD_D0 11
#define LCD_D1 13
#define LCD_D2 14
#define LCD_D3 9
#define LCD_CS 10
#define LCD_BL 41
#ifndef LCD_QSPI_HZ
#define LCD_QSPI_HZ 80000000
#endif

// Touch: the panel's controller on I2C, address 0x55.
#define TP_SDA 38
#define TP_SCL 39
#define TP_RST 48
#define TP_INT 47
#define TP_ADDR 0x55

// microSD: SDMMC, 4-bit.
#define SD_CLK 5
#define SD_CMD 4
#define SD_D0 6
#define SD_D1 7
#define SD_D2 2
#define SD_D3 3

#define NATIVE_W 320
#define NATIVE_H 480

#define TAP_PX 14
// 8 MB of PSRAM: decode the next and previous pages ahead, as on the Kindle.
#define BOARD_PREFETCH 1

#ifndef LIBRARY_DIR
#define LIBRARY_DIR "/sd/manga"
#endif
#ifndef STATE_DIR
#define STATE_DIR "/sd/manga/.mangle"
#endif
