// Simulator front light: kept in memory, changes logged as "LIGHT n".
//   MANGLE_SIM_LIGHT=LEVEL/MAX   starting level and maximum (default 10/24);
//                                "none" = no front light
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "light.h"

static int level, max_level;

int light_open(void)
{
	level = 10, max_level = 24;
	const char* e = getenv("MANGLE_SIM_LIGHT");
	if (e && (!strcmp(e, "none") || sscanf(e, "%d/%d", &level, &max_level) != 2 || max_level < 0))
		level = -1, max_level = 0;
	return max_level;
}

int light_get(void)
{
	return max_level > 0 ? level : -1;
}

int light_set(int v)
{
	if (max_level <= 0)
		return -1;
	level = v < 0 ? 0 : v > max_level ? max_level : v;
	printf("LIGHT %d\n", level);
	fflush(stdout);
	return 0;
}
