# The code every Mangle build shares (common/src), built and tested on the
# desktop:
#
#   make         build/mangle: the app with the simulator HAL (sim/hal_sim.c)
#   make test    unit tests (ASan/UBSan) + end-to-end simulator tests, as a
#                Kindle and as both Cheap Yellow Display boards
#
# The device builds compile the same sources: ../reader/Makefile (Kindle) and
# ../cyd/platformio.ini (CYD firmware).

SRC    := src
TP     := third_party

# Everything but the entry point and the Linux HAL helpers (fb.c, posix.c).
LIB    := $(SRC)/app.c $(SRC)/book.c $(SRC)/cache.c $(SRC)/font.c $(SRC)/gesture.c $(SRC)/nav.c \
          $(SRC)/pngs.c $(SRC)/render.c $(SRC)/stats.c $(SRC)/stats_view.c $(SRC)/store.c $(SRC)/tin.c \
          $(SRC)/ui.c $(SRC)/zipf.c $(SRC)/zoom.c $(TP)/miniz/miniz.c
SIM    := $(SRC)/cli.c $(SRC)/fb.c $(SRC)/posix.c sim/hal_sim.c

CPPFLAGS += -I$(SRC) -I$(TP)/miniz -D_GNU_SOURCE -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_STDIO -DMINIZ_NO_TIME
CFLAGS   ?= -O1 -g
CFLAGS   += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined
LDLIBS   += -lpthread -lm

.PHONY: all test clean
all: build/mangle

build/mangle: $(LIB) $(SIM) $(wildcard $(SRC)/*.h)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS)

build/tests: $(LIB) $(SRC)/fb.c $(SRC)/posix.c sim/hal_sim.c tests/tests.c $(wildcard $(SRC)/*.h)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(TP)/stb $(CFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS)

test: build/tests build/mangle
	./tests/run_tests.sh

clean:
	rm -rf build
