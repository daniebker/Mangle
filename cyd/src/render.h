// Draws part of a PNG scaled onto the screen while it is being decoded:
// area-averaged when shrinking, bilinear when enlarging. Only a few rows are
// ever held in memory.
#pragma once

#include <stdint.h>

#include "pngs.h"
#include "zipf.h"
#include "zoom.h"

// Where output goes: rows of 8-bit gray, and solid fills.
typedef struct {
	void (*row)(void* ctx, int x, int y, int w, const uint8_t* gray);
	void (*fill)(void* ctx, int x, int y, int w, int h, uint8_t v);
	void* ctx;
} Sink;

typedef struct {
	int sx, sy, sw, sh;    // source rectangle, image pixels (inside the image)
	int dx, dy, dw, dh;    // where it lands (may reach past the area)
	int ax, ay, aw, ah;    // screen area this draw owns: the rest of it is filled with bg
} Placement;

// Called once the image size is known. Return nonzero to abort.
typedef int (*LayoutFn)(void* u, int img_w, int img_h, Placement* out);

// Decode and draw. Returns 0 on success; on failure *err says why.
int render_png(PngRead rd, void* rctx, LayoutFn lay, void* lu, const Sink* sink, uint8_t bg, const char** err);

// Same, for an entry of a ZIP.
int render_entry(Zip* z, const char* name, LayoutFn lay, void* lu, const Sink* sink, uint8_t bg, const char** err);

// Layout for the reader: base rectangle `rect` (image pixels; w <= 0 means the
// whole image) shown on an sw x sh screen with the shared zoom logic. Fills
// *base with the ZoomBase it used (for later pinches and pans) and clamps *zm.
typedef struct {
	int       rect[4];
	bool      upscale;    // enlarge a small base to fill the screen (panels)
	int       sw, sh;
	Zoom*     zm;
	ZoomBase* base;
} ViewLayout;
int layout_view(void* u, int img_w, int img_h, Placement* out);

// Layout that fits the whole image into a box, centred (covers, thumbnails).
typedef struct {
	int  x, y, w, h;
	bool upscale;
	int  img_w, img_h;    // filled in: the image's size
} FitLayout;
int layout_fit(void* u, int img_w, int img_h, Placement* out);
