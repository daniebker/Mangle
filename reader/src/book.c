#include "book.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#define ERR(...)                                                                                                       \
	do {                                                                                                           \
		if (err)                                                                                               \
			snprintf(err, errlen, __VA_ARGS__);                                                            \
	} while (0)

static char* dupstr(const char* s)
{
	size_t n = strlen(s) + 1;
	char*  d = malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

static void free_pages(Book* b)
{
	for (int i = 0; i < b->npages; i++) {
		free(b->pages[i].image);
		for (int j = 0; j < b->pages[i].npanels; j++)
			free(b->pages[i].panels[j].image);
		free(b->pages[i].panels);
	}
	free(b->pages);
	b->pages  = NULL;
	b->npages = 0;
}

int book_parse_manifest(Book* b, const char* json, size_t len, char* err, size_t errlen)
{
	cJSON* root = cJSON_ParseWithLength(json, len);
	if (!root) {
		ERR("manifest.json is not valid JSON");
		return -1;
	}
	int rc = -1;

	cJSON* ver = cJSON_GetObjectItemCaseSensitive(root, "version");
	if (!cJSON_IsNumber(ver) || (ver->valueint != 1 && ver->valueint != 2)) {
		ERR("unsupported manifest version");
		goto out;
	}
	cJSON* title = cJSON_GetObjectItemCaseSensitive(root, "title");
	b->title     = dupstr(cJSON_IsString(title) ? title->valuestring : "");
	cJSON* dir   = cJSON_GetObjectItemCaseSensitive(root, "direction");
	b->rtl       = !(cJSON_IsString(dir) && strcmp(dir->valuestring, "ltr") == 0);
	cJSON* scr   = cJSON_GetObjectItemCaseSensitive(root, "screen");
	if (cJSON_IsArray(scr) && cJSON_GetArraySize(scr) == 2) {
		b->screen_w = cJSON_GetArrayItem(scr, 0)->valueint;
		b->screen_h = cJSON_GetArrayItem(scr, 1)->valueint;
	}

	cJSON* pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
	int    n     = cJSON_GetArraySize(pages);
	if (!cJSON_IsArray(pages) || n == 0) {
		ERR("manifest has no pages");
		goto out;
	}
	b->pages = calloc((size_t) n, sizeof(Page));
	if (!b->pages)
		goto out;
	cJSON* p;
	cJSON_ArrayForEach(p, pages)
	{
		Page*  pg  = &b->pages[b->npages++];
		cJSON* img = cJSON_GetObjectItemCaseSensitive(p, "image");
		if (!cJSON_IsString(img)) {
			ERR("page %d has no image", b->npages);
			goto out;
		}
		pg->image     = dupstr(img->valuestring);
		cJSON* panels = cJSON_GetObjectItemCaseSensitive(p, "panels");
		int    np     = cJSON_IsArray(panels) ? cJSON_GetArraySize(panels) : 0;
		if (np) {
			pg->panels = calloc((size_t) np, sizeof(Panel));
			cJSON* q;
			cJSON_ArrayForEach(q, panels)
			{
				cJSON* qi = cJSON_GetObjectItemCaseSensitive(q, "image");
				cJSON* qr = cJSON_GetObjectItemCaseSensitive(q, "rect");
				Panel* pn = &pg->panels[pg->npanels];
				if (cJSON_IsString(qi)) {
					pn->image = dupstr(qi->valuestring);
					pg->npanels++;
				} else if (cJSON_IsArray(qr) && cJSON_GetArraySize(qr) == 4) {
					pn->x = cJSON_GetArrayItem(qr, 0)->valueint;
					pn->y = cJSON_GetArrayItem(qr, 1)->valueint;
					pn->w = cJSON_GetArrayItem(qr, 2)->valueint;
					pn->h = cJSON_GetArrayItem(qr, 3)->valueint;
					if (pn->w > 0 && pn->h > 0)
						pg->npanels++;
				}
			}
		}
	}
	rc = 0;
out:
	cJSON_Delete(root);
	if (rc)
		free_pages(b);
	return rc;
}

Book* book_open(const char* path, char* err, size_t errlen)
{
	Book* b = calloc(1, sizeof(Book));
	if (!b)
		return NULL;
	pthread_mutex_init(&b->lock, NULL);
	b->path = dupstr(path);
	if (!mz_zip_reader_init_file(&b->zip, path, 0)) {
		ERR("cannot open ZIP: %s", mz_zip_get_error_string(mz_zip_get_last_error(&b->zip)));
		free(b->path);
		free(b);
		return NULL;
	}
	size_t len;
	char*  json = mz_zip_reader_extract_file_to_heap(&b->zip, "manifest.json", &len, 0);
	if (!json) {
		ERR("no manifest.json (was this ZIP made by prep.py?)");
		book_close(b);
		return NULL;
	}
	int rc = book_parse_manifest(b, json, len, err, errlen);
	mz_free(json);
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
	mz_zip_reader_end(&b->zip);
	free_pages(b);
	free(b->title);
	free(b->path);
	pthread_mutex_destroy(&b->lock);
	free(b);
}

int book_load_image(Book* b, const char* name, Image* img)
{
	img->px = NULL;
	img->w = img->h = 0;
	size_t len      = 0;
	pthread_mutex_lock(&b->lock);
	void* data = mz_zip_reader_extract_file_to_heap(&b->zip, name, &len, 0);
	pthread_mutex_unlock(&b->lock);
	if (!data) {
		fprintf(stderr, "mangle: %s missing from archive\n", name);
		return -1;
	}
	int ch;
	img->px = stbi_load_from_memory(data, (int) len, &img->w, &img->h, &ch, 1);
	mz_free(data);
	if (!img->px) {
		fprintf(stderr, "mangle: cannot decode %s: %s\n", name, stbi_failure_reason());
		return -1;
	}
	return 0;
}

void image_free(Image* img)
{
	stbi_image_free(img->px);
	img->px = NULL;
}
