# silakka54-zmk-config

ZMK firmware for the PandaKB Silakka54 MX wireless (nice!nano-compatible controller, Sharp/nice!view display).
Keymap read off the old Vial Silakka54 (VIA raw HID dump), newer than `silakka54.vil` in [AdrienDesola/crkbd](https://github.com/AdrienDesola/crkbd).

Native `silakka54` shield in `config/boards/shields/silakka54/`: Lily58 pin wiring (what the PandaKB
factory firmware uses) with a 54-key matrix transform and physical layout, so ZMK Studio shows the real board.

## Flash

1. Download the artifacts from the latest Actions run: `silakka54_left.uf2`, `silakka54_right.uf2`.
2. Plug a half in, double-tap its reset button. A `NICENANO` drive appears.
3. Copy the matching `.uf2` onto it. The board reboots. Repeat for the other half.

To clear Bluetooth bonds (halves not pairing): flash `settings_reset` on both halves, then re-flash the real firmware.

## ZMK Studio

Enabled on the left half over USB. Hold Enter for the Nav layer: `\` = Studio unlock, `0` = bootloader (only the half pressed), `9` = USB/Bluetooth toggle, `` ` `` = clear Bluetooth profile, `1`–`5` = Bluetooth profiles.
After editing in Studio, keymap file changes are ignored until "Restore Stock Settings" in Studio.
