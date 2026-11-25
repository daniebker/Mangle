// Command-line entry point, shared by the Kindle build and the desktop
// simulator (they differ only in their HAL).
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "hal.h"

// Provided by the HAL: open/close the device, and the quit flag a signal sets.
int  hal_open(void);
void hal_close(void);
void hal_quit(void);
void hal_release(void);    // async-signal-safe: let go of grabbed devices

static void on_quit(int sig)
{
	(void) sig;
	hal_quit();
}

static void on_crash(int sig)
{
	// The kernel drops input grabs when we die anyway; releasing them first is
	// belt and braces. The Kindle launch script resumes the Kindle UI.
	hal_release();
	signal(sig, SIG_DFL);
	raise(sig);
}

static void setup_signals(void)
{
	struct sigaction sa = { 0 };
	sa.sa_handler       = on_quit;    // no SA_RESTART: let poll() return EINTR
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	int crash[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL };
	for (size_t i = 0; i < sizeof(crash) / sizeof(*crash); i++)
		signal(crash[i], on_crash);
}

static void usage(const char* argv0)
{
	fprintf(stderr,
		"usage: %s [options] [BOOK.zip]\n"
		"  -l DIR   library folder: show the book shelf (and return to it after reading)\n"
		"  -s DIR   folder for settings, positions and statistics (default: ./state)\n"
		"  -r       reopen the last book read\n"
		"  -R       reopen the last book only if it was open when the program stopped\n"
		"  -t SEC   sleep after SEC idle seconds instead of the Sleep setting's minutes\n"
		"  -f N     e-ink: full (flashing) refresh every N page turns, 0 = never (default 8)\n"
		"  -O       panel mode: no page preview unless the Settings turn it on\n"
		"  -v       log every image shown\n",
		argv0);
}

int main(int argc, char** argv)
{
	AppConfig cfg = {
		.state       = "state",
		.flash_every = 8,
		.prefetch    = true,
		.defaults    = { .overview = true, .sleep_min = 2, .brightness = 50, .rotation = 0 },
	};
	int c;
	while ((c = getopt(argc, argv, "l:s:rRf:t:Ovh")) != -1) {
		switch (c) {
			case 'l': cfg.library = optarg; break;
			case 's': cfg.state = optarg; break;
			case 'r': cfg.resume = RESUME_LAST; break;
			case 'R': cfg.resume = RESUME_IF_OPEN; break;
			case 'f': cfg.flash_every = atoi(optarg); break;
			case 'O': cfg.defaults.overview = false; break;
			case 't': cfg.idle_ms = atoi(optarg) * 1000; break;
			case 'v': cfg.verbose = true; break;
			default: usage(argv[0]); return c == 'h' ? 0 : 2;
		}
	}
	cfg.book = optind < argc ? argv[optind] : NULL;
	if (!cfg.book && !cfg.library && cfg.resume == RESUME_NONE) {
		usage(argv[0]);
		return 2;
	}
	setup_signals();
	if (hal_open() != 0)
		return 1;
	app_run(&cfg);
	hal_close();
	return 0;
}
