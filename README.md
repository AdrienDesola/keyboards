# silakka54-zmk-config

ZMK firmware for the PandaKB Silakka54 MX wireless (nice!nano-compatible controller, Sharp/nice!view display).
Keymap ported from `silakka54.vil` in [AdrienDesola/crkbd](https://github.com/AdrienDesola/crkbd).

Native `silakka54` shield in `config/boards/shields/silakka54/`: Lily58 pin wiring (what the PandaKB
factory firmware uses) with a 54-key matrix transform and physical layout, so ZMK Studio shows the real board.

## Flash

1. Download the artifacts from the latest Actions run: `silakka54_left-nice_nano_v2-zmk.uf2`, `silakka54_right-nice_nano_v2-zmk.uf2`.
2. Plug a half in, double-tap its reset button. A `NICENANO` drive appears.
3. Copy the matching `.uf2` onto it. The board reboots. Repeat for the other half.

To clear Bluetooth bonds (halves not pairing): flash `settings_reset` on both halves, then re-flash the real firmware.

## ZMK Studio

Enabled on the left half over USB. Unlock key: `Fn` + right Shift. Bootloader (no reset button): `Fn` + Tab, puts both halves in bootloader; power-cycle the half you are not flashing.
After editing in Studio, keymap file changes are ignored until "Restore Stock Settings" in Studio.
