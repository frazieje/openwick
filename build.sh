#!/usr/bin/env bash
# Build the firmware through the nRF Connect SDK toolchain bundle (no global
# west/Zephyr install needed). Extra arguments are passed to "west build",
# e.g.  ./build.sh -p   for a pristine build.
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NCS_VERSION="${NCS_VERSION:-v3.3.0}"
NCS_WORKSPACE="${NCS_WORKSPACE:-$HOME/ncs/$NCS_VERSION}"
BOARD="${BOARD:-sparkfun_pro_nrf52840_mini/nrf52840/uf2}"
BUILD_DIR="${BUILD_DIR:-$APP_DIR/build}"

cd "$NCS_WORKSPACE"
exec nrfutil sdk-manager toolchain launch --ncs-version "$NCS_VERSION" -- \
	west build -b "$BOARD" -d "$BUILD_DIR" --no-sysbuild "$APP_DIR" "$@"
