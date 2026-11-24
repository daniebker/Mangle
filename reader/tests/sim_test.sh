#!/bin/sh
# End-to-end test of the reader using the desktop simulator backend.
# Needs a book ZIP: $MANGLE_TEST_ZIP, or one is generated with the
# preprocessor (needs its Python requirements).
set -eu
PY=${PYTHON:-python3}   # interpreter with the preprocessor requirements
cd "$(dirname "$0")/.."
BIN=./build/sim/mangle
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

ZIP=${MANGLE_TEST_ZIP:-}
if [ -z "$ZIP" ]; then
	"$PY" ../preprocessor/tests/sample.py "$TMP/in.cbz"
	(cd ../preprocessor && "$PY" prep.py "$TMP/in.cbz" -o "$TMP/book.zip" --screen 600x800 -j 2) 2>"$TMP/prep.log" || {
		cat "$TMP/prep.log"
		echo "FAIL: prep.py could not build the test book (is \$PYTHON set to an interpreter with preprocessor/requirements.txt?)"
		exit 1
	}
	ZIP=$TMP/book.zip
fi
export MANGLE_SIM_SCREEN=600x800

# Prefetch cache against the real book (the unit-test binary skips it without one).
./build/sim/tests "$ZIP" >/dev/null 2>"$TMP/err" || { cat "$TMP/err"; echo "FAIL: cache test"; exit 1; }
convert_check() { python3 -c "import sys;d=open(sys.argv[1],'rb').read();print(1 if len(set(d[20:]))>8 else 0)" "$1"; }
fail() { echo "FAIL: $*"; cat "$TMP/err"; exit 1; }

# Screen 600 wide: left third x<200 = forward (RTL), right third = back, centre = toggle.
# Cover (p001, whole page) -> p002 overview -> p002_1 -> p002_2 -> back to p002_1 ->
# double-tap centre (page mode, p002) -> forward (p003) ->
# single centre tap = forward (p004) -> long-press centre = close.
printf 'tap 10 400\ntap 10 400\ntap 10 400\ntap 590 400\ntap 300 400\nsleep 50\ntap 300 400\nsleep 400\ntap 10 400\ntap 300 400\nsleep 400\nlong 300 400\n' |
	$BIN -v -s "$TMP/state" "$ZIP" >"$TMP/out" 2>"$TMP/err"
grep 'mangle: show' "$TMP/err" | sed 's/.*show //' >"$TMP/shown"
cat >"$TMP/want" <<W
p001.png page=0 panel=0 mode=panel
p002.png page=1 panel=-1 mode=panel
p002.png page=1 panel=0 mode=panel
p002.png page=1 panel=1 mode=panel
p002.png page=1 panel=0 mode=panel
p002.png page=1 panel=0 mode=page
p003.png page=2 panel=0 mode=page
p004.png page=3 panel=0 mode=page
W
diff "$TMP/want" "$TMP/shown" || fail "unexpected navigation"
# First frame and the mode toggle are full refreshes; turns are partial.
[ "$(sed -n 2p "$TMP/out" | cut -d' ' -f3)" = full ] || fail "first frame not a full refresh"
[ "$(sed -n 7p "$TMP/out" | cut -d' ' -f3)" = full ] || fail "toggle not a full refresh"
[ "$(sed -n 3p "$TMP/out" | cut -d' ' -f3)" = partial ] || fail "turn not partial"

# Back from the first panel returns to the overview, then the previous page.
printf 'tap 10 400\ntap 10 400\ntap 590 400\ntap 590 400\nquit\n' |
	$BIN -v -s "$TMP/state_ov" "$ZIP" >/dev/null 2>"$TMP/err"
grep 'mangle: show' "$TMP/err" | sed 's/.*show \([^ ]*\).*/\1/' | tr '\n' ' ' >"$TMP/shown"
[ "$(cat "$TMP/shown")" = "p001.png p002.png p002.png p002.png p001.png " ] || fail "overview going back: $(cat "$TMP/shown")"

# -O: no overview, straight to the first panel.
printf 'tap 10 400\nquit\n' | $BIN -O -v -s "$TMP/state_noov" "$ZIP" >/dev/null 2>"$TMP/err"
grep -q 'show p002.png page=1 panel=0' "$TMP/err" || fail "-O should skip the overview"

# Resume: reopening shows the saved position.
echo quit | $BIN -v -s "$TMP/state" "$ZIP" >/dev/null 2>"$TMP/err"
grep -q 'show p004.png page=3 panel=0 mode=page' "$TMP/err" || fail "resume"
echo quit | $BIN -v -s "$TMP/state" -r >/dev/null 2>"$TMP/err"
grep -q 'show p004.png' "$TMP/err" || fail "resume last (-r)"

# Every 3rd turn flashes with -f 3.
printf 'key 109\nkey 109\nkey 109\nkey 104\nquit\n' | $BIN -f 3 -s "$TMP/state2" "$ZIP" >"$TMP/out" 2>"$TMP/err"
[ "$(grep -c '^FRAME.* full' "$TMP/out")" = 2 ] || fail "periodic full refresh"

# Library: a shelf of covers (3 columns at 600 wide). Tapping a cover opens it
# (natural order: "Vol 2" is first), long-press centre returns, the top row exits.
mkdir -p "$TMP/lib"
cp "$ZIP" "$TMP/lib/Vol 2.zip"
cp "$ZIP" "$TMP/lib/Vol 10.zip"
printf 'tap 100 150\nlong 300 400\ntap 300 10\n' | MANGLE_SIM_DIR="$TMP" $BIN -v -l "$TMP/lib" -s "$TMP/state3" >"$TMP/out" 2>"$TMP/err"
grep -q 'show p001.png' "$TMP/err" || fail "library open"
! grep -q '^TEXT' "$TMP/out" || fail "library should draw a shelf, not text"
[ "$(grep -c '^FRAME.* full' "$TMP/out")" -ge 2 ] || fail "library not shown again after closing book"
[ -f "$TMP/state3/Vol 2.zip.pos" ] || fail "natural order: first cover should be Vol 2"
# Cover of the first book is drawn: the shelf frame is not blank.
[ "$(convert_check "$TMP/frame_0001.pgm")" = 1 ] || fail "shelf frame is blank"

# Bad input is reported on screen, not a crash.
echo "not a zip" >"$TMP/lib/bad.zip"
printf 'tap 1 1\n' | $BIN -s "$TMP/state" "$TMP/lib/bad.zip" >"$TMP/out" 2>&1 || fail "bad zip crashed"
grep -q 'Cannot open book' "$TMP/out" || fail "bad zip message"


# Sleep: after -t idle seconds the cover is drawn and the device suspended; the page is redrawn on wake.
printf 'tap 10 400\nsleep 2500\nquit\n' | $BIN -t 1 -v -s "$TMP/state_sl" "$ZIP" >"$TMP/out" 2>"$TMP/err"
grep -q 'mangle: sleep' "$TMP/err" || fail "no sleep after idle"
grep -q '^SUSPEND' "$TMP/out" || fail "device not suspended"
[ "$(grep -c 'mangle: show' "$TMP/err")" = 3 ] || fail "wake should redraw the page"
# Settings: the menu's Settings entry toggles sleep off and persists it.
printf 'tap 300 5\ntap 200 5\ntap 300 80\ntap 10 20\nsleep 2500\nquit\n' | $BIN -t 1 -v -s "$TMP/state_st" "$ZIP" >/dev/null 2>"$TMP/err"
grep -q 'sleep=0' "$TMP/state_st/settings" || fail "settings not saved"
grep -q 'mangle: sleep' "$TMP/err" && fail "slept with sleep turned off"
echo "sim tests passed"
