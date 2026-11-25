#include "book.h"
#include "pngs.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define ERR(...)                                                                                                       \
	do {                                                                                                           \
		if (err && !*err)                                                                                      \
			snprintf(err, errlen, __VA_ARGS__);                                                            \
	} while (0)

// ---- streaming JSON ------------------------------------------------------------

enum { JS_BUF = 512, JS_DEPTH = 32, JS_STR = 256 };

typedef struct {
	JsonRead rd;
	void*    ctx;
	uint8_t  buf[JS_BUF];
	int      pos, len;
	int      c;      // current character, -1 at the end
	int      err;    // nonzero after a syntax or read error
	int      depth;
} Js;

static void jadv(Js* j)
{
	if (j->pos == j->len) {
		int r = j->rd(j->ctx, j->buf, JS_BUF);
		if (r <= 0) {
			if (r < 0)
				j->err = 1;
			j->c = -1;
			return;
		}
		j->pos = 0, j->len = r;
	}
	j->c = j->buf[j->pos++];
}

static void jws(Js* j)
{
	while (j->c == ' ' || j->c == '\n' || j->c == '\r' || j->c == '\t')
		jadv(j);
}

static int jexpect(Js* j, int ch)
{
	jws(j);
	if (j->c != ch) {
		j->err = 1;
		return -1;
	}
	jadv(j);
	return 0;
}

static int hexval(int c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// String into out (truncated to n-1 bytes, UTF-8). out may be NULL to skip.
static int jstr(Js* j, char* out, size_t n)
{
	size_t k = 0;
	if (jexpect(j, '"') != 0)
		return -1;
	while (j->c != '"') {
		if (j->c < 0) {
			j->err = 1;
			return -1;
		}
		unsigned cp = (unsigned) j->c;
		if (j->c == '\\') {
			jadv(j);
			switch (j->c) {
				case 'n': cp = '\n'; break;
				case 't': cp = '\t'; break;
				case 'r': cp = '\r'; break;
				case 'b': cp = '\b'; break;
				case 'f': cp = '\f'; break;
				case 'u':
					cp = 0;
					for (int i = 0; i < 4; i++) {
						jadv(j);
						int h = hexval(j->c);
						if (h < 0) {
							j->err = 1;
							return -1;
						}
						cp = cp << 4 | (unsigned) h;
					}
					break;
				default: cp = (unsigned) j->c; break;    // \" \\ \/
			}
			if (cp >= 0x80 && out) {    // encode as UTF-8 (surrogates kept as-is)
				char u[3];
				int  m = 0;
				if (cp < 0x800)
					u[m++] = (char) (0xC0 | cp >> 6);
				else
					u[m++] = (char) (0xE0 | cp >> 12), u[m++] = (char) (0x80 | ((cp >> 6) & 0x3F));
				u[m++] = (char) (0x80 | (cp & 0x3F));
				for (int i = 0; i < m; i++)
					if (k + 1 < n)
						out[k++] = u[i];
				jadv(j);
				continue;
			}
		}
		if (out && k + 1 < n)
			out[k++] = (char) cp;
		jadv(j);
	}
	jadv(j);
	if (out && n)
		out[k] = 0;
	return 0;
}

static int jnum(Js* j, double* v)
{
	char   tmp[40];
	size_t k = 0;
	jws(j);
	while ((j->c >= '0' && j->c <= '9') || j->c == '-' || j->c == '+' || j->c == '.' || j->c == 'e' || j->c == 'E') {
		if (k + 1 < sizeof(tmp))
			tmp[k++] = (char) j->c;
		jadv(j);
	}
	tmp[k] = 0;
	char* end;
	*v = strtod(tmp, &end);
	if (k == 0 || *end) {
		j->err = 1;
		return -1;
	}
	return 0;
}

typedef int (*JField)(Js* j, const char* key, void* u);
typedef int (*JItem)(Js* j, int idx, void* u);

static int jskip(Js* j);

static int jobj(Js* j, JField f, void* u)
{
	char key[64];
	if (++j->depth > JS_DEPTH || jexpect(j, '{') != 0)
		return j->err = 1, -1;
	jws(j);
	if (j->c == '}') {
		jadv(j);
		j->depth--;
		return 0;
	}
	for (;;) {
		if (jstr(j, key, sizeof(key)) != 0 || jexpect(j, ':') != 0)
			return -1;
		if ((f ? f(j, key, u) : jskip(j)) != 0 || j->err)
			return -1;
		jws(j);
		if (j->c == ',') {
			jadv(j);
			continue;
		}
		if (jexpect(j, '}') != 0)
			return -1;
		j->depth--;
		return 0;
	}
}

static int jarr(Js* j, JItem f, void* u)
{
	if (++j->depth > JS_DEPTH || jexpect(j, '[') != 0)
		return j->err = 1, -1;
	jws(j);
	if (j->c == ']') {
		jadv(j);
		j->depth--;
		return 0;
	}
	for (int i = 0;; i++) {
		if ((f ? f(j, i, u) : jskip(j)) != 0 || j->err)
			return -1;
		jws(j);
		if (j->c == ',') {
			jadv(j);
			continue;
		}
		if (jexpect(j, ']') != 0)
			return -1;
		j->depth--;
		return 0;
	}
}

static int jword(Js* j, const char* w)
{
	for (; *w; w++) {
		if (j->c != *w)
			return j->err = 1, -1;
		jadv(j);
	}
	return 0;
}

static int jskip(Js* j)
{
	double v;
	jws(j);
	switch (j->c) {
		case '{': return jobj(j, NULL, NULL);
		case '[': return jarr(j, NULL, NULL);
		case '"': return jstr(j, NULL, 0);
		case 't': return jword(j, "true");
		case 'f': return jword(j, "false");
		case 'n': return jword(j, "null");
		default: return jnum(j, &v);
	}
}

// Value kind at the cursor: '"', '[', '{', or 0 for anything else.
static int jpeek(Js* j)
{
	jws(j);
	return j->c == '"' || j->c == '[' || j->c == '{' ? j->c : 0;
}

// ---- manifest ------------------------------------------------------------------

static char* dupstr(const char* s)
{
	size_t n = strlen(s) + 1;
	char*  d = malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

typedef struct {
	Book* b;
	int   version;
	int   pages_cap;
	int   panels_cap;    // of the page being parsed
	int   nums[4];
	int   nnums;
	char  str[JS_STR];
	int   oom;
} M;

static int num_item(Js* j, int idx, void* u)
{
	M*     m = u;
	double v;
	if (jnum(j, &v) != 0)
		return -1;
	if (idx < 4)
		m->nums[idx] = (int) v;
	m->nnums = idx + 1;
	return 0;
}

static int panel_field(Js* j, const char* key, void* u)
{
	M*     m  = u;
	Page*  pg = &m->b->pages[m->b->npages - 1];
	Panel* pn = &pg->panels[pg->npanels];
	if (strcmp(key, "image") == 0 && jpeek(j) == '"') {
		if (jstr(j, m->str, sizeof(m->str)) != 0)
			return -1;
		free(pn->image);
		pn->image = dupstr(m->str);
		if (!pn->image)
			m->oom = 1;
		return 0;
	}
	if (strcmp(key, "rect") == 0 && jpeek(j) == '[') {
		m->nnums = 0;
		if (jarr(j, num_item, m) != 0)
			return -1;
		if (m->nnums == 4)
			pn->x = m->nums[0], pn->y = m->nums[1], pn->w = m->nums[2], pn->h = m->nums[3];
		return 0;
	}
	return jskip(j);
}

static int panel_item(Js* j, int idx, void* u)
{
	M*    m  = u;
	Page* pg = &m->b->pages[m->b->npages - 1];
	if (pg->npanels == m->panels_cap) {
		int    cap = m->panels_cap ? m->panels_cap * 2 : 8;
		Panel* t   = realloc(pg->panels, (size_t) cap * sizeof(Panel));
		if (!t)
			return m->oom = 1, -1;
		pg->panels    = t;
		m->panels_cap = cap;
	}
	Panel* pn = &pg->panels[pg->npanels];
	memset(pn, 0, sizeof(*pn));
	if (jpeek(j) != '{')
		return jskip(j);
	if (jobj(j, panel_field, m) != 0)
		return -1;
	if (pn->image || (pn->w > 0 && pn->h > 0))
		pg->npanels++;
	return 0;
}

static int page_field(Js* j, const char* key, void* u)
{
	M*    m  = u;
	Page* pg = &m->b->pages[m->b->npages - 1];
	if (strcmp(key, "image") == 0 && jpeek(j) == '"') {
		if (jstr(j, m->str, sizeof(m->str)) != 0)
			return -1;
		free(pg->image);
		pg->image = dupstr(m->str);
		if (!pg->image)
			m->oom = 1;
		return 0;
	}
	if (strcmp(key, "panels") == 0 && jpeek(j) == '[') {
		m->panels_cap = pg->npanels ? pg->npanels : 0;
		return jarr(j, panel_item, m);
	}
	return jskip(j);
}

static int page_item(Js* j, int idx, void* u)
{
	M*    m = u;
	Book* b = m->b;
	if (b->npages == m->pages_cap) {
		int   cap = m->pages_cap ? m->pages_cap * 2 : 64;
		Page* t   = realloc(b->pages, (size_t) cap * sizeof(Page));
		if (!t)
			return m->oom = 1, -1;
		b->pages     = t;
		m->pages_cap = cap;
	}
	memset(&b->pages[b->npages++], 0, sizeof(Page));
	m->panels_cap = 0;
	if (jpeek(j) != '{')
		return j->err = 1, -1;
	return jobj(j, page_field, m);
}

static int root_field(Js* j, const char* key, void* u)
{
	M*    m = u;
	Book* b = m->b;
	if (strcmp(key, "version") == 0 && jpeek(j) == 0) {
		double v;
		if (jnum(j, &v) != 0)
			return -1;
		m->version = (int) v;
		return 0;
	}
	if (strcmp(key, "title") == 0 && jpeek(j) == '"') {
		if (jstr(j, m->str, sizeof(m->str)) != 0)
			return -1;
		free(b->title);
		b->title = dupstr(m->str);
		return 0;
	}
	if (strcmp(key, "direction") == 0 && jpeek(j) == '"') {
		if (jstr(j, m->str, sizeof(m->str)) != 0)
			return -1;
		b->rtl = strcmp(m->str, "ltr") != 0;
		return 0;
	}
	if (strcmp(key, "screen") == 0 && jpeek(j) == '[') {
		m->nnums = 0;
		if (jarr(j, num_item, m) != 0)
			return -1;
		if (m->nnums == 2)
			b->screen_w = m->nums[0], b->screen_h = m->nums[1];
		return 0;
	}
	if (strcmp(key, "pages") == 0 && jpeek(j) == '[')
		return jarr(j, page_item, m);
	return jskip(j);
}

void book_free_pages(Book* b)
{
	for (int i = 0; i < b->npages; i++) {
		free(b->pages[i].image);
		for (int k = 0; k < b->pages[i].npanels; k++)
			free(b->pages[i].panels[k].image);
		free(b->pages[i].panels);
	}
	free(b->pages);
	b->pages  = NULL;
	b->npages = 0;
}

int book_parse_manifest(Book* b, JsonRead rd, void* ctx, char* err, size_t errlen)
{
	Js* j = calloc(1, sizeof(Js));
	M*  m = calloc(1, sizeof(M));
	int rc = -1;
	if (!j || !m) {
		ERR("out of memory");
		goto out;
	}
	j->rd = rd, j->ctx = ctx;
	m->b  = b;
	b->rtl = true;
	jadv(j);
	if (jobj(j, root_field, m) != 0 || j->err) {
		// Pages with a missing image make the error below more useful.
		ERR(m->oom ? "out of memory reading manifest" : "manifest.json is not valid JSON");
		goto out;
	}
	if (m->version != 1 && m->version != 2) {
		ERR("unsupported manifest version");
		goto out;
	}
	if (b->npages == 0) {
		ERR("manifest has no pages");
		goto out;
	}
	for (int i = 0; i < b->npages; i++) {
		if (!b->pages[i].image) {
			ERR("page %d has no image", i + 1);
			goto out;
		}
		if (b->pages[i].npanels == 0) {
			free(b->pages[i].panels);
			b->pages[i].panels = NULL;
		}
	}
	if (!b->title)
		b->title = dupstr("");
	rc = 0;
out:
	if (rc)
		book_free_pages(b);
	free(j);
	free(m);
	return rc;
}

// ---- books ---------------------------------------------------------------------

Book* book_open(const char* path, char* err, size_t errlen)
{
	if (err && errlen)
		*err = 0;
	Book* b = calloc(1, sizeof(Book));
	if (!b) {
		ERR("out of memory");
		return NULL;
	}
	pthread_mutex_init(&b->lock, NULL);
	b->path = dupstr(path);
	if (zip_open(&b->zip, path) != 0) {
		ERR("cannot open ZIP");
		book_close(b);
		return NULL;
	}
	int idx = zip_find(&b->zip, "manifest.json");
	if (idx < 0) {
		ERR("no manifest.json (was this ZIP made by prep.py?)");
		book_close(b);
		return NULL;
	}
	ZipStream s;
	if (zs_open(&s, &b->zip, idx) != 0) {
		ERR("cannot read manifest.json");
		book_close(b);
		return NULL;
	}
	int rc = book_parse_manifest(b, zs_source, &s, err, errlen);
	zs_close(&s);
	if (rc) {
		book_close(b);
		return NULL;
	}
	return b;
}

void book_close(Book* b)
{
	if (!b)
		return;
	zip_close(&b->zip);
	book_free_pages(b);
	free(b->title);
	free(b->path);
	pthread_mutex_destroy(&b->lock);
	free(b);
}

typedef struct {
	Image* img;
} Whole;

static int whole_row(void* ctx, int y, const uint8_t* gray, int w)
{
	Whole* d = ctx;
	memcpy(d->img->px + (size_t) y * w, gray, (size_t) w);
	return 0;
}

int book_load_image(Book* b, const char* name, Image* img)
{
	img->px = NULL;
	img->w = img->h = 0;
	int rc = -1;
	pthread_mutex_lock(&b->lock);
	int       idx = zip_find(&b->zip, name);
	ZipStream s;
	if (idx >= 0 && zs_open(&s, &b->zip, idx) == 0) {
		const char* e = NULL;
		Png*        p = png_open(zs_source, &s, &e);
		if (p) {
			png_size(p, &img->w, &img->h);
			img->px = malloc((size_t) img->w * img->h);
			Whole d = { img };
			if (img->px && png_decode(p, whole_row, &d) == 0)
				rc = 0;
			png_close(p);
		}
		zs_close(&s);
	}
	pthread_mutex_unlock(&b->lock);
	if (rc) {
		free(img->px);
		img->px = NULL;
		img->w = img->h = 0;
	}
	return rc;
}

void image_free(Image* img)
{
	free(img->px);
	img->px = NULL;
}
