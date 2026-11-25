#!/bin/sh
# All desktop tests of the shared code: unit tests, then the app end to end in
# the simulator as a Kindle and as both Cheap Yellow Display boards.
# Sample books are made with the preprocessor ($PYTHON needs its requirements).
set -eu
PY=${PYTHON:-python3}
cd "$(dirname "$0")/.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

"$PY" ../preprocessor/tests/sample.py "$TMP/in.cbz"
prep() { (cd ../preprocessor && "$PY" prep.py "$TMP/in.cbz" -j 2 "$@") 2>"$TMP/prep.log" ||
	{ cat "$TMP/prep.log"; echo "FAIL: prep.py could not build a test book"; exit 1; }; }
prep -o "$TMP/kindle.zip" --screen 600x800
prep -o "$TMP/cyd.zip" --device cyd
"$PY" tests/make_pngs.py "$TMP/pngs.zip"

./build/tests "$TMP/pngs.zip" "$TMP/kindle.zip" "$TMP/cyd.zip"
PYTHON=$PY ./tests/sim_kindle.sh "$TMP/kindle.zip"
PYTHON=$PY ./tests/sim_cyd.sh "$TMP/cyd.zip"
