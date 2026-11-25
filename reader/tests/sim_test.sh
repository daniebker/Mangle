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
# Settings: turning the page preview off persists, takes effect at once, and is saved.
printf 'tap 300 5\ntap 200 5\ntap 300 150\ntap 10 20\ntap 10 400\nquit\n' | $BIN -v -s "$TMP/state_pv" "$ZIP" >/dev/null 2>"$TMP/err"
grep -q 'overview=0' "$TMP/state_pv/settings" || fail "preview setting not saved"
grep -q 'show p002.png page=1 panel=0 mode=panel' "$TMP/err" || fail "preview should be skipped once turned off"
# Next launch: saved setting still applies.
mkdir -p "$TMP/state_pv4"; cp "$TMP/state_pv/settings" "$TMP/state_pv4/settings"
printf 'tap 10 400\nquit\n' | $BIN -v -s "$TMP/state_pv4" "$ZIP" >/dev/null 2>"$TMP/err"
grep -q 'panel=-1' "$TMP/err" && fail "saved preview-off setting ignored"
# Front light (600 wide: rows 58px, [-] at x 394..481, [+] at 497..584, bar 232..290 from x 16 to 584).
# + twice, - once, then tap the bar's right end (max) and left end (off). Light changes are partial refreshes.
printf 'tap 300 5\ntap 200 5\ntap 540 200\ntap 540 200\ntap 430 200\ntap 584 260\ntap 16 260\ntap 100 200\ntap 10 20\nquit\n' |
	MANGLE_SIM_LIGHT=10/24 $BIN -s "$TMP/state_fl" "$ZIP" >"$TMP/out" 2>"$TMP/err"
[ "$(grep '^LIGHT' "$TMP/out" | tr '\n' ' ')" = "LIGHT 11 LIGHT 12 LIGHT 11 LIGHT 24 LIGHT 0 " ] || fail "front light: $(grep '^LIGHT' "$TMP/out" | tr '\n' ' ')"
[ "$(grep -A1 '^LIGHT' "$TMP/out" | grep -c '^FRAME.* partial')" = 5 ] || fail "light change should redraw with a partial refresh"
# No front light: the row says so and taps do nothing.
printf 'tap 300 5\ntap 200 5\ntap 540 200\ntap 300 260\ntap 10 20\nquit\n' |
	MANGLE_SIM_LIGHT=none $BIN -s "$TMP/state_fl2" "$ZIP" >"$TMP/out" 2>"$TMP/err"
grep -q '^LIGHT' "$TMP/out" && fail "set a light that isn't there"
# Stats: a session is logged when the book closes (pages seen, start/end times).
printf 'tap 10 400\nsleep 11000\ntap 10 400\nquit\n' | $BIN -v -s "$TMP/state_stats" "$ZIP" >/dev/null 2>"$TMP/err"
[ -f "$TMP/state_stats/stats.log" ] || fail "no stats log"
awk '{ if (NF < 4 || $2 - $1 < 10 || $3 != 2) exit 1 }' "$TMP/state_stats/stats.log" || fail "bad stats line: $(cat "$TMP/state_stats/stats.log")"
[ "$(awk '{print $4}' "$TMP/state_stats/stats.log")" = "$(basename "$ZIP")" ] || fail "stats book name"
# Quick open-and-close is not a session.
echo quit | $BIN -s "$TMP/state_stats" "$ZIP" >/dev/null 2>&1
[ "$(wc -l <"$TMP/state_stats/stats.log")" = 1 ] || fail "short session logged"
# Menu -> Stats (daily), Monthly tab, Back returns to the page.
printf 'tap 300 5\ntap 450 5\ntap 550 20\ntap 10 20\nquit\n' | MANGLE_SIM_DIR="$TMP" $BIN -v -s "$TMP/state_stats" "$ZIP" >"$TMP/out" 2>"$TMP/err"
[ "$(grep -c '^FRAME' "$TMP/out")" = 5 ] || fail "stats screens from the menu"
[ "$(grep -c 'mangle: show' "$TMP/err")" = 2 ] || fail "page not shown again after stats"
python3 -c "import sys;d=open(sys.argv[1],'rb').read()[15:];sys.exit(sum(b<128 for b in d)<5000)" "$TMP/frame_0003.pgm" || fail "daily stats frame is blank"
# Library header: [stats] opens the stats screen (not exit), Back returns to the shelf.
printf 'tap 150 10\ntap 10 20\ntap 300 10\n' | $BIN -l "$TMP/lib" -s "$TMP/state_stats" >"$TMP/out" 2>"$TMP/err"
[ "$(grep -c '^FRAME' "$TMP/out")" = 3 ] || fail "stats from the library"

# Pinch zoom: spread 2x on the cover, drag to pan, pinch in to fit again;
# zoom again then turn the page (zoom resets); zoom, double-tap centre zooms
# out without switching mode. Drags and pinches on the library/stats are ignored.
printf 'pinch 300 400 2\ndrag 300 400 -100 0\npinch 300 400 0.3\npinch 300 400 3\ntap 10 400\npinch 300 400 2\ntap 300 400\nsleep 50\ntap 300 400\nquit\n' |
	MANGLE_SIM_DIR="$TMP" $BIN -v -s "$TMP/state_zoom" "$ZIP" >"$TMP/out" 2>"$TMP/err"
grep 'mangle: show' "$TMP/err" | sed 's/.*show //' >"$TMP/shown"
cat >"$TMP/want" <<W
p001.png page=0 panel=0 mode=panel
p001.png page=0 panel=0 mode=panel zoom=2.00
p001.png page=0 panel=0 mode=panel zoom=2.00
p001.png page=0 panel=0 mode=panel
p001.png page=0 panel=0 mode=panel zoom=3.00
p002.png page=1 panel=-1 mode=panel
p002.png page=1 panel=-1 mode=panel zoom=2.00
p002.png page=1 panel=-1 mode=panel
W
diff "$TMP/want" "$TMP/shown" || fail "pinch zoom"
# The zoomed and panned frames differ from the fitted one and each other.
cmp -s "$TMP/frame_0001.pgm" "$TMP/frame_0002.pgm" && fail "zoom did not change the frame"
cmp -s "$TMP/frame_0002.pgm" "$TMP/frame_0003.pgm" && fail "drag did not pan"
cmp -s "$TMP/frame_0001.pgm" "$TMP/frame_0004.pgm" || fail "pinching in should restore the fitted view"
# Not zoomed: a drag does nothing.
printf 'drag 300 400 -100 0\nquit\n' | $BIN -v -s "$TMP/state_zoom2" "$ZIP" >"$TMP/out" 2>"$TMP/err"
[ "$(grep -c '^FRAME' "$TMP/out")" = 1 ] || fail "drag without zoom redrew"

echo "sim tests passed"
