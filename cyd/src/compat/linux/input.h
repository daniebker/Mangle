// Key codes from <linux/input.h> that the shared gesture code (gesture.c)
// uses. The ESP32 toolchain has no Linux headers; the board has no keys, so
// only the values matter.
#pragma once

#define KEY_ESC 1
#define KEY_BACKSPACE 14
#define KEY_Q 16
#define KEY_R 19
#define KEY_ENTER 28
#define KEY_SPACE 57
#define KEY_KPENTER 96
#define KEY_HOME 102
#define KEY_PAGEUP 104
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_PAGEDOWN 109
#define KEY_MENU 139
#define KEY_BACK 158
#define KEY_NEXTSONG 163
#define KEY_PREVIOUSSONG 165
#define KEY_HOMEPAGE 172
#define KEY_SELECT 0x161
#define KEY_NEXT 0x197
#define KEY_PREVIOUS 0x19c
