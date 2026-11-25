#!/bin/sh
# Build Mangle.
#
#   ./build.sh [kindlehf]        Kindle build for firmware 5.16.3+ -> reader/dist/mangle-kual-kindlehf.zip
#   ./build.sh kindlepw2         Kindle build for older 5.x firmware (soft-float)
#   ./build.sh custom --cross P  Kindle build with another toolchain (P = triplet, e.g. arm-linux-gnueabihf)
#   ./build.sh sim               desktop simulator: reader/build/sim/mangle
#   ./build.sh cyd [ENV]         Cheap Yellow Display firmware (PlatformIO; ENV cyd35r or cyd35c, default both)
#   ./build.sh test              preprocessor + reader + CYD firmware tests
#   ./build.sh toolchain [T]     install koxtoolchain target T (default kindlehf) into ~/x-tools
#   ./build.sh clean             remove build output
#
# Options:
#   --cross PREFIX   toolchain triplet (overrides the target's default)
#   --from-source    toolchain: build it with crosstool-ng (about an hour)
#                    instead of downloading koxtoolchain's prebuilt release
#   -j N             parallel make jobs (default: CPU count)
#
# Environment: PYTHON (default python3), FBINK_VERSION (default v1.25.0),
# KOXTOOLCHAIN_VERSION (prebuilt release, default 2026.08),
# KOXTOOLCHAIN_DIR (where to clone koxtoolchain for --from-source, default ~/koxtoolchain).
set -eu

ROOT=$(cd "$(dirname "$0")" && pwd)
READER=$ROOT/reader
PYTHON=${PYTHON:-python3}
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "$1 not found${2:+ ($2)}"; }

CMD=kindlehf
CROSS=
FROM_SOURCE=
ARG=
while [ $# -gt 0 ]; do
	case "$1" in
		--cross) [ $# -ge 2 ] || die "--cross needs a value"; CROSS=$2; shift 2 ;;
		-j) [ $# -ge 2 ] || die "-j needs a value"; JOBS=$2; shift 2 ;;
		-j*) JOBS=${1#-j}; shift ;;
		--from-source) FROM_SOURCE=1; shift ;;
		-h|--help) sed -n '2,/^set -eu/{/^#/p}' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) die "unknown option $1" ;;
		*)
			if { [ "$CMD" = toolchain ] || [ "$CMD" = cyd ]; } && [ -z "$ARG" ] && [ "${SEEN_CMD:-}" = 1 ]; then ARG=$1
			else CMD=$1; SEEN_CMD=1; fi
			shift ;;
	esac
done

default_cross() {
	case "$1" in
		kindlehf) echo arm-kindlehf-linux-gnueabihf ;;
		kindlepw2) echo arm-kindlepw2-linux-gnueabi ;;
		kindle) echo arm-kindle-linux-gnueabi ;;
		*) echo "" ;;
	esac
}

# Find a cross compiler: on PATH, or in koxtoolchain's default ~/x-tools.
find_cross() {
	triplet=$1
	command -v "$triplet-gcc" >/dev/null 2>&1 && return 0
	if [ -x "$HOME/x-tools/$triplet/bin/$triplet-gcc" ]; then
		PATH=$HOME/x-tools/$triplet/bin:$PATH
		export PATH
		return 0
	fi
	return 1
}

kindle_build() {
	target=$1
	triplet=${CROSS:-$(default_cross "$target")}
	[ -n "$triplet" ] || die "no toolchain for '$target': pass --cross PREFIX"
	find_cross "$triplet" || die "$triplet-gcc not found on PATH or in ~/x-tools.
Build the toolchain with: ./build.sh toolchain $target
(or install koxtoolchain and add x-tools/$triplet/bin to PATH)"
	need make
	need git "to fetch FBInk"
	need zip "to package the KUAL extension"

	if [ ! -f "$READER/third_party/FBInk/fbink.h" ]; then
		say "Fetching FBInk"
		"$READER/fetch_fbink.sh"
	fi

	say "Building reader for $target ($triplet)"
	make -C "$READER" -j"$JOBS" "build/$target/mangle" TARGET="$target" CROSS="$triplet"
	say "Packaging KUAL extension"
	make -C "$READER" package TARGET="$target"
	say "Done: reader/dist/mangle-kual-$target.zip"
	echo "Unzip it into /mnt/us/extensions/ on the Kindle and copy books to /mnt/us/manga/."
}

case "$CMD" in
	kindlehf|kindlepw2|kindle)
		kindle_build "$CMD"
		;;
	custom)
		[ -n "$CROSS" ] || die "custom needs --cross PREFIX"
		kindle_build custom
		;;
	sim)
		need make
		say "Building desktop simulator"
		make -C "$READER" -j"$JOBS" sim
		say "Done: reader/build/sim/mangle"
		;;
	test)
		need make
		need "$PYTHON"
		"$PYTHON" -c 'import PIL, numpy, cv2, pytest' 2>/dev/null ||
			die "Python dependencies missing: $PYTHON -m pip install -r preprocessor/requirements.txt pytest"
		say "Preprocessor tests"
		(cd "$ROOT/preprocessor" && "$PYTHON" -m pytest -q tests)
		say "Reader tests"
		# sim_test.sh generates its sample book with $PYTHON.
		PYTHON=$PYTHON make -C "$READER" -j"$JOBS" test
		say "CYD firmware tests (desktop)"
		PYTHON=$PYTHON make -C "$ROOT/cyd" -j"$JOBS" test
		say "All tests passed"
		;;
	cyd)
		need pio "install PlatformIO: pip install platformio"
		envs=${ARG:-cyd35r cyd35c}
		for e in $envs; do
			say "Building CYD firmware ($e)"
			(cd "$ROOT/cyd" && pio run -e "$e")
		done
		say "Done: cyd/.pio/build/<env>/firmware.bin (flash with: cd cyd && pio run -e <env> -t upload)"
		;;
	toolchain)
		target=${ARG:-kindlehf}
		if [ -z "$FROM_SOURCE" ]; then
			version=${KOXTOOLCHAIN_VERSION:-2026.08}
			need curl
			need zstd "to unpack the toolchain"
			url=https://github.com/koreader/koxtoolchain/releases/download/$version/$target.tar.zst
			tmp=$(mktemp)
			trap 'rm -f "$tmp"' EXIT
			say "Downloading koxtoolchain $version ($target)"
			curl -fL --retry 3 -o "$tmp" "$url" || die "download failed: $url (try --from-source)"
			# The archive holds x-tools/<triplet>/...
			zstd -dc "$tmp" | tar -C "$HOME" -xf -
			say "Installed into ~/x-tools. Now run: ./build.sh $target"
			exit 0
		fi
		need git
		dir=${KOXTOOLCHAIN_DIR:-$HOME/koxtoolchain}
		if [ ! -d "$dir/.git" ]; then
			say "Cloning koxtoolchain into $dir"
			git clone --depth 1 https://github.com/koreader/koxtoolchain.git "$dir"
		fi
		say "Building the $target toolchain (this takes a long time; output goes to ~/x-tools)"
		echo "koxtoolchain needs crosstool-ng's build dependencies; see $dir/README.md if it fails."
		(cd "$dir" && ./gen-tc.sh "$target")
		say "Done. Now run: ./build.sh $target"
		;;
	clean)
		make -C "$READER" clean
		;;
	*)
		die "unknown command '$CMD' (try --help)"
		;;
esac
