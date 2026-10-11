#!/usr/bin/env bash
# Open the board's USB console: Zephyr shell plus the log output.
# Press Enter for a prompt. Leave screen with Ctrl-A then K, then y.
set -euo pipefail

PORT="${1:-}"
if [ -z "$PORT" ]; then
	for p in /dev/serial/by-id/usb-Zephyr_Project_*-if00; do
		[ -e "$p" ] && { PORT=$p; break; }
	done
fi
[ -n "$PORT" ] || {
	echo "No Zephyr USB serial port found. Is the board plugged in and running the"
	echo "firmware (not sitting in the bootloader)?"
	exit 1
}

echo "Console on $PORT at 115200. Press Enter for a prompt."
if command -v tio >/dev/null; then
	echo "(exit: Ctrl-T then q)"; exec tio -b 115200 "$PORT"
elif command -v picocom >/dev/null; then
	echo "(exit: Ctrl-A then Ctrl-X)"; exec picocom -b 115200 "$PORT"
else
	echo "(exit: Ctrl-A then K, then y)"; exec screen "$PORT" 115200
fi
