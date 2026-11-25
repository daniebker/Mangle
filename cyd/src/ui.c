#include "ui.h"

#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "hal.h"

static int glyph(const char** s)
{
	unsigned char c = (unsigned char) **s;
	(*s)++;
	if (c < 0x80)
		return c >= 32 && c < 127 ? c : '?';
	while ((**s & 0xC0) == 0x80)    // skip the rest of a UTF-8 sequence
		(*s)++;
	return '?';
}

static int glyphs(const char* s)
{
	int n = 0;
	while (*s) {
		glyph(&s);
		n++;
	}
	return n;
}

int ui_text_width(const char* s, int scale)
{
	return glyphs(s) * FONT_W * scale;
}

int ui_text(int x, int y, int scale, const char* s, uint8_t fg, uint8_t bg)
{
	int sw = hal_width(), n = glyphs(s);
	int w  = n * FONT_W * scale;
	if (x < 0 || n == 0)
		return w;
	int vis = w;
	if (x + vis > sw)
		vis = sw - x;
	if (vis <= 0)
		return w;
	uint8_t* row = malloc((size_t) w);
	char*    g   = malloc((size_t) n);
	if (!row || !g) {
		free(row);
		free(g);
		return w;
	}
	const char* p = s;
	for (int i = 0; i < n; i++)
		g[i] = (char) glyph(&p);
	for (int fy = 0; fy < FONT_H; fy++) {
		for (int i = 0; i < n; i++) {
			unsigned bits = font_data[g[i] - 32][fy];
			uint8_t* o    = row + i * FONT_W * scale;
			for (int fx = 0; fx < FONT_W; fx++) {
				uint8_t v = (bits & (0x80u >> fx)) ? fg : bg;
				memset(o + fx * scale, v, (size_t) scale);
			}
		}
		for (int k = 0; k < scale; k++)
			hal_row(x, y + fy * scale + k, vis, row);
	}
	free(row);
	free(g);
	return w;
}

void ui_text_fit(int x, int y, int w, int scale, const char* s, uint8_t fg, uint8_t bg, int centre)
{
	int cw  = FONT_W * scale;
	int max = w / cw;
	if (max <= 0)
		return;
	char buf[128];
	int  n = glyphs(s);
	if (n > max) {
		// Keep the first max-3 glyphs (whole UTF-8 sequences), then "...".
		const char* p    = s;
		int         keep = max > 3 ? max - 3 : 0;
		for (int i = 0; i < keep; i++)
			glyph(&p);
		size_t len = (size_t) (p - s);
		if (len > sizeof(buf) - 4)
			len = sizeof(buf) - 4;
		memcpy(buf, s, len);
		strcpy(buf + len, max > 3 ? "..." : "");
		s = buf;
		n = glyphs(s);
	}
	int tx = centre ? x + (w - n * cw) / 2 : x;
	ui_text(tx, y, scale, s, fg, bg);
}

// Lines of at most `max` glyphs: wrap at spaces (or anywhere in a long word).
static int wrap(const char* s, int max, char out[][64], int nout)
{
	int n = 0;
	while (*s && n < nout) {
		int len = (int) strlen(s);
		int cut = len;
		if (len > max) {
			cut = max;
			for (int i = max; i > 0; i--)
				if (s[i] == ' ') {
					cut = i;
					break;
				}
		}
		if (cut > 63)
			cut = 63;
		memcpy(out[n], s, (size_t) cut);
		out[n++][cut] = 0;
		s += cut;
		while (*s == ' ')
			s++;
	}
	return n;
}

void ui_message(const char* const* lines, int n)
{
	int  sw = hal_width(), sh = hal_height();
	int  scale = 2, lh = FONT_H * scale + 6;
	int  max   = (sw - 16) / (FONT_W * scale);
	char wrapped[24][64];
	int  total = 0;
	for (int i = 0; i < n && total < 24; i++) {
		if (!lines[i] || !*lines[i]) {
			wrapped[total++][0] = 0;
			continue;
		}
		total += wrap(lines[i], max, wrapped + total, 24 - total);
	}
	hal_fill(0, 0, sw, sh, UI_WHITE);
	int y = (sh - total * lh) / 2;
	for (int i = 0; i < total; i++, y += lh)
		ui_text_fit(0, y, sw, scale, wrapped[i], UI_BLACK, UI_WHITE, 1);
	hal_flush();
}
