#include "zipf.h"

#include <stdlib.h>
#include <string.h>

#include "miniz.h"

static uint16_t rd16(const uint8_t* p)
{
	return (uint16_t) (p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t* p)
{
	return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static int read_at(Zip* z, long ofs, void* buf, size_t n)
{
	if (z->at != ofs && fseek(z->f, ofs, SEEK_SET) != 0) {
		z->at = -1;
		return -1;
	}
	if (fread(buf, 1, n, z->f) != n) {
		z->at = -1;
		return -1;
	}
	z->at = ofs + (long) n;
	return 0;
}

// Find the end-of-central-directory record: scan the last 64 KiB (+22) of the
// file backwards in small blocks.
static long find_eocd(Zip* z, long size, uint8_t* eocd)
{
	enum { BLK = 512 };
	uint8_t buf[BLK + 3];
	long    lo  = size - 22 - 65535;
	long    end = size;    // scan [start, end) each round
	if (lo < 0)
		lo = 0;
	while (end > lo) {
		long start = end - BLK;
		if (start < lo)
			start = lo;
		size_t n = (size_t) (end - start) + (end + 3 <= size ? 3 : (size_t) (size - end));
		if (read_at(z, start, buf, n) != 0)
			return -1;
		for (long i = (long) n - 4; i >= 0; i--) {
			if (buf[i] == 'P' && buf[i + 1] == 'K' && buf[i + 2] == 5 && buf[i + 3] == 6 && start + i + 22 <= size) {
				if (read_at(z, start + i, eocd, 22) != 0)
					return -1;
				return start + i;
			}
		}
		end = start;
	}
	return -1;
}

int zip_open(Zip* z, const char* path)
{
	memset(z, 0, sizeof(*z));
	z->f = fopen(path, "rb");
	if (!z->f)
		return -1;
	setvbuf(z->f, NULL, _IOFBF, 4096);    // newlib's default buffer is tiny
	z->at = -1;
	uint8_t eocd[22], h[46];
	if (fseek(z->f, 0, SEEK_END) != 0)
		goto fail;
	long size = ftell(z->f);
	z->at = -1;
	if (size < 22 || find_eocd(z, size, eocd) < 0)
		goto fail;
	int      n      = rd16(eocd + 10);
	uint32_t cd_ofs = rd32(eocd + 16);
	if (n == 0xFFFF || cd_ofs == 0xFFFFFFFFu)    // ZIP64: not needed for books
		goto fail;
	z->e = calloc(n ? (size_t) n : 1, sizeof(ZipEntry));
	if (!z->e || fseek(z->f, (long) cd_ofs, SEEK_SET) != 0)
		goto fail;
	for (int i = 0; i < n; i++) {
		if (fread(h, 1, 46, z->f) != 46 || rd32(h) != 0x02014b50)
			goto fail;
		int nlen = rd16(h + 28), xlen = rd16(h + 30), clen = rd16(h + 32);
		ZipEntry* e = &z->e[z->n];
		e->method   = rd16(h + 10);
		e->csize    = rd32(h + 20);
		e->usize    = rd32(h + 24);
		e->ofs      = rd32(h + 42);
		e->name     = malloc((size_t) nlen + 1);
		if (!e->name || fread(e->name, 1, (size_t) nlen, z->f) != (size_t) nlen) {
			free(e->name);
			goto fail;
		}
		e->name[nlen] = 0;
		z->n++;
		if (fseek(z->f, xlen + clen, SEEK_CUR) != 0)
			goto fail;
	}
	z->at = -1;
	return 0;
fail:
	zip_close(z);
	return -1;
}

void zip_close(Zip* z)
{
	for (int i = 0; i < z->n; i++)
		free(z->e[i].name);
	free(z->e);
	if (z->f)
		fclose(z->f);
	memset(z, 0, sizeof(*z));
}

int zip_find(const Zip* z, const char* name)
{
	for (int i = 0; i < z->n; i++)
		if (strcmp(z->e[i].name, name) == 0)
			return i;
	return -1;
}

// ---- entry streams -------------------------------------------------------------

enum { IN_BUF = 2048 };

struct ZipInflate {
	tinfl_decompressor d;
	uint8_t            dict[TINFL_LZ_DICT_SIZE];
	uint8_t            in[IN_BUF];
	size_t             in_ofs, in_len;
	size_t             out_ofs, out_len;    // pending output in dict: [out_ofs, out_ofs + out_len)
	size_t             dict_ofs;            // where tinfl writes next
	int                done;                // 1: stream ended, -1: error
};

int zs_open(ZipStream* s, Zip* z, int index)
{
	memset(s, 0, sizeof(*s));
	if (index < 0 || index >= z->n)
		return -1;
	ZipEntry* e = &z->e[index];
	uint8_t   h[30];
	if (read_at(z, (long) e->ofs, h, 30) != 0 || rd32(h) != 0x04034b50)
		return -1;
	if (e->method != 0 && e->method != 8)
		return -1;
	s->z        = z;
	s->pos      = e->ofs + 30 + rd16(h + 26) + rd16(h + 28);
	s->left     = e->csize;
	s->out_left = e->usize;
	if (e->method == 8) {
		s->inf = malloc(sizeof(ZipInflate));
		if (!s->inf)
			return -1;
		tinfl_init(&s->inf->d);
		s->inf->in_ofs = s->inf->in_len = s->inf->out_ofs = s->inf->out_len = s->inf->dict_ofs = 0;
		s->inf->done = 0;
	}
	return 0;
}

static int raw_read(ZipStream* s, uint8_t* buf, int n)
{
	if ((uint32_t) n > s->left)
		n = (int) s->left;
	if (n <= 0)
		return 0;
	if (read_at(s->z, (long) s->pos, buf, (size_t) n) != 0)
		return -1;
	s->pos += (uint32_t) n;
	s->left -= (uint32_t) n;
	return n;
}

int zs_read(ZipStream* s, uint8_t* buf, int n)
{
	if (!s->z)
		return -1;
	if (!s->inf) {
		int r = raw_read(s, buf, n);
		if (r > 0)
			s->out_left -= (uint32_t) r > s->out_left ? s->out_left : (uint32_t) r;
		return r;
	}
	ZipInflate* f   = s->inf;
	int         got = 0;
	while (got < n) {
		if (f->out_len) {
			size_t k = f->out_len < (size_t) (n - got) ? f->out_len : (size_t) (n - got);
			memcpy(buf + got, f->dict + f->out_ofs, k);
			got += (int) k;
			f->out_ofs += k;
			f->out_len -= k;
			continue;
		}
		if (f->done)
			break;
		if (f->in_ofs == f->in_len && s->left) {
			int r = raw_read(s, f->in, IN_BUF);
			if (r < 0) {
				f->done = -1;
				break;
			}
			f->in_ofs = 0, f->in_len = (size_t) r;
		}
		size_t in_n  = f->in_len - f->in_ofs;
		size_t out_n = TINFL_LZ_DICT_SIZE - f->dict_ofs;
		tinfl_status st = tinfl_decompress(&f->d, f->in + f->in_ofs, &in_n, f->dict, f->dict + f->dict_ofs, &out_n,
						   s->left ? TINFL_FLAG_HAS_MORE_INPUT : 0);
		f->in_ofs += in_n;
		f->out_ofs = f->dict_ofs;
		f->out_len = out_n;
		f->dict_ofs = (f->dict_ofs + out_n) & (TINFL_LZ_DICT_SIZE - 1);
		if (st == TINFL_STATUS_DONE)
			f->done = 1;
		else if (st < 0 || (st == TINFL_STATUS_NEEDS_MORE_INPUT && !s->left && f->in_ofs == f->in_len && !out_n))
			f->done = -1;
	}
	if (got == 0 && f->done < 0)
		return -1;
	return got;
}

void zs_close(ZipStream* s)
{
	free(s->inf);
	memset(s, 0, sizeof(*s));
}

int zs_source(void* stream, uint8_t* buf, int n)
{
	return zs_read(stream, buf, n);
}
