// The drawing half of the HAL for devices with a memory framebuffer (Kindle,
// desktop simulator): an 8-bit gray buffer in the current rotation, turned to
// the physical orientation when presented.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tin.h"

// Physical screen size (portrait as the device mounts it).
int  fb_init(int w, int h);
void fb_free(void);
// The device shows a physical-orientation frame (w*h bytes as given to fb_init).
// Implemented by the platform.
void fb_show(const uint8_t* frame, bool full);
// Touch position in physical coordinates -> current rotation.
TPoint fb_rotate_point(int x, int y);
