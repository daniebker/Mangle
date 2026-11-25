// Pins and options for the ESP32-3248S035 "Cheap Yellow Display" (3.5",
// ST7796 320x480). Override any of these with -D in platformio.ini.
#pragma once

// Display: ST7796 on HSPI.
#ifndef LCD_SCLK
#define LCD_SCLK 14
#endif
#ifndef LCD_MOSI
#define LCD_MOSI 13
#endif
#ifndef LCD_MISO
#define LCD_MISO 12
#endif
#ifndef LCD_CS
#define LCD_CS 15
#endif
#ifndef LCD_DC
#define LCD_DC 2
#endif
#ifndef LCD_BL
#define LCD_BL 27
#endif
#ifndef LCD_SPI_HZ
#define LCD_SPI_HZ 40000000
#endif
// MADCTL for rotations 0..3 (MX/MY/MV bits plus BGR). Change if the picture
// comes out mirrored on another panel.
#ifndef LCD_MADCTL
#define LCD_MADCTL { 0x48, 0x28, 0x88, 0xE8 }
#endif

// SD card on VSPI.
#ifndef SD_SCLK
#define SD_SCLK 18
#endif
#ifndef SD_MISO
#define SD_MISO 19
#endif
#ifndef SD_MOSI
#define SD_MOSI 23
#endif
#ifndef SD_CS
#define SD_CS 5
#endif
#ifndef SD_SPI_HZ
#define SD_SPI_HZ 25000000
#endif

// Touch: define TOUCH_GT911 for the capacitive board (ESP32-3248S035C);
// otherwise the resistive XPT2046 (ESP32-3248S035R) is used.
#ifdef TOUCH_GT911
#ifndef GT911_SDA
#define GT911_SDA 33
#endif
#ifndef GT911_SCL
#define GT911_SCL 32
#endif
#ifndef GT911_INT
#define GT911_INT 21
#endif
#ifndef GT911_RST
#define GT911_RST 25
#endif
#else
// XPT2046 shares the display's SPI bus.
#ifndef XPT_CS
#define XPT_CS 33
#endif
#ifndef XPT_IRQ
#define XPT_IRQ 36
#endif
#ifndef XPT_SPI_HZ
#define XPT_SPI_HZ 2000000
#endif
#ifndef XPT_Z_MIN
#define XPT_Z_MIN 350    // pressure that counts as a touch
#endif
#endif

// Swap/mirror the touch axes (in portrait) if taps land in the wrong place.
// (The XPT2046 is calibrated on screen, so this is only for the GT911.)
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 0
#endif
#ifndef TOUCH_MIRROR_X
#define TOUCH_MIRROR_X 0
#endif
#ifndef TOUCH_MIRROR_Y
#define TOUCH_MIRROR_Y 0
#endif

// Where books and state live on the SD card.
#ifndef LIBRARY_DIR
#define LIBRARY_DIR "/sd/manga"
#endif
#ifndef STATE_DIR
#define STATE_DIR "/sd/manga/.mangle"
#endif

#define NATIVE_W 320
#define NATIVE_H 480
