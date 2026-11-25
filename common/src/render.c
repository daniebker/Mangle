#include "render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const Placement* pl;
	const Sink*      sink;
	// visible output columns [jx0, jx1) and rows [jy0, jy1), relative to dx/dy
	int       jx0, jx1, jy0, jy1;
	bool      hdown, vdown;
	uint16_t* hidx;    // per visible column: first source column (relative to sx)
	uint16_t* hn;      // downscale: source columns in the span; upscale: weight of the next one (0..256)
	uint32_t* hinv;    // downscale: 65536 / span
	uint8_t*  cur;     // current source row, resampled horizontally
	uint8_t*  prev;    // previous one (upscale)
	uint32_t* acc;     // downscale: sum of resampled rows in the span
	uint8_t*  out;
	int       nacc;    // rows in acc
	int       j;       // next output row
	int       vis;     // visible columns
} R;

// Output row j's vertical source: downscale span [a, b), or bilinear (i0, i1, w).
static void vspan(const R* r, int j, int* a, int* b)
{
	int sh = r->pl->sh, dh = r->pl->dh;
	*a     = (int) ((int64_t) j * sh / dh);
	*b     = (int) ((int64_t) (j + 1) * sh / dh);
	if (*b <= *a)
		*b = *a + 1;
}

static void bilin(int j, int n_src, int n_dst, int* i0, int* i1, int* w)
{
	int64_t v = ((int64_t) (2 * j + 1) * n_src * 256) / (2 * n_dst) - 128;    // in 1/256 source pixels
	if (v < 0)
		v = 0;
	*i0 = (int) (v >> 8);
	*w  = (int) (v & 255);
	if (*i0 >= n_src - 1)
		*i0 = n_src - 1, *w = 0;
	*i1 = *i0 + (*i0 < n_src - 1);
}

static void hresample(R* r, const uint8_t* src)
{
	src += r->pl->sx;
	if (r->hdown) {
		for (int k = 0; k < r->vis; k++) {
			const uint8_t* s   = src + r->hidx[k];
			uint32_t       sum = 0;
			for (int i = 0; i < r->hn[k]; i++)
				sum += s[i];
			r->cur[k] = (uint8_t) ((sum * r->hinv[k] + 32768) >> 16);
		}
	} else {
		for (int k = 0; k < r->vis; k++) {
			const uint8_t* s = src + r->hidx[k];
			int            w = r->hn[k];
			r->cur[k]        = (uint8_t) ((s[0] * (256 - w) + s[w ? 1 : 0] * w + 128) >> 8);
		}
	}
}

static void emit(R* r, int j, const uint8_t* px)
{
	if (j < r->jy0 || j >= r->jy1)
		return;
	const Placement* pl = r->pl;
	r->sink->row(r->sink->ctx, pl->dx + r->jx0, pl->dy + j, r->vis, px);
}

static int on_row(void* ctx, int y, const uint8_t* gray, int w)
{
	R*               r  = ctx;
	const Placement* pl = r->pl;
	int              ry = y - pl->sy;
	if (ry < 0)
		return 0;
	if (ry >= pl->sh || r->j >= r->jy1)
		return 1;    // everything visible is drawn: stop decoding
	uint8_t* t = r->prev;
	r->prev = r->cur, r->cur = t;
	hresample(r, gray);

	if (r->vdown) {
		for (int k = 0; k < r->vis; k++)
			r->acc[k] += r->cur[k];
		r->nacc++;
		int a, b;
		vspan(r, r->j, &a, &b);
		if (b == ry + 1) {
			uint32_t inv = 65536u / (uint32_t) r->nacc;
			for (int k = 0; k < r->vis; k++)
				r->out[k] = (uint8_t) ((r->acc[k] * inv + 32768) >> 16);
			emit(r, r->j, r->out);
			r->j++;
			memset(r->acc, 0, (size_t) r->vis * sizeof(uint32_t));
			r->nacc = 0;
		}
	} else {
		for (;;) {
			if (r->j >= pl->dh)
				break;
			int i0, i1, wt;
			bilin(r->j, pl->sh, pl->dh, &i0, &i1, &wt);
			if (i1 > ry)
				break;
			const uint8_t* a = i0 == ry ? r->cur : r->prev;
			if (wt == 0 || i0 == i1)
				emit(r, r->j, a);
			else {
				for (int k = 0; k < r->vis; k++)
					r->out[k] = (uint8_t) ((a[k] * (256 - wt) + r->cur[k] * wt + 128) >> 8);
				emit(r, r->j, r->out);
			}
			r->j++;
		}
	}
	return r->j >= r->jy1;
}

static void fill(const Sink* s, int x0, int y0, int x1, int y1, uint8_t v)
{
	if (x1 > x0 && y1 > y0)
		s->fill(s->ctx, x0, y0, x1 - x0, y1 - y0, v);
}

static int imax(int a, int b)
{
	return a > b ? a : b;
}

static int imin(int a, int b)
{
	return a < b ? a : b;
}

// Feeds the image's rows, in order, to a PngRow callback (stops when it returns nonzero).
typedef int (*RowFeed)(void* src, PngRow row, void* ctx);

static int draw(int w, int h, RowFeed feed, void* src, LayoutFn lay, void* lu, const Sink* sink, uint8_t bg,
		const char** err)
{
	Placement pl;
	memset(&pl, 0, sizeof(pl));
	if (lay(lu, w, h, &pl) != 0) {
		if (err)
			*err = "nothing to show";
		return -1;
	}
	// Keep the source inside the image.
	if (pl.sx < 0)
		pl.sw += pl.sx, pl.sx = 0;
	if (pl.sy < 0)
		pl.sh += pl.sy, pl.sy = 0;
	pl.sw = imin(pl.sw, w - pl.sx);
	pl.sh = imin(pl.sh, h - pl.sy);

	R r;
	memset(&r, 0, sizeof(r));
	r.pl = &pl, r.sink = sink;
	int vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0;    // visible destination, screen coordinates
	if (pl.sw > 0 && pl.sh > 0 && pl.dw > 0 && pl.dh > 0) {
		vx0 = imax(pl.dx, pl.ax), vx1 = imin(pl.dx + pl.dw, pl.ax + pl.aw);
		vy0 = imax(pl.dy, pl.ay), vy1 = imin(pl.dy + pl.dh, pl.ay + pl.ah);
	}
	if (vx1 <= vx0 || vy1 <= vy0) {
		fill(sink, pl.ax, pl.ay, pl.ax + pl.aw, pl.ay + pl.ah, bg);
		return 0;
	}
	// Background around the picture.
	fill(sink, pl.ax, pl.ay, pl.ax + pl.aw, vy0, bg);
	fill(sink, pl.ax, vy1, pl.ax + pl.aw, pl.ay + pl.ah, bg);
	fill(sink, pl.ax, vy0, vx0, vy1, bg);
	fill(sink, vx1, vy0, pl.ax + pl.aw, vy1, bg);

	r.jx0 = vx0 - pl.dx, r.jx1 = vx1 - pl.dx;
	r.jy0 = vy0 - pl.dy, r.jy1 = vy1 - pl.dy;
	r.vis   = r.jx1 - r.jx0;
	r.hdown = pl.dw < pl.sw;
	r.vdown = pl.dh < pl.sh;
	r.hidx  = malloc((size_t) r.vis * sizeof(uint16_t));
	r.hn    = malloc((size_t) r.vis * sizeof(uint16_t));
	r.hinv  = malloc((size_t) r.vis * sizeof(uint32_t));
	r.cur   = malloc((size_t) r.vis);
	r.prev  = malloc((size_t) r.vis);
	r.out   = malloc((size_t) r.vis);
	r.acc   = calloc((size_t) r.vis, sizeof(uint32_t));
	int rc  = -1;
	if (!r.hidx || !r.hn || !r.hinv || !r.cur || !r.prev || !r.out || !r.acc) {
		if (err)
			*err = "out of memory";
		goto out;
	}
	for (int k = 0; k < r.vis; k++) {
		int j = r.jx0 + k;
		if (r.hdown) {
			int a = (int) ((int64_t) j * pl.sw / pl.dw), b = (int) ((int64_t) (j + 1) * pl.sw / pl.dw);
			if (b <= a)
				b = a + 1;
			r.hidx[k] = (uint16_t) a;
			r.hn[k]   = (uint16_t) (b - a);
			r.hinv[k] = 65536u / (uint32_t) (b - a);
		} else {
			int i0, i1, wt;
			bilin(j, pl.sw, pl.dw, &i0, &i1, &wt);
			r.hidx[k] = (uint16_t) i0;
			r.hn[k]   = (uint16_t) (i1 > i0 ? wt : 0);
		}
	}
	if (feed(src, on_row, &r) != 0) {
		if (err)
			*err = "corrupt image data";
		goto out;
	}
	if (r.j < r.jy1) {
		if (err)
			*err = "image ended early";
		goto out;
	}
	rc = 0;
out:
	free(r.hidx);
	free(r.hn);
	free(r.hinv);
	free(r.cur);
	free(r.prev);
	free(r.out);
	free(r.acc);
	return rc;
}

static int png_feed(void* src, PngRow row, void* ctx)
{
	return png_decode(src, row, ctx);
}

int render_png(PngRead rd, void* rctx, LayoutFn lay, void* lu, const Sink* sink, uint8_t bg, const char** err)
{
	const char* e = NULL;
	Png*        p = png_open(rd, rctx, &e);
	if (!p) {
		if (err)
			*err = e;
		return -1;
	}
	int w, h;
	png_size(p, &w, &h);
	int rc = draw(w, h, png_feed, p, lay, lu, sink, bg, err);
	png_close(p);
	return rc;
}

static int image_feed(void* src, PngRow row, void* ctx)
{
	const Image* img = src;
	for (int y = 0; y < img->h; y++)
		if (row(ctx, y, img->px + (size_t) y * img->w, img->w))
			break;
	return 0;
}

int render_image(const Image* img, LayoutFn lay, void* lu, const Sink* sink, uint8_t bg, const char** err)
{
	if (!img || !img->px) {
		if (err)
			*err = "no image";
		return -1;
	}
	return draw(img->w, img->h, image_feed, (void*) img, lay, lu, sink, bg, err);
}

int render_entry(Zip* z, const char* name, LayoutFn lay, void* lu, const Sink* sink, uint8_t bg, const char** err)
{
	ZipStream s;
	int       idx = zip_find(z, name);
	if (idx < 0 || zs_open(&s, z, idx) != 0) {
		if (err)
			*err = idx < 0 ? "missing from the book" : "cannot read it";
		return -1;
	}
	int rc = render_png(zs_source, &s, lay, lu, sink, bg, err);
	zs_close(&s);
	return rc;
}

// ---- layouts -------------------------------------------------------------------

// Source and destination for an image region [x0, x1) shown at scale s with
// image coordinate c at screen coordinate half (one axis).
static void axis(double x0, double x1, double c, double s, double half, int* src, int* n, int* dst, int* dn)
{
	int a = (int) floor(x0 + 1e-6), b = (int) ceil(x1 - 1e-6);
	if (b <= a)
		b = a + 1;
	*src   = a;
	*n     = b - a;
	int d0 = (int) lround((a - c) * s + half);
	int d1 = (int) lround((b - c) * s + half);
	if (d1 <= d0)
		d1 = d0 + 1;
	*dst = d0;
	*dn  = d1 - d0;
}

int layout_view(void* u, int img_w, int img_h, Placement* out)
{
	ViewLayout* v    = u;
	int         r[4] = { v->rect[0], v->rect[1], v->rect[2], v->rect[3] };
	if (r[2] <= 0 || r[3] <= 0)
		r[0] = r[1] = 0, r[2] = img_w, r[3] = img_h;
	// Keep the base inside the image.
	if (r[0] < 0)
		r[2] += r[0], r[0] = 0;
	if (r[1] < 0)
		r[3] += r[1], r[1] = 0;
	r[2] = imin(r[2], img_w - r[0]);
	r[3] = imin(r[3], img_h - r[1]);
	if (r[2] <= 0 || r[3] <= 0)
		return -1;
	*v->base = zoom_base(r, v->upscale, v->sw, v->sh);
	zoom_clamp(v->zm, v->base);
	double s = zoom_scale(v->zm, v->base), cx, cy;
	if (zoom_active(v->zm))
		cx = v->zm->cx, cy = v->zm->cy;
	else
		cx = r[0] + r[2] / 2.0, cy = r[1] + r[3] / 2.0;
	double hw = v->sw / (2 * s), hh = v->sh / (2 * s);
	double x0 = fmax(r[0], cx - hw), x1 = fmin(r[0] + r[2], cx + hw);
	double y0 = fmax(r[1], cy - hh), y1 = fmin(r[1] + r[3], cy + hh);
	axis(x0, x1, cx, s, v->sw / 2.0, &out->sx, &out->sw, &out->dx, &out->dw);
	axis(y0, y1, cy, s, v->sh / 2.0, &out->sy, &out->sh, &out->dy, &out->dh);
	out->ax = out->ay = 0;
	out->aw = v->sw, out->ah = v->sh;
	return 0;
}

int layout_fit(void* u, int img_w, int img_h, Placement* out)
{
	FitLayout* f = u;
	f->img_w = img_w, f->img_h = img_h;
	int dw, dh;
	if ((int64_t) img_w * f->h > (int64_t) img_h * f->w)
		dw = f->w, dh = (int) ((int64_t) img_h * f->w / img_w);
	else
		dh = f->h, dw = (int) ((int64_t) img_w * f->h / img_h);
	if (!f->upscale && dw > img_w)
		dw = img_w, dh = img_h;
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;
	out->sx = out->sy = 0;
	out->sw = img_w, out->sh = img_h;
	out->dx = f->x + (f->w - dw) / 2, out->dy = f->y + (f->h - dh) / 2;
	out->dw = dw, out->dh = dh;
	out->ax = f->x, out->ay = f->y, out->aw = f->w, out->ah = f->h;
	return 0;
}
