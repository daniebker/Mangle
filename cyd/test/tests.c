// Unit tests for the CYD firmware's portable code (run on the desktop).
//
//   build/tests [BOOK_OR_PNG_ZIP...]
//
// Every PNG in the given ZIPs is decoded with the streaming decoder and
// compared with stb_image; each ZIP that has a manifest is also opened as a book.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cbook.h"
#include "nav.h"
#include "pngs.h"
#include "render.h"
#include "tin.h"
#include "zipf.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

static int fails, checks;

#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		checks++;                                                                                              \
		if (!(c)) {                                                                                            \
			fails++;                                                                                       \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                         \
		}                                                                                                      \
	} while (0)

// ---- memory sources and sinks ----------------------------------------------------------

typedef struct {
	const uint8_t* p;
	size_t         len, pos;
	int            chunk;    // max bytes per read (exercises refills)
} Mem;

static int mem_read(void* ctx, uint8_t* buf, int n)
{
	Mem* m = ctx;
	if (m->chunk && n > m->chunk)
		n = m->chunk;
	size_t k = m->len - m->pos < (size_t) n ? m->len - m->pos : (size_t) n;
	memcpy(buf, m->p + m->pos, k);
	m->pos += k;
	return (int) k;
}

typedef struct {
	uint8_t* px;
	int      w, h;
	int      rows;
} Canvas;

static void cv_row(void* ctx, int x, int y, int w, const uint8_t* g)
{
	Canvas* c = ctx;
	CHECK(x >= 0 && y >= 0 && x + w <= c->w && y < c->h);
	if (x >= 0 && y >= 0 && x + w <= c->w && y < c->h)
		memcpy(c->px + (size_t) y * c->w + x, g, (size_t) w);
	c->rows++;
}

static void cv_fill(void* ctx, int x, int y, int w, int h, uint8_t v)
{
	Canvas* c = ctx;
	CHECK(x >= 0 && y >= 0 && x + w <= c->w && y + h <= c->h);
	for (int r = y; r < y + h && r < c->h; r++)
		if (r >= 0)
			memset(c->px + (size_t) r * c->w + x, v, (size_t) w);
}

static int collect(void* ctx, int y, const uint8_t* gray, int w)
{
	Canvas* c = ctx;
	memcpy(c->px + (size_t) y * w, gray, (size_t) w);
	c->rows++;
	return 0;
}

static uint8_t* read_entry(Zip* z, int idx, size_t* len)
{
	ZipStream s;
	if (zs_open(&s, z, idx) != 0)
		return NULL;
	size_t   cap = z->e[idx].usize + 1, n = 0;
	uint8_t* buf = malloc(cap);
	int      r;
	while (buf && (r = zs_read(&s, buf + n, (int) (cap - n))) > 0)
		n += (size_t) r;
	zs_close(&s);
	if (buf && n != z->e[idx].usize) {
		free(buf);
		return NULL;
	}
	*len = n;
	return buf;
}

// ---- PNG ---------------------------------------------------------------------------------

// Decode with stb_image (gray + alpha, alpha composited over white like pngs.c).
static uint8_t* stb_gray(const uint8_t* data, size_t len, int* w, int* h)
{
	int      ch;
	uint8_t* ga = stbi_load_from_memory(data, (int) len, w, h, &ch, 2);
	if (!ga)
		return NULL;
	uint8_t* g = malloc((size_t) *w * *h);
	for (int i = 0; i < *w * *h; i++) {
		int v = ga[2 * i], a = ga[2 * i + 1];
		g[i]  = (uint8_t) ((v * a + 255 * (255 - a) + 127) / 255);
	}
	stbi_image_free(ga);
	return g;
}

static void check_png(const char* name, const uint8_t* data, size_t len)
{
	int      sw, sh;
	uint8_t* want = stb_gray(data, len, &sw, &sh);
	CHECK(want != NULL);
	if (!want)
		return;
	int chunks[] = { 0, 7 };    // whole reads, and tiny ones
	for (int c = 0; c < 2; c++) {
		Mem         m   = { data, len, 0, chunks[c] };
		const char* err = NULL;
		Png*        p   = png_open(mem_read, &m, &err);
		CHECK(p != NULL);
		if (!p) {
			fprintf(stderr, "  %s: %s\n", name, err);
			break;
		}
		int w, h;
		png_size(p, &w, &h);
		CHECK(w == sw && h == sh);
		Canvas cv = { calloc((size_t) w * h, 1), w, h, 0 };
		CHECK(png_decode(p, collect, &cv) == 0);
		CHECK(cv.rows == h);
		size_t diff = 0;
		for (size_t i = 0; i < (size_t) w * h; i++)
			diff += cv.px[i] != want[i];
		CHECK(diff == 0);
		if (diff)
			fprintf(stderr, "  %s: %zu pixels differ from stb_image\n", name, diff);
		free(cv.px);
		png_close(p);
	}
	free(want);
}

static void test_zip(const char* path)
{
	Zip z;
	CHECK(zip_open(&z, path) == 0);
	if (!z.e)
		return;
	int pngs = 0;
	for (int i = 0; i < z.n; i++) {
		size_t len = strlen(z.e[i].name);
		if (len < 4 || strcmp(z.e[i].name + len - 4, ".png") != 0)
			continue;
		size_t   n;
		uint8_t* data = read_entry(&z, i, &n);
		CHECK(data != NULL);
		if (data)
			check_png(z.e[i].name, data, n);
		free(data);
		pngs++;
	}
	printf("%s: %d entries, %d PNGs checked against stb_image\n", path, z.n, pngs);
	int has_manifest = zip_find(&z, "manifest.json") >= 0;
	zip_close(&z);

	if (has_manifest) {
		char   err[128];
		CBook* cb = cbook_open(path, err, sizeof(err));
		CHECK(cb != NULL);
		if (!cb) {
			fprintf(stderr, "  %s: %s\n", path, err);
			return;
		}
		Book* b = &cb->b;
		CHECK(b->npages > 0);
		// Every page and panel renders on a 320x480 screen.
		Canvas cv = { malloc(320 * 480), 320, 480, 0 };
		Sink   s  = { cv_row, cv_fill, &cv };
		Pos    p  = nav_start(b);
		int    shown = 0;
		do {
			Zoom        zm = { 1, 0, 0 };
			ZoomBase    base;
			ViewLayout  v   = { { 0 }, false, 320, 480, &zm, &base };
			const char* e   = NULL;
			v.upscale       = nav_rect(b, p, v.rect);
			cv.rows         = 0;
			CHECK(render_entry(&cb->zip, nav_image(b, p), layout_view, &v, &s, 255, &e) == 0);
			CHECK(cv.rows > 0);
			shown++;
		} while (nav_next(b, &p));
		printf("%s: %s, %d pages, %d views drawn\n", path, b->title, b->npages, shown);
		free(cv.px);
		cbook_close(cb);
	}
}

// ---- renderer ----------------------------------------------------------------------------

// A w x h gray PNG (uncompressed deflate blocks) built in memory.
static uint32_t crc_table[256];

static uint32_t crc(const uint8_t* p, size_t n, uint32_t c)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t k = i;
			for (int j = 0; j < 8; j++)
				k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1;
			crc_table[i] = k;
		}
	c = ~c;
	while (n--)
		c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
	return ~c;
}

static void put32(uint8_t* p, uint32_t v)
{
	p[0] = (uint8_t) (v >> 24), p[1] = (uint8_t) (v >> 16), p[2] = (uint8_t) (v >> 8), p[3] = (uint8_t) v;
}

static uint8_t* make_png(const uint8_t* px, int w, int h, size_t* out_len)
{
	size_t   raw_len = (size_t) (w + 1) * h;
	uint8_t* raw     = malloc(raw_len);
	for (int y = 0; y < h; y++) {
		raw[(size_t) y * (w + 1)] = 0;
		memcpy(raw + (size_t) y * (w + 1) + 1, px + (size_t) y * w, (size_t) w);
	}
	size_t   zlen;
	uint8_t* z   = tdefl_compress_mem_to_heap(raw, raw_len, &zlen, TDEFL_WRITE_ZLIB_HEADER | 128);
	size_t   len = 8 + 25 + 12 + zlen + 12;
	uint8_t* png = malloc(len), *q = png;
	memcpy(q, "\x89PNG\r\n\x1a\n", 8), q += 8;
	put32(q, 13), memcpy(q + 4, "IHDR", 4);
	put32(q + 8, (uint32_t) w), put32(q + 12, (uint32_t) h);
	q[16] = 8, q[17] = 0, q[18] = 0, q[19] = 0, q[20] = 0;
	put32(q + 21, crc(q + 4, 17, 0)), q += 25;
	put32(q, (uint32_t) zlen), memcpy(q + 4, "IDAT", 4), memcpy(q + 8, z, zlen);
	put32(q + 8 + zlen, crc(q + 4, zlen + 4, 0)), q += 12 + zlen;
	put32(q, 0), memcpy(q + 4, "IEND", 4), put32(q + 8, crc(q + 4, 4, 0));
	free(raw);
	mz_free(z);
	*out_len = len;
	return png;
}

static int fixed_layout(void* u, int w, int h, Placement* out)
{
	*out = *(Placement*) u;
	return 0;
}

static void render_to(const uint8_t* png, size_t len, Placement pl, Canvas* cv)
{
	Mem         m   = { png, len, 0, 0 };
	Sink        s   = { cv_row, cv_fill, cv };
	const char* err = NULL;
	memset(cv->px, 1, (size_t) cv->w * cv->h);    // 1 = never written
	CHECK(render_png(mem_read, &m, fixed_layout, &pl, &s, 255, &err) == 0);
}

static void test_render(void)
{
	enum { W = 64, H = 48 };
	uint8_t src[W * H];
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			src[y * W + x] = (uint8_t) ((x * 7 + y * 13) ^ (x * y));
	size_t   len;
	uint8_t* png = make_png(src, W, H, &len);
	Canvas   cv  = { malloc(100 * 100), 100, 100, 0 };

	// 1:1 copy, placed at (10, 20) in a 100x100 area: exact, and white around it.
	render_to(png, len, (Placement){ 0, 0, W, H, 10, 20, W, H, 0, 0, 100, 100 }, &cv);
	int bad = 0;
	for (int y = 0; y < 100; y++)
		for (int x = 0; x < 100; x++) {
			bool    in   = x >= 10 && x < 10 + W && y >= 20 && y < 20 + H;
			uint8_t want = in ? src[(y - 20) * W + x - 10] : 255;
			bad += cv.px[y * 100 + x] != want;
		}
	CHECK(bad == 0);

	// Halving: each pixel is the rounded mean of a 2x2 block.
	render_to(png, len, (Placement){ 0, 0, W, H, 0, 0, W / 2, H / 2, 0, 0, 100, 100 }, &cv);
	bad = 0;
	for (int y = 0; y < H / 2; y++)
		for (int x = 0; x < W / 2; x++) {
			// Horizontal mean first (rounded), then vertical, like the renderer.
			int a = (src[2 * y * W + 2 * x] + src[2 * y * W + 2 * x + 1] + 1) / 2;
			int b = (src[(2 * y + 1) * W + 2 * x] + src[(2 * y + 1) * W + 2 * x + 1] + 1) / 2;
			bad += abs(cv.px[y * 100 + x] - (a + b + 1) / 2) > 1;
		}
	CHECK(bad == 0);

	// Crop of the source (rows 10..29, cols 20..51) shown 1:1.
	render_to(png, len, (Placement){ 20, 10, 32, 20, 0, 0, 32, 20, 0, 0, 100, 100 }, &cv);
	bad = 0;
	for (int y = 0; y < 20; y++)
		for (int x = 0; x < 32; x++)
			bad += cv.px[y * 100 + x] != src[(y + 10) * W + x + 20];
	CHECK(bad == 0);

	// Enlarging a constant image stays constant; a destination reaching past
	// the area is clipped (the canvas checks every write is inside it).
	uint8_t flat[W * H];
	memset(flat, 77, sizeof(flat));
	size_t   flen;
	uint8_t* fpng = make_png(flat, W, H, &flen);
	render_to(fpng, flen, (Placement){ 0, 0, W, H, -30, -40, W * 3, H * 3, 0, 0, 100, 100 }, &cv);
	bad = 0;
	for (int i = 0; i < 100 * 100; i++)
		bad += cv.px[i] != 77;
	CHECK(bad == 0);

	// Enlarging keeps a gradient monotonic.
	uint8_t grad[W * H];
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			grad[y * W + x] = (uint8_t) (x * 4);
	size_t   glen;
	uint8_t* gpng = make_png(grad, W, H, &glen);
	render_to(gpng, glen, (Placement){ 0, 0, 25, 25, 0, 0, 100, 100, 0, 0, 100, 100 }, &cv);
	bad = 0;
	for (int y = 0; y < 100; y++)
		for (int x = 1; x < 100; x++)
			bad += cv.px[y * 100 + x] < cv.px[y * 100 + x - 1];
	CHECK(bad == 0);
	CHECK(cv.px[0] == 0 && cv.px[99] >= 90 && cv.px[99] <= 96);

	// A view zoomed into a panel: everything on screen is either the panel or white.
	Zoom       zm = { 1, 0, 0 };
	ZoomBase   base;
	ViewLayout v = { { 8, 8, 32, 16 }, true, 100, 100, &zm, &base };
	Placement  pl;
	CHECK(layout_view(&v, W, H, &pl) == 0);
	CHECK(pl.sx == 8 && pl.sy == 8 && pl.sw == 32 && pl.sh == 16);
	CHECK(pl.dw == 100 && pl.dh == 50 && pl.dx == 0 && pl.dy == 25);
	CHECK(zoom_pinch(&zm, &base, 50, 50, 50, 50, 2.0));
	CHECK(layout_view(&v, W, H, &pl) == 0);
	CHECK(pl.sx >= 8 && pl.sx + pl.sw <= 40 && pl.sy >= 8 && pl.sy + pl.sh <= 24);
	CHECK(pl.dx <= 0 && pl.dx + pl.dw >= 100);    // fills the width when zoomed

	// Fit layout keeps the aspect ratio, centred.
	FitLayout fl = { 10, 10, 50, 50, true, 0, 0 };
	CHECK(layout_fit(&fl, 200, 100, &pl) == 0);
	CHECK(pl.dw == 50 && pl.dh == 25 && pl.dx == 10 && pl.dy == 22 && fl.img_w == 200);

	free(png);
	free(fpng);
	free(gpng);
	free(cv.px);
}

// ---- manifest ----------------------------------------------------------------------------

static int parse(const char* json, Book* b, char* err)
{
	Mem m = { (const uint8_t*) json, strlen(json), 0, 5 };
	memset(b, 0, sizeof(*b));
	return cbook_parse_manifest(b, mem_read, &m, err, 128);
}

static void test_manifest(void)
{
	Book b;
	char err[128] = "";
	CHECK(parse("{\"version\":2,\"title\":\"Caf\\u00e9 \\\"x\\\"\",\"direction\":\"ltr\",\"screen\":[320,480],"
		    "\"extra\":{\"a\":[1,{\"b\":null}],\"c\":true},"
		    "\"pages\":[{\"image\":\"p1.png\",\"panels\":[]},"
		    " {\"panels\":[{\"rect\":[1,2,3,4],\"box\":[0,0,9,9]},{\"rect\":[0,0,0,5]},{\"rect\":[5,6,7,8]}],"
		    "  \"image\":\"p2.png\"}]}",
		    &b, err) == 0);
	CHECK(strcmp(b.title, "Caf\xc3\xa9 \"x\"") == 0);
	CHECK(!b.rtl && b.screen_w == 320 && b.screen_h == 480 && b.npages == 2);
	CHECK(strcmp(b.pages[0].image, "p1.png") == 0 && b.pages[0].npanels == 0);
	CHECK(strcmp(b.pages[1].image, "p2.png") == 0 && b.pages[1].npanels == 2);    // the empty one is dropped
	CHECK(b.pages[1].panels[1].x == 5 && b.pages[1].panels[1].h == 8 && !b.pages[1].panels[1].image);
	cbook_free_pages(&b);
	free(b.title);

	// Version 1: panels as their own images. No direction = RTL.
	CHECK(parse("{\"version\":1,\"pages\":[{\"image\":\"a.png\",\"panels\":[{\"image\":\"a_1.png\"}]}]}", &b, err) == 0);
	CHECK(b.rtl && b.pages[0].npanels == 1 && strcmp(b.pages[0].panels[0].image, "a_1.png") == 0);
	cbook_free_pages(&b);
	free(b.title);

	// Many pages and panels (growing arrays).
	char* big = malloc(200000);
	int   n   = sprintf(big, "{\"version\":2,\"pages\":[");
	for (int i = 0; i < 500; i++)
		n += sprintf(big + n, "%s{\"image\":\"p%d.png\",\"panels\":[%s]}", i ? "," : "", i,
			     "{\"rect\":[1,1,2,2]},{\"rect\":[1,1,2,2]},{\"rect\":[1,1,2,2]},{\"rect\":[1,1,2,2]},"
			     "{\"rect\":[1,1,2,2]},{\"rect\":[1,1,2,2]},{\"rect\":[1,1,2,2]},{\"rect\":[1,1,2,2]},"
			     "{\"rect\":[1,1,2,2]},{\"rect\":[9,9,9,9]}");
	strcpy(big + n, "]}");
	CHECK(parse(big, &b, err) == 0);
	CHECK(b.npages == 500 && b.pages[499].npanels == 10 && b.pages[499].panels[9].x == 9);
	cbook_free_pages(&b);
	free(b.title);
	free(big);

	// Errors.
	err[0] = 0;
	CHECK(parse("{\"version\":3,\"pages\":[{\"image\":\"a.png\"}]}", &b, err) != 0 && strstr(err, "version"));
	err[0] = 0;
	CHECK(parse("{\"version\":2,\"pages\":[]}", &b, err) != 0 && strstr(err, "no pages"));
	err[0] = 0;
	CHECK(parse("{\"version\":2,\"pages\":[{\"panels\":[]}]}", &b, err) != 0 && strstr(err, "no image"));
	err[0] = 0;
	CHECK(parse("{\"version\":2,\"pages\":[{\"image\":\"a.png\"}", &b, err) != 0 && strstr(err, "JSON"));
	err[0] = 0;
	CHECK(parse("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[", &b, err) != 0);
}

// ---- touch gestures ----------------------------------------------------------------------

static bool feed(Tin* t, int n, int x0, int y0, int x1, int y1, int64_t now, InEvent* ev)
{
	TPoint p[2] = { { x0, y0 }, { x1, y1 } };
	return tin_feed(t, n, p, now, ev);
}

static void test_touch(void)
{
	Tin     t;
	InEvent ev;
	tin_init(&t);
	// Tap: a little jitter, released quickly.
	CHECK(!feed(&t, 1, 100, 200, 0, 0, 0, &ev));
	CHECK(!feed(&t, 1, 104, 203, 0, 0, 20, &ev));
	CHECK(feed(&t, 0, 0, 0, 0, 0, 120, &ev) && ev.kind == IN_TAP && ev.x == 100 && ev.y == 200);
	// Long press fires while held, then the release is ignored.
	CHECK(!feed(&t, 1, 50, 60, 0, 0, 1000, &ev));
	CHECK(!feed(&t, 1, 52, 60, 0, 0, 1500, &ev));
	CHECK(feed(&t, 1, 52, 61, 0, 0, 1000 + TIN_LONG_MS, &ev) && ev.kind == IN_LONG && ev.x == 50);
	CHECK(!feed(&t, 1, 52, 61, 0, 0, 2000, &ev));
	CHECK(!feed(&t, 0, 0, 0, 0, 0, 2100, &ev));
	// Drag.
	CHECK(!feed(&t, 1, 10, 10, 0, 0, 3000, &ev));
	CHECK(!feed(&t, 1, 60, 30, 0, 0, 3050, &ev));
	CHECK(!feed(&t, 1, 110, 40, 0, 0, 3900, &ev));    // moved: no long press
	CHECK(feed(&t, 0, 0, 0, 0, 0, 3950, &ev) && ev.kind == IN_DRAG && ev.dx == 100 && ev.dy == 30);
	// Pinch out: fingers 100 apart, then 200.
	CHECK(!feed(&t, 2, 100, 100, 200, 100, 5000, &ev));
	CHECK(!feed(&t, 2, 50, 100, 250, 100, 5100, &ev));
	CHECK(feed(&t, 1, 50, 100, 0, 0, 5200, &ev) && ev.kind == IN_PINCH && fabsf(ev.scale - 2.0f) < 1e-3f &&
	      ev.x == 150 && ev.dx == 0);
	CHECK(!feed(&t, 0, 0, 0, 0, 0, 5300, &ev));
	CHECK(t.state == TIN_IDLE);
}

// ---- zip -----------------------------------------------------------------------------------

static void test_zip_errors(void)
{
	Zip z;
	CHECK(zip_open(&z, "/nonexistent.zip") != 0);
	FILE* f = fopen("build/notazip.zip", "wb");
	if (f) {
		for (int i = 0; i < 1000; i++)
			fputs("not a zip ", f);
		fclose(f);
		CHECK(zip_open(&z, "build/notazip.zip") != 0);
	}
}

int main(int argc, char** argv)
{
	test_render();
	test_manifest();
	test_touch();
	test_zip_errors();
	for (int i = 1; i < argc; i++)
		test_zip(argv[i]);
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
