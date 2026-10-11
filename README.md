# silakka54-zmk-config

ZMK firmware for the PandaKB Silakka54 MX wireless (nice!nano-compatible controller, Sharp/nice!view display).
Keymap ported from `silakka54.vil` in [AdrienDesola/crkbd](https://github.com/AdrienDesola/crkbd).

Uses the stock `lily58` shield (same wiring as the PandaKB factory firmware); the 4 Lily58 positions
absent on the Silakka54 are `&none`.

## Flash

1. Download the artifacts from the latest Actions run: `silakka54_left`, `silakka54_right`.
2. Plug a half in, double-tap its reset button. A `NICENANO` drive appears.
3. Copy the matching `.uf2` onto it. The board reboots. Repeat for the other half.

To clear Bluetooth bonds (halves not pairing): flash `settings_reset` on both halves, then re-flash the real firmware.

## ZMK Studio

Enabled on the left half over USB. Unlock key: `Fn` + right Shift.
After editing in Studio, keymap file changes are ignored until "Restore Stock Settings" in Studio.
