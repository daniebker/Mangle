#!/bin/sh
# End-to-end test of the app as a 3.5" Cheap Yellow Display (320x480 LCD,
# resistive or capacitive touch, no exit), using the desktop simulator.
#   tests/sim_cyd.sh BOOK.zip      (a book made with prep.py --device cyd)
set -eu
PY=${PYTHON:-python3}
cd "$(dirname "$0")/.."
ZIP=$1
BIN=./build/mangle
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/lib" "$TMP/frames"
cp "$ZIP" "$TMP/lib/book.zip"
export MANGLE_SIM_DEVICE=cyd35r MANGLE_SIM_DIR="$TMP/frames"
fail() { echo "FAIL (cyd): $*"; tail -20 "$TMP/err"; exit 1; }
shown() { grep 'mangle: show' "$TMP/err" | sed 's/.*show //'; }
# As the firmware starts it: library, state on the card, reopen the book that was open.
run() { $BIN -v -R -l "$TMP/lib" -s "$TMP/state" "$@" >"$TMP/out" 2>"$TMP/err"; }

# Layout at 320x480: bars 38 px, two covers per row (the first cell is
# 0-160 x 38-240), menu zone the top 40 px. Library header: Stats 48-160,
# Settings 160-320. Menu bar: < Library 0-176, Settings 176-336.
# Settings rows (58 px): preview 38, sleep 96, brightness 154, rotation 212,
# night 270, invert panel 328, calibrate 386.

# Library -> first cover -> p001 -> overview -> panel 1 -> panel 2 -> back ->
# hold a side (2x) -> drag (pan) -> hold (3x) -> hold (fitted) -> double tap
# centre (page mode) -> swipe right (RTL: forward) -> menu -> Library.
printf '%s\n' 'tap 80 150' 'tap 10 300' 'tap 10 300' 'tap 10 300' 'tap 310 300' 'long 20 300' 'drag 100 200 40 30' \
	'long 20 300' 'long 20 300' 'tap 160 300' 'sleep 50' 'tap 160 300' 'sleep 400' 'drag 50 300 120 5' 'tap 160 10' 'tap 20 10' 'quit' |
	run || fail "exited with an error"
shown >"$TMP/shown"
cat >"$TMP/want" <<W
p001.png page=0 panel=0 mode=panel
p002.png page=1 panel=-1 mode=panel
p002.png page=1 panel=0 mode=panel
p002.png page=1 panel=1 mode=panel
p002.png page=1 panel=0 mode=panel
p002.png page=1 panel=0 mode=panel zoom=2.00
p002.png page=1 panel=0 mode=panel zoom=2.00
p002.png page=1 panel=0 mode=panel zoom=3.00
p002.png page=1 panel=0 mode=panel
p002.png page=1 panel=0 mode=page
p003.png page=2 panel=0 mode=page
W
diff "$TMP/want" "$TMP/shown" || fail "unexpected navigation"
[ "$(cat "$TMP/state/book.zip.pos")" = "2 0 0 12" ] || fail "position not saved: $(cat "$TMP/state/book.zip.pos")"
[ -f "$TMP/state/book.zip.thm" ] || fail "no cover thumbnail cached"
grep -q '^SCREEN 320x480' "$TMP/out" || fail "not a 320x480 screen"
# Frames are real pictures (many gray levels), not blank screens.
"$PY" -c "
import sys, glob
fs = sorted(glob.glob(sys.argv[1] + '/*.pgm'))
assert len(fs) >= 10, len(fs)
rich = sum(1 for f in fs if len(set(open(f, 'rb').read()[15:])) > 8)
assert rich >= 8, rich
" "$TMP/frames" || fail "frames look wrong"

# The menu's Library entry closed the book: power-on starts in the library.
# Open it again: it resumes at p003 in page mode.
printf '%s\n' 'tap 80 150' 'quit' | run
[ "$(shown)" = "p003.png page=2 panel=0 mode=page" ] || fail "did not resume"
# That run ended with the book open (power off): the next start reopens it.
printf '%s\n' 'tap 10 300' 'quit' | run
grep -q 'show p003.png page=2' "$TMP/err" && grep -q 'show p004.png page=3' "$TMP/err" || fail "did not reopen the open book at start-up"
# Holding the centre closes the book: back to the library at the next start.
printf '%s\n' 'long 160 300' 'quit' | run
printf '%s\n' 'quit' | run
! grep -q 'mangle: show' "$TMP/err" || fail "a closed book was reopened"

# Settings (library header): rotate to landscape, read a page there.
rm -f "$TMP/frames"/*
printf '%s\n' 'tap 250 10' 'tap 100 240' 'tap 100 20' 'tap 80 150' 'quit' | $BIN -v -l "$TMP/lib" -s "$TMP/state2" >"$TMP/out" 2>"$TMP/err" ||
	fail "exited with an error"
grep -q 'rotation=1' "$TMP/state2/settings" || fail "rotation not saved"
grep -q 'show p' "$TMP/err" || fail "no page in landscape"
# Hardware settings of the resistive board: invert panel and calibration rows.
printf '%s\n' 'tap 250 10' 'tap 100 350' 'tap 100 400' 'quit' | $BIN -l "$TMP/lib" -s "$TMP/state3" >"$TMP/out" 2>"$TMP/err"
grep -q 'invert=1' "$TMP/state3/settings" || fail "invert panel not saved"
grep -q '^CALIBRATE' "$TMP/out" || fail "calibration not started"

# Reading statistics, as on the Kindle; without a clock (no Wi-Fi time) the
# screen explains instead.
printf '%s\n' 'tap 100 10' 'tap 10 20' 'quit' | $BIN -l "$TMP/lib" -s "$TMP/state" >"$TMP/out" 2>"$TMP/err"
[ "$(grep -c '^FRAME' "$TMP/out")" = 3 ] || fail "stats from the library"
printf '%s\n' 'tap 100 10' 'tap 10 20' 'quit' | MANGLE_SIM_NOCLOCK=1 $BIN -l "$TMP/lib" -s "$TMP/state" >"$TMP/out" 2>"$TMP/err"
grep -q 'need the date' "$TMP/err" || fail "no explanation without a clock"

# Capacitive board: pinch zoom; resistive: pinches can't happen.
printf '%s\n' 'tap 80 150' 'pinch 160 240 2' 'quit' | MANGLE_SIM_DEVICE=cyd35c $BIN -v -l "$TMP/lib" -s "$TMP/state4" >"$TMP/out" 2>"$TMP/err"
grep -q 'zoom=2.00' "$TMP/err" || fail "no pinch zoom on the capacitive board"
# The Freenove ESP32-S3 board: same app and screen, capacitive, no calibration row.
printf '%s\n' 'tap 80 150' 'pinch 160 240 2' 'quit' | MANGLE_SIM_DEVICE=freenove35 $BIN -v -l "$TMP/lib" -s "$TMP/state6" >"$TMP/out" 2>"$TMP/err"
grep -q 'zoom=2.00' "$TMP/err" && grep -q '^SCREEN 320x480 freenove35' "$TMP/out" || fail "freenove35 profile"

# The BOOT button: sleep now (cover on screen, only the button wakes it),
# then the page is drawn again.
printf '%s\n' 'tap 80 150' 'button' 'quit' | $BIN -v -l "$TMP/lib" -s "$TMP/state7" >"$TMP/out" 2>"$TMP/err" || fail "exited with an error"
grep -q '^SUSPEND button' "$TMP/out" || fail "BOOT button did not sleep"
grep -q 'mangle: sleep (button)' "$TMP/err" || fail "no sleep log for the button"
[ "$(grep -c 'mangle: show' "$TMP/err")" = 2 ] || fail "page not redrawn after waking"
# In the library it sleeps too (no book: no cover).
printf '%s\n' 'button' 'quit' | $BIN -l "$TMP/lib" -s "$TMP/state7" >"$TMP/out" 2>"$TMP/err"
grep -q '^SUSPEND button' "$TMP/out" || fail "BOOT button did not sleep in the library"

# A broken book shows a message instead of crashing.
printf 'not a zip' >"$TMP/lib/0broken.zip"
printf '%s\n' 'tap 80 150' 'tap 100 100' 'quit' | $BIN -l "$TMP/lib" -s "$TMP/state5" >"$TMP/out" 2>"$TMP/err" || fail "exited with an error"
grep -q 'Cannot open book' "$TMP/err" || fail "no error for the broken book"

echo "cyd sim tests passed"
