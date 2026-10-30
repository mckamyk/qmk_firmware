#!/usr/bin/env bash
# Flash the firmware written by build.sh. Starts waiting first, so it can be
# launched with the mouse before the keyboard is put into the bootloader.
set -uo pipefail

BIN="$HOME/.local/share/air96/air96_v2_mac.bin"
DFU_ID="0483:df11" # STM32 DFU bootloader
WAIT_SECS="${WAIT_SECS:-180}"

if [ ! -f "$BIN" ]; then
    echo "No firmware at $BIN - run the Build action first."
    exit 1
fi
echo "Firmware: $BIN (built $(date -r "$BIN" '+%F %T'))"

echo
echo "Waiting up to ${WAIT_SECS}s for the bootloader ($DFU_ID). Now:"
echo "  1. Set the side switch to cable/USB mode"
echo "  2. Unplug the USB cable"
echo "  3. Hold Esc and plug the cable back in, release after ~1s"
echo

found=0
for ((i = 0; i < WAIT_SECS; i++)); do
    if dfu-util -l 2>/dev/null | grep -qi "\[$DFU_ID\]"; then
        found=1
        break
    fi
    sleep 1
done
if [ "$found" -ne 1 ]; then
    echo "Timed out waiting for the bootloader. Nothing was flashed."
    exit 1
fi

echo "Bootloader found, flashing..."
out=$(dfu-util -a 0 -d "$DFU_ID" -s 0x08000000:leave -D "$BIN" 2>&1)
echo "$out"

# dfu-util often exits non-zero after a successful write because the chip
# resets on :leave before answering get_status, so judge by the output.
if grep -q "File downloaded successfully\|Download done" <<<"$out"; then
    echo
    echo "Flash complete. The keyboard restarts on its own."
    exit 0
fi
echo
echo "Flash FAILED - the keyboard is still in the bootloader; run Flash again."
exit 1
