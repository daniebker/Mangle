# Mangle for Kindle. The app itself is the shared code in ../common; this
# folder only has the Kindle's HAL (FBInk display, evdev input) and the KUAL
# packaging.
#
#   make test            Kindle-only unit tests (touch decoding)
#   make kindlehf        Kindle, firmware 5.16.3 and newer (hard-float)
#   make kindlepw2       Kindle, older 5.x firmware (soft-float, PW2 and later)
#   make kindle          same as kindlehf
#   make package         KUAL extension folder + zip in dist/ (after a Kindle build)
#
# ../build.sh wraps all of this (finds the toolchain, fetches FBInk, packages).
# The desktop simulator and the app's tests are in ../common (make -C ../common test).
#
# Kindle builds need NiLuJe's koxtoolchain on PATH (x-tools/<target>/bin)
# and FBInk in third_party/FBInk (run ./fetch_fbink.sh once).

COMMON    := ../common
CS        := $(COMMON)/src
TP        := third_party
FBINK_DIR := $(TP)/FBInk

# The shared app (common/src without other devices' HAL halves).
LIB := $(CS)/app.c $(CS)/book.c $(CS)/cache.c $(CS)/cli.c $(CS)/fb.c $(CS)/font.c $(CS)/gesture.c $(CS)/nav.c \
       $(CS)/pngs.c $(CS)/posix.c $(CS)/render.c $(CS)/stats.c $(CS)/stats_view.c $(CS)/store.c $(CS)/tin.c $(CS)/ui.c \
       $(CS)/zipf.c $(CS)/zoom.c $(COMMON)/third_party/miniz/miniz.c

CPPFLAGS += -Isrc -I$(CS) -I$(COMMON)/third_party/miniz -D_GNU_SOURCE \
            -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_STDIO -DMINIZ_NO_TIME
CFLAGS   ?= -O2 -g
CFLAGS   += -std=gnu11 -Wall -Wextra -Wno-unused-parameter
# -lrt: clock_gettime lives there on the older glibc of the kindlepw2 target.
LDLIBS   += -lpthread -lm -lrt

.PHONY: all test kindle kindlehf kindlepw2 package clean

all: test

# ---- tests (desktop) -------------------------------------------------------------
build/test/tests: src/touch.c $(CS)/tin.c tests/tests.c $(wildcard src/*.h) $(CS)/tin.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -o $@ $(filter %.c,$^) $(LDLIBS)

test: build/test/tests
	./build/test/tests

# ---- Kindle ------------------------------------------------------------------
kindle: kindlehf

kindlehf:
	$(MAKE) build/$@/mangle TARGET=$@ CROSS=arm-kindlehf-linux-gnueabihf

kindlepw2:
	$(MAKE) build/$@/mangle TARGET=$@ CROSS=arm-kindlepw2-linux-gnueabi

ifdef CROSS
KCC     := $(CROSS)-gcc
KSTRIP  := $(CROSS)-strip
FBINK_A := build/$(TARGET)/fbink/libfbink.a

# MINIMAL + IMAGE (print_raw_data); text is drawn by the app itself.
$(FBINK_A):
	@test -f $(FBINK_DIR)/fbink.h || { echo "FBInk missing: run ./fetch_fbink.sh"; exit 1; }
	$(MAKE) -C $(FBINK_DIR) clean
	@# libi2c is only used on Kobo; mark it built so FBInk doesn't need the submodule.
	touch $(FBINK_DIR)/libi2c.built
	$(MAKE) -C $(FBINK_DIR) staticlib KINDLE=1 CROSS_TC=$(CROSS) MINIMAL=1 BITMAP=1 IMAGE=1
	@mkdir -p $(@D)
	cp $(FBINK_DIR)/Release/libfbink.a $@

build/$(TARGET)/mangle: $(LIB) src/hal_kindle.c src/touch.c $(FBINK_A) $(wildcard src/*.h) $(wildcard $(CS)/*.h)
	@mkdir -p $(@D)
	$(KCC) $(CPPFLAGS) -I$(FBINK_DIR) $(CFLAGS) -o $@ $(filter %.c,$^) $(FBINK_A) $(LDLIBS)
	$(KSTRIP) --strip-unneeded $@
	@echo "built $@"
endif

# ---- packaging ---------------------------------------------------------------
# Copies a Kindle binary into the KUAL extension and zips it:
#   make package TARGET=kindlehf   -> dist/mangle-kual-kindlehf.zip
# Without TARGET, uses the newest of the kindlehf/kindlepw2 builds.
package:
	@if [ -n "$(TARGET)" ]; then bin=build/$(TARGET)/mangle; \
	else bin=$$(ls -t build/kindlehf/mangle build/kindlepw2/mangle 2>/dev/null | head -1); fi; \
	test -f "$$bin" || { echo "build a Kindle target first"; exit 1; }; \
	name=mangle-kual-$$(basename $$(dirname $$bin)); \
	rm -rf dist/mangle && mkdir -p dist && cp -r ../kual/mangle dist/mangle && \
	cp "$$bin" dist/mangle/bin/mangle && chmod +x dist/mangle/bin/* && \
	(cd dist && rm -f $$name.zip && zip -qr $$name.zip mangle) && \
	echo "dist/$$name.zip (binary: $$bin)"

clean:
	rm -rf build dist
