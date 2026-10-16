# Silakka54 wireless (ZMK)

ZMK firmware for the PandaKB Silakka54 MX wireless (nice!nano-compatible controller, Sharp/nice!view display).
What the firmware must do, and which commit runs on each half, is in `SPEC.md`.
Keymap read off the old Vial Silakka54 (VIA raw HID dump), newer than `silakka54.vil` in [AdrienDesola/crkbd](https://github.com/AdrienDesola/crkbd).

Native `silakka54` shield in `boards/shields/silakka54/`: Lily58 pin wiring (what the PandaKB
factory firmware uses) with a 54-key matrix transform and physical layout, so ZMK Studio shows the real board.

## Flash

1. Download the `silakka54-wireless` artifact from the latest Actions run: `silakka54_left.uf2`, `silakka54_right.uf2`.
2. Plug a half in, double-tap its reset button. A `NICENANO` drive appears.
3. Copy the matching `.uf2` onto it. The board reboots. Repeat for the other half.

To clear Bluetooth bonds (halves not pairing): flash `settings_reset` on both halves, then re-flash the real firmware.

## ZMK Studio

Enabled on the left half over USB. Hold Enter for the Nav layer: `\` = Studio unlock, `0` = bootloader (only the half pressed), `9` = USB/Bluetooth toggle, `` ` `` = clear Bluetooth profile, `1`–`5` = Bluetooth profiles.
After editing in Studio, keymap file changes are ignored until "Restore Stock Settings" in Studio.

## Status screen

`boards/shields/nice_view_silakka/` is the stock nice!view shield reworked. Left half, top to bottom: battery and
output, the active Bluetooth profile with its host's name (read over GATT, saved in settings, scrolls once when
wider than the screen, `open` for an empty slot), an 80-row stage, the layer name in small type. Right half: battery
and link to the left half, then a 140-row stage.

The stages are themed. `themes/<name>/left/` and `themes/<name>/right/` each hold a `theme.c` that names a resting
still, optional blink, pose and asleep stills, and optional flipbook clips for power-on, link and idle.
Set `CONFIG_NICE_VIEW_SILAKKA_THEME` in `silakka54.conf` and reflash both halves; only the chosen theme, and only
each half's own part of it, is compiled. See `SPEC.md`, section Themes, for the exact behaviour.

Making art: `tools/img2art.py` fits a picture into one-bit stills; `tools/vid2clip.py <video> <name> --start S
--duration D --fps 12 --size 68x140` (or `68x80` for the left) turns video into `clip_<name>.c` (run-length frames,
XOR deltas, about 100 to 900 bytes a frame depending on how flat the art is). Both need Pillow, the clip tool ffmpeg.
`tools/gen_jack.py` rebuilds the jack theme's right-half stills.
