#!/usr/bin/env bash
# Rebuilds the bundled opera-boot ROM from the source in third_party/opera-boot
# and installs it as the image a3DO embeds.
#
#   tools/build-opera-boot.sh /path/to/3do-devkit
#
# opera-boot is compiled with the 3DO DevKit's ARM SDT tools (armcc, armasm,
# armlink), which are not part of the normal Android or desktop toolchain. That
# is why a3DO builds from the prebuilt image in third_party/opera-boot/prebuilt
# by default: run this only after changing the ROM's source, then commit the
# new prebuilt/opera-boot.bin with the source change.
#
# DevKit: https://github.com/trapexit/3do-devkit
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$here/third_party/opera-boot"
devkit="${1:-${TDO_DEVKIT_PATH:-}}"

if [[ -z "$devkit" || ! -f "$devkit/activate-env" ]]; then
    echo "usage: $0 /path/to/3do-devkit" >&2
    echo "(or set TDO_DEVKIT_PATH; the folder must contain activate-env)" >&2
    exit 1
fi

# shellcheck disable=SC1091
source "$devkit/activate-env"

make -C "$src" clean
make -C "$src"

size=$(wc -c < "$src/build/opera-boot.bin")
if [[ "$size" -ne 131072 ]]; then
    echo "error: expected a 131072-byte ROM, got $size bytes" >&2
    exit 1
fi

cp "$src/build/opera-boot.bin" "$src/prebuilt/opera-boot.bin"
echo "Installed $src/prebuilt/opera-boot.bin"
sha256sum "$src/prebuilt/opera-boot.bin"
echo "Rebuild a3DO to embed it."
