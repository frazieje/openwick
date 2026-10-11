#!/usr/bin/env bash
# Restore the UF2 bootloader on a SparkFun Pro nRF52840 Mini over SWD, using an
# nRF52840 DK's on-board J-Link (P20 "Debug out" header) as the programmer.
#
# Steps:
#   1. Read the target's FICR DEVICEADDR and show the USB serial it implies.
#      Refuse to continue unless it matches --expect-usb-serial (when given),
#      so the DK's own nRF52840 is never erased by mistake.
#   2. Dump the current flash, UICR and FICR to backups/<timestamp>.hex.
#   3. Erase the chip, program the bootloader hex, verify, reset.
#
# Usage:
#   tools/restore_bootloader.sh [--yes] [--expect-usb-serial XXXXXXXXXXXX]
#                               [--jlink-sn N] [--hex FILE]
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
HEX=$ROOT/bootloader/sparkfun_nrf52840_mini_bootloader-0.11.0_s140_6.1.1.hex
BACKUP_DIR=${BACKUP_DIR:-$ROOT/backups}
JLINK_SN=${JLINK_SN:-}
EXPECT_USB_SERIAL=""
YES=0

while [ $# -gt 0 ]; do
    case "$1" in
        --yes) YES=1 ;;
        --expect-usb-serial) EXPECT_USB_SERIAL=${2^^}; shift ;;
        --jlink-sn) JLINK_SN=$2; shift ;;
        --hex) HEX=$2; shift ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

[ -f "$HEX" ] || { echo "bootloader hex not found: $HEX" >&2; exit 1; }
command -v JLinkExe >/dev/null || { echo "JLinkExe not found in PATH" >&2; exit 1; }
command -v nrfutil >/dev/null || { echo "nrfutil not found in PATH" >&2; exit 1; }

if [ -z "$JLINK_SN" ]; then
    JLINK_SN=$(nrfutil device list --json 2>/dev/null | python3 -I -c '
import json, sys
for line in sys.stdin:
    line = line.strip()
    if not line.startswith("{"):
        continue
    d = json.loads(line)
    devs = d.get("devices") or d.get("data", {}).get("devices") or []
    for dev in devs:
        if dev.get("traits", {}).get("jlink"):
            print(dev["serialNumber"].lstrip("0"))
            sys.exit(0)
')
    [ -n "$JLINK_SN" ] || { echo "no J-Link probe found" >&2; exit 1; }
fi
echo "J-Link probe: $JLINK_SN"

# --- 1. identify the chip on the other end of the SWD wires ---------------
script=$(mktemp)
trap 'rm -f "$script"' EXIT
printf 'connect\nmem32 0x100000A4 2\nmem32 0x10000060 2\nmem32 0x10001014 1\nmem32 0x00000000 1\nexit\n' > "$script"
probe=$(JLinkExe -nogui 1 -device nRF52840_xxAA -if SWD -speed 4000 -autoconnect 1 \
        -USB "$JLINK_SN" -CommandFile "$script" 2>&1 || true)
addr_words=$(printf '%s\n' "$probe" | awk '/^100000A4 = /{print $3, $4}')
id_words=$(printf '%s\n' "$probe" | awk '/^10000060 = /{print $3, $4}')
bl_word=$(printf '%s\n' "$probe" | awk '/^10001014 = /{print $3}')
vec_word=$(printf '%s\n' "$probe" | awk '/^00000000 = /{print $3}')
vtref=$(printf '%s\n' "$probe" | awk -F= '/^VTref/{print $2}')
if [ -z "$addr_words" ]; then
    echo "could not read the target over SWD (VTref:${vtref:-?}). J-Link said:" >&2
    printf '%s\n' "$probe" | grep -iE 'error|fail|could not|VTref' >&2 || true
    exit 1
fi
set -- $addr_words
# Nordic firmware derives the USB serial from the 48-bit DEVICEADDR with the two
# top bits forced to 1 (random static address), then DEVICEADDR[0].
usb_serial=$(printf '%02X%s%s' $(( 0x${2:4:2} | 0xC0 )) "${2:6:2}" "$1")
echo "target VTref:                 ${vtref:-?}"
echo "FICR DEVICEID:                $id_words"
echo "FICR DEVICEADDR:              $addr_words  (USB serial form: $usb_serial)"
echo "UICR NRFFW[0] (bootloader):   0x$bl_word   (0x000F4000 means a bootloader is registered)"
echo "flash word 0 (initial SP):    0x$vec_word"
if [ -n "$EXPECT_USB_SERIAL" ] && [ "$usb_serial" != "$EXPECT_USB_SERIAL" ]; then
    echo "DEVICEADDR does not match the expected USB serial $EXPECT_USB_SERIAL." >&2
    echo "Is the DK's Debug Out header wired to the SparkFun board (VTG and GND DETECT included)?" >&2
    exit 1
fi
if [ "$YES" != 1 ]; then
    read -r -p "Erase this chip and program $(basename "$HEX")? [y/N] " ans
    [ "${ans,,}" = y ] || { echo "aborted"; exit 1; }
fi

# --- 2. backup ---------------------------------------------------------------
mkdir -p "$BACKUP_DIR"
backup=$BACKUP_DIR/sparkfun_mini_flash_$(date +%Y%m%d_%H%M%S).hex
echo "backing up flash + UICR + FICR to $backup"
nrfutil device dump-to-file --code --uicr --ficr "$backup" --serial-number "$JLINK_SN"
ls -l "$backup"

# --- 3. erase, program, verify, reset ----------------------------------------
echo "programming $(basename "$HEX")"
nrfutil device program --firmware "$HEX" --serial-number "$JLINK_SN" \
    --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_SYSTEM
echo
echo "Done. The bootloader finds no application, so it stays in UF2 mode:"
echo "the blue LED pulses and an NRF52BOOT drive appears over the board's own USB."
echo "Then: ./build.sh && ./flash.sh"
