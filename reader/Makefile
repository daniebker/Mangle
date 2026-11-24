# Mangle reader.
#
#   make                 desktop simulator build: build/sim/mangle (PGM frames, stdin input)
#   make test            unit tests (desktop)
#   make kindlehf        Kindle, firmware 5.16.3 and newer (hard-float)
#   make kindlepw2       Kindle, older 5.x firmware (soft-float, PW2 and later)
#   make kindle          same as kindlehf
#   make package         KUAL extension folder + zip in dist/ (after a Kindle build)
#
# ../build.sh wraps all of this (finds the toolchain, fetches FBInk, packages).
#
# Kindle builds need NiLuJe's koxtoolchain on PATH (x-tools/<target>/bin)
# and FBInk in third_party/FBInk (run ./fetch_fbink.sh once).

SRC       := src
TP        := third_party
FBINK_DIR := $(TP)/FBInk

CORE := $(SRC)/book.c $(SRC)/nav.c $(SRC)/gesture.c $(SRC)/cache.c $(SRC)/compose.c \
        $(SRC)/state.c $(SRC)/library.c $(SRC)/touch.c $(SRC)/input_common.c \
        $(TP)/miniz/miniz.c $(TP)/cjson/cJSON.c

CPPFLAGS += -I$(SRC) -I$(TP)/miniz -I$(TP)/cjson -I$(TP)/stb -D_GNU_SOURCE \
            -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_ARCHIVE_WRITING_APIS
CFLAGS   ?= -O2 -g
CFLAGS   += -std=gnu11 -Wall -Wextra -Wno-unused-parameter
LDLIBS   += -lpthread -lm


.PHONY: all sim test kindle kindlehf kindlepw2 package clean

all: sim

# ---- desktop simulator -------------------------------------------------------
sim: build/sim/mangle

build/sim/mangle: $(CORE) $(SRC)/main.c $(SRC)/display_sim.c $(SRC)/input_sim.c $(wildcard $(SRC)/*.h)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(filter %.c,$^) $(LDLIBS)

# ---- tests -------------------------------------------------------------------
build/sim/tests: $(CORE) $(SRC)/display_sim.c $(SRC)/input_sim.c tests/tests.c $(wildcard $(SRC)/*.h)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -o $@ $(filter %.c,$^) $(LDLIBS)

test: build/sim/tests build/sim/mangle
	./build/sim/tests
	./tests/sim_test.sh

# ---- Kindle ------------------------------------------------------------------
# FBINK_TARGET: FBInk's own platform flags. LEGACY=1 is for pre-Touch devices.
kindle: kindlehf

kindlehf:
	$(MAKE) build/$@/mangle TARGET=$@ CROSS=arm-kindlehf-linux-gnueabihf

kindlepw2:
	$(MAKE) build/$@/mangle TARGET=$@ CROSS=arm-kindlepw2-linux-gnueabi

ifdef CROSS
KCC     := $(CROSS)-gcc
KSTRIP  := $(CROSS)-strip
FBINK_A := build/$(TARGET)/fbink/libfbink.a

# MINIMAL + BITMAP (fbink_print with the built-in font) + IMAGE (print_raw_data).
$(FBINK_A):
	@test -f $(FBINK_DIR)/fbink.h || { echo "FBInk missing: run ./fetch_fbink.sh"; exit 1; }
	$(MAKE) -C $(FBINK_DIR) clean
	@# libi2c is only used on Kobo; mark it built so FBInk doesn't need the submodule.
	touch $(FBINK_DIR)/libi2c.built
	$(MAKE) -C $(FBINK_DIR) staticlib KINDLE=1 CROSS_TC=$(CROSS) MINIMAL=1 BITMAP=1 IMAGE=1
	@mkdir -p $(@D)
	cp $(FBINK_DIR)/Release/libfbink.a $@

build/$(TARGET)/mangle: $(CORE) $(SRC)/main.c $(SRC)/display_fbink.c $(SRC)/input_evdev.c $(FBINK_A) $(wildcard $(SRC)/*.h)
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
