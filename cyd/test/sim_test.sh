#!/bin/sh
# End-to-end test of the CYD firmware's app code with the desktop simulator.
# Builds sample books with the preprocessor ($PYTHON needs its requirements),
# or uses $MANGLE_TEST_ZIP.
set -eu
PY=${PYTHON:-python3}
cd "$(dirname "$0")/.."
BIN=./build/cydsim
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/lib" "$TMP/frames"

if [ -n "${MANGLE_TEST_ZIP:-}" ]; then
	cp "$MANGLE_TEST_ZIP" "$TMP/lib/book.zip"
else
	"$PY" ../preprocessor/tests/sample.py "$TMP/in.cbz"
	(cd ../preprocessor && "$PY" prep.py "$TMP/in.cbz" -o "$TMP/lib/book.zip" --device cyd -j 2) \
		2>"$TMP/prep.log" || { cat "$TMP/prep.log"; echo "FAIL: prep.py could not build the test book"; exit 1; }
fi
"$PY" test/make_pngs.py "$TMP/pngs.zip"
./build/tests "$TMP/pngs.zip" "$TMP/lib/book.zip" >"$TMP/unit" 2>&1 || { cat "$TMP/unit"; echo "FAIL: book tests"; exit 1; }

export MANGLE_SIM_LIBRARY="$TMP/lib" MANGLE_SIM_DIR="$TMP/frames"
fail() { echo "FAIL: $*"; cat "$TMP/err"; exit 1; }

# Portrait 320x480: left third x<106 = forward (RTL), right third = back.
# Library: tap the first cover -> p001 -> p002 overview -> panel 1 -> panel 2 ->
# back -> hold a side (zoom 2x) -> drag (pan) -> pinch in (fitted again) ->
# double tap centre (page mode) -> swipe right (RTL: forward, p003) ->
# menu -> Library.
printf '%s\n' 'tap 80 150' 'tap 10 300' 'tap 10 300' 'tap 10 300' 'tap 310 300' 'long 20 300' 'drag 100 200 40 30' \
	'pinch 160 240 0.5' 'tap 160 300' 'sleep 50' 'tap 160 300' 'sleep 400' 'drag 50 300 120 5' 'tap 160 10' 'tap 20 10' 'quit' |
	$BIN >"$TMP/out" 2>"$TMP/err" || fail "simulator exited with an error"
grep 'mangle: show' "$TMP/err" | sed 's/.*show //' >"$TMP/shown"
cat >"$TMP/want" <<W
p001.png page=0 panel=0 mode=panel
p002.png page=1 panel=-1 mode=panel
p002.png page=1 panel=0 mode=panel
p002.png page=1 panel=1 mode=panel
p002.png page=1 panel=0 mode=panel
p002.png page=1 panel=0 mode=panel zoom=2.00
p002.png page=1 panel=0 mode=panel zoom=2.00
p002.png page=1 panel=0 mode=panel
p002.png page=1 panel=0 mode=page
p003.png page=2 panel=0 mode=page
W
diff "$TMP/want" "$TMP/shown" >/dev/null || { diff "$TMP/want" "$TMP/shown"; fail "unexpected navigation"; }
grep -q 'zoom=2.00' "$TMP/err" || fail "no zoom"
[ "$(cat "$TMP/lib/.mangle/book.zip.pos")" = "2 0 0 12" ] || fail "position not saved: $(cat "$TMP/lib/.mangle/book.zip.pos")"
[ -f "$TMP/lib/.mangle/book.zip.thm" ] || fail "no cover thumbnail cached"
# Frames are real pictures (many gray levels), not blank screens.
"$PY" -c "
import sys, glob
fs = sorted(glob.glob(sys.argv[1] + '/*.pgm'))
assert len(fs) >= 10, len(fs)
rich = sum(1 for f in fs if len(set(open(f, 'rb').read()[15:])) > 8)
assert rich >= 8, rich
" "$TMP/frames" || fail "frames look wrong"

# The menu's Library entry cleared "last"; reopening goes to the library first.
# Open the book again: it resumes at p003 in page mode. Hold the centre: back
# to the library.
rm -f "$TMP/frames"/*
printf '%s\n' 'tap 80 150' 'long 160 300' 'quit' | $BIN >"$TMP/out" 2>"$TMP/err" || fail "simulator exited with an error"
grep 'mangle: show' "$TMP/err" | sed 's/.*show //' >"$TMP/shown"
[ "$(cat "$TMP/shown")" = "p003.png page=2 panel=0 mode=page" ] || { cat "$TMP/shown"; fail "did not resume"; }

# Quitting while reading (power off) keeps the book: the next start opens it.
printf '%s\n' 'tap 80 150' 'tap 10 300' 'quit' | $BIN >"$TMP/out" 2>"$TMP/err"
printf '%s\n' 'quit' | $BIN >"$TMP/out" 2>"$TMP/err"
grep -q 'show p004.png page=3' "$TMP/err" || fail "did not reopen the last book at start-up"

# Settings: rotate to landscape (480x320) and read a page there.
printf '%s\n' 'tap 300 10' 'tap 100 200' 'tap 100 20' 'tap 80 150' 'quit' | MANGLE_SIM_STATE="$TMP/state2" $BIN \
	>"$TMP/out" 2>"$TMP/err" || fail "simulator exited with an error"
grep -q 'rotation=1' "$TMP/state2/settings" || fail "rotation not saved"
last=$(ls "$TMP/frames"/*.pgm | tail -1)
head -c 15 "$last" | grep -q '480 320' || fail "last frame is not landscape"

# A broken book shows a message instead of crashing.
printf 'not a zip' >"$TMP/lib/0broken.zip"
printf '%s\n' 'tap 80 150' 'tap 100 100' 'quit' | MANGLE_SIM_STATE="$TMP/state3" $BIN >"$TMP/out" 2>"$TMP/err" ||
	fail "simulator exited with an error"
grep -q 'cannot open ZIP\|0broken' "$TMP/err" || fail "no error for the broken book"

echo "cyd sim test: OK"
