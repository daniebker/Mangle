// FBInk backend: draws to the Kindle's e-ink framebuffer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "fbink.h"

static int         fbfd = -1;
static FBInkConfig cfg;
static Screen      scr;

static WFM_MODE_INDEX_T partial_wfm(void)
{
	// GC16 without flashing is the safe default for gray manga art.
	// MANGLE_WFM=gl16|reagl|auto|du can trade fidelity for speed per device.
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

int display_open(Screen* s)
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
	scr.width      = (int) st.view_width;
	scr.height     = (int) st.view_height;
	scr.row_height = st.font_h ? (int) st.font_h : 16;
	scr.rows       = st.max_rows ? (int) st.max_rows : scr.height / scr.row_height;
	scr.cols       = st.max_cols ? (int) st.max_cols : scr.width / 8;
	fprintf(stderr, "mangle: %s, %dx%d, rota %d\n", st.device_name, scr.width, scr.height, st.current_rota);
	*s = scr;
	return 0;
}

void display_close(void)
{
	if (fbfd >= 0)
		fbink_close(fbfd);
	fbfd = -1;
}

int display_frame(const uint8_t* buf, bool flash)
{
	FBInkConfig c = cfg;
	c.is_flashing = flash;
	c.wfm_mode    = flash ? WFM_GC16 : partial_wfm();
	// Older FBInk releases take a non-const pointer; the data isn't modified.
	return fbink_print_raw_data(fbfd, (unsigned char*) buf, scr.width, scr.height, (size_t) scr.width * scr.height, 0, 0, &c);
}

int display_text(const char* const* lines, int n, bool flash)
{
	FBInkConfig c = cfg;
	c.no_refresh  = true;    // draw everything, then refresh once
	fbink_cls(fbfd, &c, NULL, false);
	for (int i = 0; i < n && i < scr.rows; i++) {
		if (!lines[i] || !*lines[i])
			continue;
		c.row = (short) i;
		fbink_print(fbfd, lines[i], &c);
	}
	c.is_flashing = flash;
	c.wfm_mode    = WFM_GC16;
	return fbink_refresh(fbfd, 0, 0, 0, 0, &c);
}
