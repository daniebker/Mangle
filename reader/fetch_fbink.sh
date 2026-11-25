#!/bin/sh
# Fetch FBInk (GPLv3, not vendored) at a pinned release, with its submodules.
set -eu
VERSION=${FBINK_VERSION:-v1.25.0}
mkdir -p "$(dirname "$0")/third_party"
cd "$(dirname "$0")/third_party"
if [ -d FBInk/.git ]; then
	echo "third_party/FBInk already present (delete it to re-fetch)"
	exit 0
fi
git clone --branch "$VERSION" --depth 1 https://github.com/NiLuJe/FBInk.git FBInk
# Only the submodules our build (MINIMAL BITMAP IMAGE) uses; the others
# (i2c-tools, libevdev, libunibreak) are for Kobo / OpenType builds.
git -C FBInk submodule update --init --depth 1 font8x8 stb
echo "FBInk $VERSION ready in third_party/FBInk"
