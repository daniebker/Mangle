#include "pngs.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

enum { IN_BUF = 2048, MAX_DIM = 16384 };

struct Png {
	PngRead  rd;
	void*    ctx;
	int      w, h, depth, ctype, channels;
	size_t   rowbytes;    // without the filter byte
	int      bpp;         // bytes per pixel for filtering (at least 1)
	uint8_t  pal[256];    // palette as gray
	uint8_t  palpha[256];
	uint32_t chunk_left;  // bytes left in the current IDAT chunk
	int      idat_end;    // no more IDAT chunks
	tinfl_decompressor inf;
	uint8_t  dict[TINFL_LZ_DICT_SIZE];
	uint8_t  in[IN_BUF];
};

static uint32_t be32(const uint8_t* p)
{
	return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static int read_full(Png* p, void* buf, size_t n)
{
	uint8_t* b = buf;
	while (n) {
		int r = p->rd(p->ctx, b, n > 0x7fffffff ? 0x7fffffff : (int) n);
		if (r <= 0)
			return -1;
		b += r;
		n -= (size_t) r;
	}
	return 0;
}

static int skip(Png* p, uint32_t n)
{
	uint8_t tmp[64];
	while (n) {
		uint32_t k = n < sizeof(tmp) ? n : sizeof(tmp);
		if (read_full(p, tmp, k) != 0)
			return -1;
		n -= k;
	}
	return 0;
}

// Next chunk header. Returns 0 and fills len and type.
static int chunk(Png* p, uint32_t* len, char type[5])
{
	uint8_t h[8];
	if (read_full(p, h, 8) != 0)
		return -1;
	*len = be32(h);
	memcpy(type, h + 4, 4);
	type[4] = 0;
	return 0;
}

#define FAIL(msg)                                                                                                      \
	do {                                                                                                           \
		if (err)                                                                                               \
			*err = msg;                                                                                    \
		goto fail;                                                                                             \
	} while (0)

Png* png_open(PngRead rd, void* ctx, const char** err)
{
	static const uint8_t sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
	Png*                 p      = malloc(sizeof(Png));
	if (!p) {
		if (err)
			*err = "out of memory";
		return NULL;
	}
	memset(p, 0, offsetof(Png, inf));
	p->rd = rd, p->ctx = ctx;
	for (int i = 0; i < 256; i++)
		p->pal[i] = (uint8_t) i, p->palpha[i] = 255;

	uint8_t  b[13];
	uint32_t len;
	char     type[5];
	if (read_full(p, b, 8) != 0 || memcmp(b, sig, 8) != 0)
		FAIL("not a PNG");
	if (chunk(p, &len, type) != 0 || strcmp(type, "IHDR") != 0 || len != 13 || read_full(p, b, 13) != 0 ||
	    skip(p, 4) != 0)
		FAIL("bad PNG header");
	p->w = (int) be32(b), p->h = (int) be32(b + 4);
	p->depth = b[8], p->ctype = b[9];
	if (p->w <= 0 || p->h <= 0 || p->w > MAX_DIM || p->h > MAX_DIM)
		FAIL("bad PNG size");
	if (b[12] != 0)
		FAIL("interlaced PNG not supported");
	switch (p->ctype) {
		case 0: p->channels = 1; break;
		case 2: p->channels = 3; break;
		case 3: p->channels = 1; break;
		case 4: p->channels = 2; break;
		case 6: p->channels = 4; break;
		default: FAIL("bad PNG colour type");
	}
	int d = p->depth;
	if (!(d == 1 || d == 2 || d == 4 || d == 8 || d == 16) || (p->ctype == 3 && d == 16) ||
	    (p->ctype != 0 && p->ctype != 3 && d < 8))
		FAIL("bad PNG bit depth");
	p->rowbytes = ((size_t) p->w * p->channels * d + 7) / 8;
	p->bpp      = p->channels * d / 8 > 0 ? p->channels * d / 8 : 1;

	// Chunks up to the first IDAT.
	for (;;) {
		if (chunk(p, &len, type) != 0)
			FAIL("truncated PNG");
		if (strcmp(type, "IDAT") == 0) {
			p->chunk_left = len;
			break;
		}
		if (strcmp(type, "IEND") == 0)
			FAIL("PNG has no image data");
		if (strcmp(type, "PLTE") == 0 && len % 3 == 0 && len <= 768) {
			uint8_t rgb[3];
			for (uint32_t i = 0; i < len / 3; i++) {
				if (read_full(p, rgb, 3) != 0)
					FAIL("truncated PNG");
				p->pal[i] = (uint8_t) ((rgb[0] * 77 + rgb[1] * 150 + rgb[2] * 29) >> 8);
			}
			len = 0;
		} else if (strcmp(type, "tRNS") == 0 && p->ctype == 3 && len <= 256) {
			if (read_full(p, p->palpha, len) != 0)
				FAIL("truncated PNG");
			len = 0;
		}
		if (skip(p, len + 4) != 0)    // data (unless consumed) + CRC
			FAIL("truncated PNG");
	}
	tinfl_init(&p->inf);
	return p;
fail:
	free(p);
	return NULL;
}

void png_size(const Png* p, int* w, int* h)
{
	*w = p->w, *h = p->h;
}

void png_close(Png* p)
{
	free(p);
}

// Read compressed bytes from the IDAT chunks. Returns count, 0 at the end.
static int idat_read(Png* p, uint8_t* buf, int n)
{
	while (!p->idat_end && p->chunk_left == 0) {
		uint32_t len;
		char     type[5];
		if (skip(p, 4) != 0 || chunk(p, &len, type) != 0)    // CRC of the previous chunk, next header
			return -1;
		if (strcmp(type, "IDAT") != 0)
			p->idat_end = 1;
		else
			p->chunk_left = len;
	}
	if (p->idat_end)
		return 0;
	if ((uint32_t) n > p->chunk_left)
		n = (int) p->chunk_left;
	int r = p->rd(p->ctx, buf, n);
	if (r <= 0)
		return -1;
	p->chunk_left -= (uint32_t) r;
	return r;
}

static uint8_t paeth(int a, int b, int c)
{
	int pa = abs(b - c), pb = abs(a - c), pc = abs(a + b - 2 * c);
	return (uint8_t) (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

static int unfilter(Png* p, int f, uint8_t* cur, const uint8_t* prev)
{
	size_t n = p->rowbytes;
	int    bpp = p->bpp;
	switch (f) {
		case 0: break;
		case 1:
			for (size_t i = (size_t) bpp; i < n; i++)
				cur[i] += cur[i - bpp];
			break;
		case 2:
			for (size_t i = 0; i < n; i++)
				cur[i] += prev[i];
			break;
		case 3:
			for (size_t i = 0; i < n; i++)
				cur[i] += (uint8_t) (((i >= (size_t) bpp ? cur[i - bpp] : 0) + prev[i]) >> 1);
			break;
		case 4:
			for (size_t i = 0; i < n; i++)
				cur[i] += i >= (size_t) bpp ? paeth(cur[i - bpp], prev[i], prev[i - bpp]) : paeth(0, prev[i], 0);
			break;
		default: return -1;
	}
	return 0;
}

static uint8_t over_white(int v, int a)
{
	return (uint8_t) ((v * a + 255 * (255 - a) + 127) / 255);
}

static void to_gray(const Png* p, const uint8_t* s, uint8_t* g)
{
	int w = p->w, d = p->depth;
	if (d < 8) {
		int mask = (1 << d) - 1, mul = 255 / mask;
		for (int x = 0; x < w; x++) {
			int bit = x * d;
			int v   = (s[bit >> 3] >> (8 - d - (bit & 7))) & mask;
			g[x]    = p->ctype == 3 ? over_white(p->pal[v], p->palpha[v]) : (uint8_t) (v * mul);
		}
		return;
	}
	int step = d / 8;    // 1 or 2 bytes per sample; the high byte comes first
	switch (p->ctype) {
		case 0:
			for (int x = 0; x < w; x++)
				g[x] = s[x * step];
			break;
		case 3:
			for (int x = 0; x < w; x++)
				g[x] = over_white(p->pal[s[x]], p->palpha[s[x]]);
			break;
		case 4:
			for (int x = 0; x < w; x++)
				g[x] = over_white(s[2 * x * step], s[(2 * x + 1) * step]);
			break;
		case 2:
		case 6: {
			int c = p->channels;
			for (int x = 0; x < w; x++) {
				const uint8_t* px = s + (size_t) x * c * step;
				int            v  = (px[0] * 77 + px[step] * 150 + px[2 * step] * 29) >> 8;
				g[x]              = c == 4 ? over_white(v, px[3 * step]) : (uint8_t) v;
			}
			break;
		}
	}
}

int png_decode(Png* p, PngRow row, void* ctx)
{
	size_t   rb   = p->rowbytes + 1;    // with the filter byte
	uint8_t* cur  = malloc(rb);
	uint8_t* prev = calloc(1, rb);
	uint8_t* gray = malloc((size_t) p->w);
	int      rc   = -1;
	if (!cur || !prev || !gray)
		goto out;

	size_t in_ofs = 0, in_len = 0, dict_ofs = 0, fill = 0;
	int    y = 0, more = 1;
	while (y < p->h) {
		if (in_ofs == in_len && more) {
			int r = idat_read(p, p->in, IN_BUF);
			if (r < 0)
				goto out;
			if (r == 0)
				more = 0;
			in_ofs = 0, in_len = (size_t) r;
		}
		size_t       in_n  = in_len - in_ofs;
		size_t       out_n = TINFL_LZ_DICT_SIZE - dict_ofs;
		tinfl_status st    = tinfl_decompress(&p->inf, p->in + in_ofs, &in_n, p->dict, p->dict + dict_ofs, &out_n,
							  TINFL_FLAG_PARSE_ZLIB_HEADER | (more ? TINFL_FLAG_HAS_MORE_INPUT : 0));
		in_ofs += in_n;
		const uint8_t* o = p->dict + dict_ofs;
		dict_ofs         = (dict_ofs + out_n) & (TINFL_LZ_DICT_SIZE - 1);
		while (out_n && y < p->h) {
			size_t k = rb - fill < out_n ? rb - fill : out_n;
			memcpy(cur + fill, o, k);
			fill += k, o += k, out_n -= k;
			if (fill < rb)
				break;
			fill = 0;
			if (unfilter(p, cur[0], cur + 1, prev + 1) != 0)
				goto out;
			to_gray(p, cur + 1, gray);
			if (row(ctx, y, gray, p->w)) {
				rc = 0;
				goto out;
			}
			y++;
			uint8_t* t = prev;
			prev = cur, cur = t;
		}
		if (st < 0 || (st == TINFL_STATUS_DONE && y < p->h) ||
		    (st == TINFL_STATUS_NEEDS_MORE_INPUT && !more && in_ofs == in_len))
			goto out;
	}
	rc = 0;
out:
	free(cur);
	free(prev);
	free(gray);
	return rc;
}
