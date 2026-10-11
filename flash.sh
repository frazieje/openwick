#!/usr/bin/env bash
# Copy build/zephyr/zephyr.uf2 onto the board's bootloader drive.
# Double-tap the RESET button first: the blue LED pulses and a "NRF52BOOT"
# USB drive appears.
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UF2="${1:-$APP_DIR/build/zephyr/zephyr.uf2}"
[ -f "$UF2" ] || { echo "No firmware at $UF2 (run ./build.sh first)"; exit 1; }

find_mount() {
	for m in /media/"$USER"/*/ /run/media/"$USER"/*/ /mnt/*/; do
		[ -f "$m/INFO_UF2.TXT" ] && { echo "$m"; return 0; }
	done
	return 1
}

MOUNT="$(find_mount || true)"
if [ -z "$MOUNT" ]; then
	for dev in /dev/disk/by-label/*BOOT; do
		[ -e "$dev" ] || continue
		udisksctl mount -b "$dev" >/dev/null 2>&1 || true
	done
	sleep 1
	MOUNT="$(find_mount || true)"
fi
[ -n "$MOUNT" ] || { echo "Bootloader drive not found. Double-tap RESET on the board and retry."; exit 1; }

echo "Bootloader: $(grep -i '^Bootloader' "$MOUNT/INFO_UF2.TXT" || cat "$MOUNT/INFO_UF2.TXT")"
echo "Copying $(basename "$UF2") to $MOUNT ..."
cp "$UF2" "$MOUNT/" && sync
echo "Done. The board resets into the new firmware."
