#!/usr/bin/env bash
# Build the Air96 V2 "mac" keymap and copy the firmware to a fixed path.
# Run from anywhere inside the repo (main checkout or a worktree).
set -euo pipefail

OUT_DIR="$HOME/.local/share/air96"
OUT_BIN="$OUT_DIR/air96_v2_mac.bin"
VENV="$HOME/.local/share/qmk-venv"

cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

# This fork's Python tooling needs packages (e.g. appdirs) newer QMK dropped,
# so `make` uses a venv-backed qmk wrapper instead of the system one.
if [ ! -x "$VENV/bin/qmk" ]; then
    echo "Setting up QMK venv at $VENV"
    python3 -m venv --system-site-packages "$VENV"
    "$VENV/bin/pip" install -q -r requirements.txt
    printf '#!/bin/sh\nexec %s/bin/python /usr/bin/qmk "$@"\n' "$VENV" > "$VENV/bin/qmk"
    chmod +x "$VENV/bin/qmk"
fi

make QMK_BIN="$VENV/bin/qmk" nuphy/air96_v2/ansi:mac

mkdir -p "$OUT_DIR"
cp .build/nuphy_air96_v2_ansi_mac.bin "$OUT_BIN"
echo
echo "Firmware written to $OUT_BIN ($(git rev-parse --short HEAD), $(date '+%F %T'))"
