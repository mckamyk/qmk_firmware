# Agent notes

This is a fork of QMK (NuPhy's vendor branch). **The only keyboard we work on is
the NuPhy Air96 V2 with the `mac` keymap:**

- Keymap: `keyboards/nuphy/air96_v2/keymaps/mac/` (`keymap.c`, `config.h`, `rules.mk`)
- Board/vendor code: `keyboards/nuphy/air96_v2/ansi/` (`ansi.c`, `rf.c`, `side.c`, `sleep.c`)
- Build target: `nuphy/air96_v2/ansi:mac`

Ignore every other keyboard in `keyboards/`, including the other NuPhy boards
and the stock `ansi/keymaps/default` and `via` keymaps. Don't search, edit or
build them unless explicitly asked.

## Building and flashing

Both are T3 project actions (**Build Air96** and **Flash Air96**), and both
call scripts that live next to the keymap:

- `keyboards/nuphy/air96_v2/keymaps/mac/build.sh` builds and copies the
  firmware to `~/.local/share/air96/air96_v2_mac.bin` (always the same path).
- `keyboards/nuphy/air96_v2/keymaps/mac/flash.sh` waits up to 3 minutes for
  the STM32 DFU bootloader (`0483:df11`) to show up, then writes that file with
  `dfu-util`. The Air96 is the user's only keyboard, so they start Flash with
  the mouse and *then* put the board in the bootloader (switch to cable mode,
  unplug, hold Esc, plug in).

Plain `make`/`qmk` doesn't work in this fork: its Python tooling needs packages
(e.g. `appdirs`) the system qmk lacks. `build.sh` uses (and creates if missing)
a venv at `~/.local/share/qmk-venv`; pass
`QMK_BIN=$HOME/.local/share/qmk-venv/bin/qmk` to any manual `make`.

## Keymap gotchas

- Layers: 0 = `_WINBASE`, 1 = `_MACBASE`, 2 = `_MOD` (hold Caps). The vendor
  code in `ansi.c` assumes stock layers (0 = Mac, 2 = Win) when the side
  Mac/Win slider moves, so moving the slider selects the wrong layers.
- `keyboard_post_init_user` forces `_MACBASE` on every boot, and connecting on
  BT1/BT2 forces Win/Mac respectively.
- NuPhy patched `tmk_core/protocol/host.c` to route reports over the RF UART;
  unit tests under `tests/` need stubs for `uart_send_*` to link.
