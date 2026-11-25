#include "store.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>


#define PATH_MAX_ 560

static char dir[300] = "state";

void store_init(const char* d)
{
	snprintf(dir, sizeof(dir), "%s", d);
}

const char* store_dir(void)
{
	return dir;
}

static void path_for(const char* file, char* out)
{
	snprintf(out, PATH_MAX_, "%s/%s", dir, file);
}

// "<state>/<book file name><ext>"
static void book_path(const char* book, const char* ext, char* out)
{
	const char* base = strrchr(book, '/');
	base             = base ? base + 1 : book;
	char   safe[200];
	size_t i = 0;
	for (; base[i] && i < sizeof(safe) - 1; i++) {
		char c  = base[i];
		safe[i] = (c == '\\' || c == '\n' || (unsigned char) c < 32) ? '_' : c;
	}
	safe[i] = 0;
	snprintf(out, PATH_MAX_, "%s/%s%s", dir, safe, ext);
}

int store_write(const char* path, const void* data, int len)
{
	char tmp[PATH_MAX_ + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(dir, 0755);
	FILE* f = fopen(tmp, "wb");
	if (!f)
		return -1;
	int ok = fwrite(data, 1, (size_t) len, f) == (size_t) len;
	ok &= fclose(f) == 0;
	if (!ok) {
		remove(tmp);
		return -1;
	}
	if (rename(tmp, path) != 0) {
		remove(path);
		if (rename(tmp, path) != 0) {
			remove(tmp);
			return -1;
		}
	}
	return 0;
}

void store_load_settings(Settings* s)
{
	char path[PATH_MAX_];
	path_for("settings", path);
	FILE* f = fopen(path, "r");
	if (!f)
		return;
	char line[64];
	while (fgets(line, sizeof(line), f)) {
		int v;
		if (sscanf(line, "overview=%d", &v) == 1)
			s->overview = v != 0;
		else if (sscanf(line, "sleep_min=%d", &v) == 1)
			s->sleep_min = v < 0 ? 0 : v;
		else if (sscanf(line, "brightness=%d", &v) == 1)
			s->brightness = v < 5 ? 5 : v > 100 ? 100 : v;
		else if (sscanf(line, "rotation=%d", &v) == 1)
			s->rotation = v & 3;
		else if (sscanf(line, "night=%d", &v) == 1)
			s->night = v != 0;
		else if (sscanf(line, "invert=%d", &v) == 1)
			s->invert = v != 0;
		else if (sscanf(line, "sleep=%d", &v) == 1 && !v)    // older Kindle builds: sleep on/off
			s->sleep_min = 0;
	}
	fclose(f);
}

int store_save_settings(const Settings* s)
{
	char path[PATH_MAX_], text[160];
	path_for("settings", path);
	int n = snprintf(text, sizeof(text), "overview=%d\nsleep_min=%d\nbrightness=%d\nrotation=%d\nnight=%d\ninvert=%d\n",
			 s->overview, s->sleep_min, s->brightness, s->rotation, s->night, s->invert);
	return store_write(path, text, n);
}

int store_load_pos(const char* book, Pos* p, int* npages)
{
	char path[PATH_MAX_];
	book_path(book, ".pos", path);
	FILE* f = fopen(path, "r");
	if (!f)
		return -1;
	int page, panel, mode, np = 0;
	int n = fscanf(f, "%d %d %d %d", &page, &panel, &mode, &np);
	fclose(f);
	if (n < 3)
		return -1;
	*p = (Pos){ page, panel, mode ? MODE_PANEL : MODE_PAGE };
	if (npages)
		*npages = n == 4 ? np : 0;
	return 0;
}

int store_save_pos(const char* book, Pos p, int npages)
{
	char path[PATH_MAX_], text[64];
	book_path(book, ".pos", path);
	int n = snprintf(text, sizeof(text), "%d %d %d %d\n", p.page, p.panel, p.mode == MODE_PANEL, npages);
	return store_write(path, text, n);
}

// Thumbnail file: "MGT2 <w> <h> <stamp>\n<title>\n" then w*h gray bytes.
int store_load_thumb(const char* book, long stamp, int w, int h, uint8_t* px, char* title, int title_len)
{
	char path[PATH_MAX_];
	book_path(book, ".thm", path);
	FILE* f = fopen(path, "rb");
	if (!f)
		return -1;
	char line[160];
	int  tw, th, rc = -1;
	long ts;
	if (fgets(line, sizeof(line), f) && sscanf(line, "MGT2 %d %d %ld", &tw, &th, &ts) == 3 && tw == w && th == h &&
	    ts == stamp &&
	    fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		snprintf(title, (size_t) title_len, "%s", line);
		if (fread(px, 1, (size_t) w * h, f) == (size_t) w * h)
			rc = 0;
	}
	fclose(f);
	return rc;
}

int store_save_thumb(const char* book, long stamp, int w, int h, const uint8_t* px, const char* title)
{
	char path[PATH_MAX_], tmp[PATH_MAX_ + 8];
	book_path(book, ".thm", path);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(dir, 0755);
	FILE* f = fopen(tmp, "wb");
	if (!f)
		return -1;
	char t[120];
	snprintf(t, sizeof(t), "%s", title);
	for (char* c = t; *c; c++)
		if (*c == '\n' || *c == '\r')
			*c = ' ';
	int ok = fprintf(f, "MGT2 %d %d %ld\n%s\n", w, h, stamp, t) > 0;
	ok &= fwrite(px, 1, (size_t) w * h, f) == (size_t) w * h;
	ok &= fclose(f) == 0;
	if (ok && rename(tmp, path) != 0) {
		remove(path);
		ok = rename(tmp, path) == 0;
	}
	if (!ok)
		remove(tmp);
	return ok ? 0 : -1;
}

int store_load_calib(int32_t* v, int n)
{
	char path[PATH_MAX_];
	path_for("touch_calibration", path);
	FILE* f = fopen(path, "r");
	if (!f)
		return -1;
	int i = 0;
	long x;
	while (i < n && fscanf(f, "%ld", &x) == 1)
		v[i++] = (int32_t) x;
	fclose(f);
	return i == n ? 0 : -1;
}

int store_save_calib(const int32_t* v, int n)
{
	char path[PATH_MAX_], text[256];
	int  len = 0;
	path_for("touch_calibration", path);
	for (int i = 0; i < n && len < (int) sizeof(text) - 16; i++)
		len += snprintf(text + len, sizeof(text) - (size_t) len, "%ld\n", (long) v[i]);
	return store_write(path, text, len);
}

// "last": the book's path on the first line, "open" on the second while it
// is being read.
int store_set_last(const char* book, bool open)
{
	char path[PATH_MAX_], text[PATH_MAX_ + 8];
	path_for("last", path);
	int n = snprintf(text, sizeof(text), "%s\n%s", book, open ? "open\n" : "");
	return store_write(path, text, n);
}

int store_get_last(char* out, int len, bool* open)
{
	char path[PATH_MAX_], second[16] = "";
	path_for("last", path);
	FILE* f = fopen(path, "r");
	if (!f)
		return -1;
	char* r = fgets(out, len, f);
	if (r && !fgets(second, sizeof(second), f))
		second[0] = 0;
	fclose(f);
	if (!r)
		return -1;
	out[strcspn(out, "\r\n")] = 0;
	if (open)
		*open = strncmp(second, "open", 4) == 0;
	return out[0] ? 0 : -1;
}
