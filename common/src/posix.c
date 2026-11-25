// hal_ms and hal_log for the Linux builds (Kindle and desktop simulator).
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "hal.h"

int64_t hal_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void hal_log(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("mangle: ", stderr);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
