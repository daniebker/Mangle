#!/bin/sh
# End-to-end test of the app as a Kindle (600x800 e-ink, multitouch, can
# exit to KUAL), using the desktop simulator.
#   tests/sim_kindle.sh BOOK.zip      (a book made with prep.py --screen 600x800)
set -eu
PY=${PYTHON:-python3}
cd "$(dirname "$0")/.."
ZIP=$1
BIN=./build/mangle
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export MANGLE_SIM_DEVICE=kindle MANGLE_SIM_SCREEN=600x800
fail() { echo "FAIL (kindle): $*"; cat "$TMP/err" 2>/dev/null | tail -20; exit 1; }
shown() { grep 'mangle: show' "$TMP/err" | sed 's/.*show //'; }
frames() { grep '^FRAME' "$TMP/out"; }

# Layout at 600x800 (UI text 16x26): bars 38 px; the top twelfth (66 px) of a
# page opens the menu. Library header buttons: Stats 232-344, Settings
# 344-504, Exit 504-600. Menu bar (one book): < Exit 0-128, Settings 128-288,
# Stats 288-400; with a library: < Library 0-176, Settings 176-336, Stats
# 336-448. Settings rows: Back 0-38, preview 38-96, sleep 96-154, brightness
# 154-212, rotation 212-270, night 270-328. Stats tabs: Daily 328-440, Monthly 440-600.

# Taps: left third = forward (RTL), right third = back, centre = toggle.
# Cover -> p002 overview -> panel 1 -> panel 2 -> back to panel 1 ->
# double-tap centre (page mode) -> forward -> single centre tap -> hold centre.
printf 'tap 10 400\ntap 10 400\ntap 10 400\ntap 590 400\ntap 300 400\nsleep 50\ntap 300 400\nsleep 400\ntap 10 400\ntap 300 400\nsleep 400\nlong 300 400\n' |
	$BIN -v -s "$TMP/state" "$ZIP" >"$TMP/out" 2>"$TMP/err" || fail "exited with an error"
shown >"$TMP/shown"
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
# The first page and the mode switch are full refreshes; turns are partial.
[ "$(frames | sed -n 1p | cut -d' ' -f3)" = full ] || fail "first frame not a full refresh"
[ "$(frames | sed -n 2p | cut -d' ' -f3)" = partial ] || fail "turn not partial"
[ "$(frames | sed -n 6p | cut -d' ' -f3)" = full ] || fail "toggle not a full refresh"

# Back from the first panel returns to the overview, then the previous page.
printf 'tap 10 400\ntap 10 400\ntap 590 400\ntap 590 400\nquit\n' | $BIN -v -s "$TMP/state_ov" "$ZIP" >/dev/null 2>"$TMP/err"
[ "$(shown | cut -d' ' -f1 | tr '\n' ' ')" = "p001.png p002.png p002.png p002.png p001.png " ] || fail "overview going back"
# -O: no overview, straight to the first panel.
printf 'tap 10 400\nquit\n' | $BIN -O -v -s "$TMP/state_noov" "$ZIP" >/dev/null 2>"$TMP/err"
grep -q 'show p002.png page=1 panel=0' "$TMP/err" || fail "-O should skip the overview"

# Resume: reopening shows the saved position; -r reopens the last book.
echo quit | $BIN -v -s "$TMP/state" "$ZIP" >/dev/null 2>"$TMP/err"
grep -q 'show p004.png page=3 panel=0 mode=page' "$TMP/err" || fail "resume"
echo quit | $BIN -v -s "$TMP/state" -r >/dev/null 2>"$TMP/err"
grep -q 'show p004.png' "$TMP/err" || fail "resume last (-r)"

# Every 3rd turn flashes with -f 3 (the first page is full too).
printf 'key 109\nkey 109\nkey 109\nkey 104\nquit\n' | $BIN -f 3 -s "$TMP/state2" "$ZIP" >"$TMP/out" 2>"$TMP/err"
[ "$(frames | grep -c ' full ')" = 2 ] || fail "periodic full refresh"

# Library: a shelf of covers. Natural order ("Vol 2" first); tapping a cover
# opens it, holding the centre returns, Exit leaves.
mkdir -p "$TMP/lib"
cp "$ZIP" "$TMP/lib/Vol 2.zip"
cp "$ZIP" "$TMP/lib/Vol 10.zip"
printf 'tap 100 150\nlong 300 400\ntap 550 10\n' | MANGLE_SIM_DIR="$TMP" $BIN -v -l "$TMP/lib" -s "$TMP/state3" >"$TMP/out" 2>"$TMP/err" ||
	fail "library exited with an error"
grep -q 'show p001.png' "$TMP/err" || fail "library open"
[ "$(frames | wc -l)" = 3 ] || fail "shelf, page, shelf again"
[ -f "$TMP/state3/Vol 2.zip.pos" ] || fail "natural order: first cover should be Vol 2"
[ -f "$TMP/state3/Vol 2.zip.thm" ] || fail "cover thumbnail not cached"
"$PY" -c "import sys;d=open(sys.argv[1],'rb').read()[15:];sys.exit(len(set(d))<8)" "$TMP/frame_0001.pgm" || fail "shelf frame has no covers"

# Bad input is reported on screen, not a crash.
echo "not a zip" >"$TMP/lib/bad.zip"
printf 'tap 1 1\n' | $BIN -s "$TMP/state" "$TMP/lib/bad.zip" >"$TMP/out" 2>&1 || fail "bad zip crashed"
grep -q 'Cannot open book' "$TMP/out" || fail "bad zip message"

# Sleep: after -t idle seconds the cover is shown and the device suspended;
# the page is redrawn on wake.
printf 'tap 10 400\nsleep 2500\nquit\n' | $BIN -t 1 -v -s "$TMP/state_sl" "$ZIP" >"$TMP/out" 2>"$TMP/err"
grep -q 'mangle: sleep' "$TMP/err" || fail "no sleep after idle"
grep -q '^SUSPEND' "$TMP/out" || fail "device not suspended"
[ "$(grep -c 'mangle: show' "$TMP/err")" = 3 ] || fail "wake should redraw the page"
# Settings (menu -> Settings): Sleep cycles 2 -> 5 -> 10 -> 30 -> never, saved; no sleep after.
printf 'tap 300 5\ntap 200 5\ntap 300 120\ntap 300 120\ntap 300 120\ntap 300 120\ntap 10 20\nsleep 2500\nquit\n' |
	$BIN -t 1 -v -s "$TMP/state_st" "$ZIP" >/dev/null 2>"$TMP/err"
grep -q 'sleep_min=0' "$TMP/state_st/settings" || fail "sleep setting not saved"
grep -q 'mangle: sleep' "$TMP/err" && fail "slept with sleep turned off"
# Page preview off: saved, and takes effect at once.
printf 'tap 300 5\ntap 200 5\ntap 300 60\ntap 10 20\ntap 10 400\nquit\n' | $BIN -v -s "$TMP/state_pv" "$ZIP" >/dev/null 2>"$TMP/err"
grep -q 'overview=0' "$TMP/state_pv/settings" || fail "preview setting not saved"
grep -q 'show p002.png page=1 panel=0 mode=panel' "$TMP/err" || fail "preview should be skipped once turned off"
# Rotation and night mode: saved; the frame stays the physical size, turned.
mkdir -p "$TMP/rot"
printf 'tap 300 5\ntap 200 5\ntap 300 240\ntap 300 300\ntap 10 20\nquit\n' |
	MANGLE_SIM_DIR="$TMP/rot" $BIN -v -s "$TMP/state_rot" "$ZIP" >"$TMP/out" 2>"$TMP/err"
grep -q 'rotation=1' "$TMP/state_rot/settings" && grep -q 'night=1' "$TMP/state_rot/settings" || fail "rotation/night not saved"
last=$(frames | tail -1 | cut -d' ' -f4)
head -c 15 "$last" | grep -q '600 800' || fail "frame should stay 600x800"
# Night mode: the page's white margin is black.
"$PY" -c "import sys;d=open(sys.argv[1],'rb').read();sys.exit(d[15+5]!=0)" "$last" || fail "night mode not inverted"

# Stats: a session is logged when the book closes (pages seen, start and end).
printf 'tap 10 400\nsleep 11000\ntap 10 400\nquit\n' | $BIN -v -s "$TMP/state_stats" "$ZIP" >/dev/null 2>"$TMP/err"
[ -f "$TMP/state_stats/stats.log" ] || fail "no stats log"
awk '{ if (NF < 4 || $2 - $1 < 10 || $3 != 2) exit 1 }' "$TMP/state_stats/stats.log" || fail "bad stats line: $(cat "$TMP/state_stats/stats.log")"
echo quit | $BIN -s "$TMP/state_stats" "$ZIP" >/dev/null 2>&1    # a quick open-and-close is not a session
[ "$(wc -l <"$TMP/state_stats/stats.log")" = 1 ] || fail "short session logged"
# Menu -> Stats (daily), Monthly tab, Back returns to the page.
printf 'tap 300 5\ntap 340 5\ntap 550 20\ntap 10 20\nquit\n' | MANGLE_SIM_DIR="$TMP" $BIN -v -s "$TMP/state_stats" "$ZIP" >"$TMP/out" 2>"$TMP/err"
[ "$(frames | wc -l)" = 5 ] || fail "stats screens from the menu"
[ "$(grep -c 'mangle: show' "$TMP/err")" = 2 ] || fail "page not shown again after stats"
"$PY" -c "import sys;d=open(sys.argv[1],'rb').read()[15:];sys.exit(sum(b<128 for b in d)<5000)" "$TMP/frame_0003.pgm" || fail "daily stats frame is blank"
# Library header: Stats opens the stats screen, Back returns to the shelf, Exit leaves.
printf 'tap 290 10\ntap 10 20\ntap 550 10\n' | $BIN -l "$TMP/lib" -s "$TMP/state_stats" >"$TMP/out" 2>"$TMP/err"
[ "$(frames | wc -l)" = 3 ] || fail "stats from the library"

# Zoom: pinch 2x, drag to pan, pinch in to fit again; zoom then turn (zoom
# resets); zoom, double-tap centre zooms out without switching mode; hold a
# side for 2x, 3x and back.
printf 'pinch 300 400 2\ndrag 300 400 -100 0\npinch 300 400 0.3\npinch 300 400 3\ntap 10 400\npinch 300 400 2\ntap 300 400\nsleep 50\ntap 300 400\nlong 10 400\nlong 10 400\nlong 10 400\nquit\n' |
	MANGLE_SIM_DIR="$TMP" $BIN -v -s "$TMP/state_zoom" "$ZIP" >"$TMP/out" 2>"$TMP/err"
shown >"$TMP/shown"
cat >"$TMP/want" <<W
p001.png page=0 panel=0 mode=panel
p001.png page=0 panel=0 mode=panel zoom=2.00
p001.png page=0 panel=0 mode=panel zoom=2.00
p001.png page=0 panel=0 mode=panel
p001.png page=0 panel=0 mode=panel zoom=3.00
p002.png page=1 panel=-1 mode=panel
p002.png page=1 panel=-1 mode=panel zoom=2.00
p002.png page=1 panel=-1 mode=panel
p002.png page=1 panel=-1 mode=panel zoom=2.00
p002.png page=1 panel=-1 mode=panel zoom=3.00
p002.png page=1 panel=-1 mode=panel
W
diff "$TMP/want" "$TMP/shown" || fail "zoom"
cmp -s "$TMP/frame_0001.pgm" "$TMP/frame_0002.pgm" && fail "zoom did not change the frame"
cmp -s "$TMP/frame_0002.pgm" "$TMP/frame_0003.pgm" && fail "drag did not pan"
cmp -s "$TMP/frame_0001.pgm" "$TMP/frame_0004.pgm" || fail "pinching in should restore the fitted view"
# Not zoomed: a swipe turns the page (RTL: to the right goes forward).
printf 'drag 100 400 200 10\ndrag 400 400 -200 0\ndrag 300 400 0 200\nquit\n' | $BIN -v -s "$TMP/state_sw" "$ZIP" >"$TMP/out" 2>"$TMP/err"
[ "$(shown | cut -d' ' -f1,2 | tr '\n' ' ')" = "p001.png page=0 p002.png page=1 p001.png page=0 " ] || fail "swipes: $(shown | tr '\n' '|')"

echo "kindle sim tests passed"
