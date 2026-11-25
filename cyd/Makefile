# Desktop builds of the CYD firmware's portable code (the board itself is
# built with PlatformIO, see platformio.ini):
#
#   make         build/cydsim: the whole app against a simulated screen
#   make test    unit tests + end-to-end simulator test

R      := ../reader
SRC    := src
CORE   := $(SRC)/app.c $(SRC)/cbook.c $(SRC)/pngs.c $(SRC)/render.c $(SRC)/store.c $(SRC)/tin.c $(SRC)/ui.c \
          $(SRC)/zipf.c $(R)/src/nav.c $(R)/src/zoom.c $(R)/src/gesture.c $(R)/src/font.c \
          $(R)/third_party/miniz/miniz.c

CPPFLAGS += -I$(SRC) -I$(R)/src -I$(R)/third_party/miniz -D_GNU_SOURCE \
            -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_STDIO -DMINIZ_NO_TIME
CFLAGS   ?= -O1 -g
CFLAGS   += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined
LDLIBS   += -lm

.PHONY: all test clean
all: build/cydsim

build/cydsim: $(CORE) sim/hal_sim.c $(wildcard $(SRC)/*.h)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS)

build/tests: $(filter-out $(SRC)/app.c,$(CORE)) sim/hal_sim.c test/tests.c $(wildcard $(SRC)/*.h)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -DSIM_NO_MAIN -I$(R)/third_party/stb $(CFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS)

test: build/tests build/cydsim
	./build/tests $(TEST_BOOK)
	./test/sim_test.sh

clean:
	rm -rf build
