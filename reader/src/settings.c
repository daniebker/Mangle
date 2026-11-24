#include "settings.h"

#include <stdio.h>
#include <string.h>

void settings_load(const char* dir, Settings* s)
{
	s->sleep = true;
	char path[4096];
	snprintf(path, sizeof(path), "%s/settings", dir);
	FILE* f = fopen(path, "r");
	if (!f)
		return;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		int v;
		if (sscanf(line, "sleep=%d", &v) == 1)
			s->sleep = v != 0;
	}
	fclose(f);
}

int settings_save(const char* dir, const Settings* s)
{
	char path[4096], tmp[4100];
	snprintf(path, sizeof(path), "%s/settings", dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "w");
	if (!f)
		return -1;
	fprintf(f, "sleep=%d\n", s->sleep ? 1 : 0);
	if (fclose(f) != 0)
		return -1;
	return rename(tmp, path);
}
