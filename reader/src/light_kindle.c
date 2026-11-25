// Kindle front light. The Kindle's power daemon (powerd) owns the light and
// keeps running while the Kindle UI is paused, so we ask it through lipc:
// the level is then remembered and restored after sleep like a normal change.
// If lipc isn't there, fall back to /sys/class/backlight/*.
//   MANGLE_LIGHT_MAX=N   highest level, if powerd doesn't report it (default 24)
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "light.h"

static enum { NONE, LIPC, SYSFS } kind;
static int  max_level;
static char sysfs[512];    // .../brightness

// Run a command and read one integer from its output. -1 on failure.
static int read_cmd_int(const char* cmd)
{
	FILE* p = popen(cmd, "r");
	if (!p)
		return -1;
	int v = -1;
	if (fscanf(p, "%d", &v) != 1)
		v = -1;
	if (pclose(p) != 0)
		v = -1;
	return v;
}

static int read_file_int(const char* path)
{
	FILE* f = fopen(path, "r");
	if (!f)
		return -1;
	int v = -1;
	if (fscanf(f, "%d", &v) != 1)
		v = -1;
	fclose(f);
	return v;
}

static int open_sysfs(void)
{
	const char* base = "/sys/class/backlight";
	DIR*        d    = opendir(base);
	if (!d)
		return 0;
	struct dirent* e;
	int            max = 0;
	while (!max && (e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		char path[512];
		snprintf(path, sizeof(path), "%s/%s/max_brightness", base, e->d_name);
		int m = read_file_int(path);
		if (m <= 0)
			continue;
		snprintf(sysfs, sizeof(sysfs), "%s/%s/brightness", base, e->d_name);
		max = m;
	}
	closedir(d);
	return max;
}

int light_open(void)
{
	kind      = NONE;
	max_level = 0;
	if (read_cmd_int("lipc-get-prop com.lab126.powerd flIntensity 2>/dev/null") >= 0) {
		kind      = LIPC;
		max_level = read_cmd_int("lipc-get-prop com.lab126.powerd flMaxIntensity 2>/dev/null");
		const char* e = getenv("MANGLE_LIGHT_MAX");
		if (e && atoi(e) > 0)
			max_level = atoi(e);
		if (max_level <= 0)
			max_level = 24;
	} else if ((max_level = open_sysfs()) > 0) {
		kind = SYSFS;
	}
	if (kind == NONE)
		fprintf(stderr, "mangle: no front light control found\n");
	else
		fprintf(stderr, "mangle: front light via %s, 0..%d\n", kind == LIPC ? "powerd" : sysfs, max_level);
	return max_level;
}

int light_get(void)
{
	switch (kind) {
		case LIPC:
			return read_cmd_int("lipc-get-prop com.lab126.powerd flIntensity 2>/dev/null");
		case SYSFS:
			return read_file_int(sysfs);
		default:
			return -1;
	}
}

int light_set(int level)
{
	if (kind == NONE)
		return -1;
	if (level < 0)
		level = 0;
	if (level > max_level)
		level = max_level;
	if (kind == LIPC) {
		char cmd[128];
		snprintf(cmd, sizeof(cmd), "lipc-set-prop com.lab126.powerd flIntensity %d >/dev/null 2>&1", level);
		return system(cmd) == 0 ? 0 : -1;
	}
	FILE* f = fopen(sysfs, "w");
	if (!f)
		return -1;
	int rc = fprintf(f, "%d", level) < 0 ? -1 : 0;
	if (fclose(f) != 0)
		rc = -1;
	return rc;
}
