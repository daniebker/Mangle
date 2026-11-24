// Desktop simulator backend: each frame is written as a PGM file and logged
// to stdout, so the reader can be tested without a Kindle.
//   MANGLE_SIM_SCREEN=1072x1448   screen size
//   MANGLE_SIM_DIR=frames         where to write frame_NNNN.pgm (unset = don't write)
#include <stdio.h>
#include <stdlib.h>

#include "display.h"

static Screen scr;
static int    frame;

int display_open(Screen* s)
{
	scr.width = 1072, scr.height = 1448;
	const char* e = getenv("MANGLE_SIM_SCREEN");
	if (e && sscanf(e, "%dx%d", &scr.width, &scr.height) != 2)
		return -1;
	scr.row_height = 32;
	scr.rows       = scr.height / scr.row_height;
	scr.cols       = scr.width / 16;
	*s             = scr;
	printf("SCREEN %dx%d rows=%d\n", scr.width, scr.height, scr.rows);
	fflush(stdout);
	return 0;
}

void display_close(void) {}

int display_frame(const uint8_t* buf, bool flash)
{
	frame++;
	const char* dir = getenv("MANGLE_SIM_DIR");
	char        path[4096] = "";
	if (dir) {
		snprintf(path, sizeof(path), "%s/frame_%04d.pgm", dir, frame);
		FILE* f = fopen(path, "wb");
		if (f) {
			fprintf(f, "P5\n%d %d\n255\n", scr.width, scr.height);
			fwrite(buf, 1, (size_t) scr.width * scr.height, f);
			fclose(f);
		}
	}
	printf("FRAME %d %s %s\n", frame, flash ? "full" : "partial", path);
	fflush(stdout);
	return 0;
}

int display_text(const char* const* lines, int n, bool flash)
{
	frame++;
	printf("TEXT %d %s\n", frame, flash ? "full" : "partial");
	for (int i = 0; i < n; i++)
		if (lines[i] && *lines[i])
			printf("  %2d| %s\n", i, lines[i]);
	fflush(stdout);
	return 0;
}
