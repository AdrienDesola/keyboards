# Silakka54 wireless firmware spec

Source of truth for what this firmware does. Change behavior, change this file in the same commit.
`README.md` says how to build and flash; this file says what the result must do.

## Hardware facts

- PandaKB Silakka54 MX wireless, two halves, nice!nano v2 compatible controllers, nice!view displays.
- Left half is the split central: talks to hosts, carries ZMK Studio over USB.
- Right half is the split peripheral: talks only to the left half.
- Controller measures its VDDH rail for the battery percentage. On USB that rail is the USB supply minus a diode
  (about 4.44 V), so the percentage reads 100 on USB whatever the cell holds. Off USB it reads the cell.
- Charger LED on the controller is solid while current flows into the cell, off when full, flickers with no cell.
- Plugging USB into the left half resets it. Harmless.
- ZMK pinned to v0.3 in `west.yml` and in the workflow. ZMK main moved to LVGL 9 and breaks nice!view shields.

## Keymap

- Three layers: Base, Symbols (hold Space), Nav (hold Enter). Layer holds are tap-preferred with a 200 ms term:
  the other key must be pressed after Enter or Space has been held for 200 ms, or it types the tap.
- Base matches the old Vial layout. Ctrl/Esc and Alt/Z and RAlt// are mod-taps.
- Nav row 1, left: `` ` `` clears the active Bluetooth profile, `1` to `5` select profiles 1 to 5.
- Nav row 1, right: `9` toggles USB/Bluetooth output, `0` resets the half it sits on (right) into its bootloader,
  `\` unlocks ZMK Studio.
- Studio edits override the keymap file until "Restore Stock Settings" in Studio.

## Bluetooth profiles

- Slots in use (2026-10-13): 1 = work MacBook Pro, 2 = personal MacBook Air.
- Five host slots. Only the active slot advertises. Links on other slots stay up; only the active one receives keys.
- Clearing a slot drops its bond and starts advertising. The host must then forget the keyboard and pair again.
- A host that keeps old keys after the keyboard cleared the slot links at radio level but cannot encrypt:
  the keyboard shows no link, the host receives nothing. Forgetting on the host fixes it.
- Pairing is started by the host. The keyboard never asks for it. A host that links and never pairs sits
  unpaired with the slot still open. Seen with a work MacBook Pro whose Bluetooth stack was stuck: it linked,
  negotiated connection parameters, never sent a pairing request. Rebooting that Mac fixed it.

## Left display

Rows as the user sees the screen, top to bottom: 68 status, 80 stage, 12 layer.

- Status, first rows: battery percentage bar with a bolt while USB power is present (bolt means USB seen, not
  charging), USB symbol when USB is the output, Bluetooth badge when the active profile is linked, gear when the
  active profile is empty, cross when bonded but away.
- Status, below: active profile only. Filled disc with the slot number, always drawn. Thin outer ring: solid when
  the host is linked, dashed when bonded but away, absent when the slot is empty.
- Name under the disc: the host's Bluetooth name, read over GATT a few seconds after each link and saved in the
  settings area under `nv_host/<slot>`. It stays while the host is away, through deep sleep and reboots, and goes
  with the bond when the slot is cleared or `settings_reset` runs. A changed host name replaces the saved one on the
  next link. Bonded slot with no name yet: the label from `CONFIG_NICE_VIEW_SILAKKA_PROFILE_NAMES` (unset, so blank).
  Empty slot: `open`.
  A name wider than the screen scrolls once, one step per `CONFIG_NICE_VIEW_SILAKKA_NAME_SCROLL_MS`, then parks.
- Stage: 80 rows of the theme's left art (see Themes).
- Layer: name in the small unscii font, bottom strip. For debugging, rarely looked at.
- No WPM: the graph and the ZMK WPM module were removed.
- Polarity: black text on white.

## Right display

- Top strip: battery bar and Bluetooth badge for the link to the left half.
- Below: the theme's right art on 140 rows (see Themes).

## Themes

- A theme is a folder `boards/shields/nice_view_silakka/themes/<name>/` with `left/` and `right/`. Each holds a
  `theme.c` defining `theme_side` (`widgets/theme.h`) plus the stills and clips it points at.
  `CONFIG_NICE_VIEW_SILAKKA_THEME` (set in `silakka54.conf`, default `nhl95`) picks the folder at build time. Each half
  compiles only its own subfolder of the selected theme, so other themes and the other half's art take no flash.
  Changing theme means editing that line and reflashing both halves.
- Stage behaviour, shared by both halves (`widgets/stage.c`), every field optional:
  - Linked: `rest`. While that half is typed on, `blink` for 250 ms every `CONFIG_NICE_VIEW_SILAKKA_ART_BLINK_MS`.
    Idle, every `CONFIG_NICE_VIEW_SILAKKA_ART_IDLE_MS`: `blink`, then `pose` for 60 s, back to `rest`.
  - Not linked: `asleep`, with `asleep_alt` for 1 s once per idle period.
  - Clips: `boot` at power-on and every wake from deep sleep while the halves are not yet linked; `link` the moment
    the halves link, on both halves at once, so a matched pair plays together; `idle` when typing stops while
    linked. A clip never interrupts another except `link`, which waits for nothing. A key press or a lost link cuts a
    clip short. Frame rate is the clip's own, capped at 15 fps, on the display thread, so keys never wait on it.
- Link state comes from the Bluetooth stack's connection callbacks, filtered by role: the split link is the one where
  the left half is central and the right half peripheral. Host links are ignored.
- Sizes as the user sees them: right 68x140, left 68x80. `tools/vid2clip.py --size WxH` makes clips from a video,
  `tools/frames2clip.py` from a folder of finished one-bit frames, `tools/img2still.py` stills.
- Clip storage (`widgets/clip.h`): every frame is the XOR of the previous one, packed with one of two codecs chosen
  per clip, whichever is smaller: bit runs (still backgrounds, big flat areas) or zero-run/literal bytes (busy line
  art, where it is about half the size). `frames2clip.py` picks; `vid2clip.py` always writes bit runs.
- habs theme: Canadiens #85 against the Leafs' #1 in black-and-white manga ink (AI stills and motion made locally,
  puck, streaks, GOAL! and Zamboni drawn in code). Ignored by git for the team names and logos. Boot = the faceoff
  split across both halves; link = #85 shoots off the left screen, the puck goes in top shelf on the right, GOAL!;
  idle = the Zamboni crossing both screens and leaving fresh ice; rest/blink = typing poses; asleep = searching poses.
  The folder also holds loops for searching, typing and host search, unused until the stage can play loops.
- nhl95 theme (NHL Hockey '95, Game Boy). Its folder is ignored by git because the art is the game's
  copyrighted graphics; it exists only on the machine that made it. Built without it, both stages stay blank (the build
  falls back to `widgets/theme_empty.c` with a warning).
  - Right: Bill Brown at the desk (`rest`), his next intro line held a minute when idle (`pose`), EA SPORTS logo while
    not linked (`asleep`). Clips: boot = Game Boy Nintendo scroll then EA SPORTS (4 s); link = opening faceoff from
    above (6 s); idle = a goal at the net (6 s).
  - Left: the HUD referee at twice the Game Boy pixel size, arms crossed (`rest`), arms shifted (`blink`), pointing
    (`pose`). Clips: link = faceoff close-up, raises and drops the puck (2.6 s); idle = arm up for offside (2.2 s).
  - Source: World-of-Longplays' Game Boy longplay of NHL Hockey '95, cut with `tools/img2still.py` and
    `tools/vid2clip.py`.

## Power

- Deep sleep after 15 min idle (ZMK default), not while on USB. Wake is a key press plus about a second to relink.
- Battery of the right half is fetched by the left and proxied to the host.
- Radio transmit power is the ZMK default, 0 dBm. Both halves sit on one desk, so the earlier +8 dBm bought range nothing needed at three times the transmit current on the left half, which transmits every 7.5 ms as the split central.
- Radio and display timing stay at ZMK defaults (split connection interval 7.5 ms, display tick 10 ms). Tuning them trades latency for battery and was declined.

## Build, flash, versions

- GitHub Actions builds every push touching `silakka54-wireless/`. Artifact `silakka54-wireless` holds
  `silakka54_left.uf2`, `silakka54_right.uf2`, `settings_reset.uf2`. Each run is tied to one commit.
- Flash: double-click the half's reset button with USB in, `NICENANO` mounts, copy the matching file.
  `&bootloader` on Nav `0` resets only the right half, the key sits there.
- Git tags `flashed-left` and `flashed-right` point at the commit running on each half. Move the tag on every
  flash: `git tag -f flashed-left <sha> -m "<run id>, <date>"` then `git push -f origin flashed-left`.
- Halves lost each other: flash `settings_reset.uf2` on both, then the real firmware on both, then pair hosts again.
- Branch `diag-battery`: same firmware plus USB debug logging (`zmk-usb-logging` snippet, debug log level,
  battery sampled every 10 s). Second serial port appears next to the Studio port; `cat` it at 115200.
  Battery line reads `vddh_sample_fetch: ADC raw N ~ M mV => P%`. Not for daily use, drains the battery faster.

## Known quirks

- Power switch: one position makes the board vanish from USB on some plugs; both positions keep the board alive
  on USB and leave the VDDH reading unchanged. Not yet mapped to the wiring.
- Mac `system_profiler` lists the keyboard as Not Connected even while typing over Bluetooth works. Ignore it.

## Open

- Old repo `AdrienDesola/silakka54-zmk-config` still to delete.
