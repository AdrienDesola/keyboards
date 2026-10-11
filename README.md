# keyboards

My keyboard configs, one folder per board.

| Folder | Board | Firmware | Edit with |
|---|---|---|---|
| `corne/` | Corne (wired) | QMK + Vial | Vial GUI, export to `corne.vil` |
| `silakka54-wired/` | Silakka54, RP2040 Zero | QMK + Vial | Vial GUI, export to `silakka54.vil` |
| `silakka54-wireless/` | PandaKB Silakka54 MX wireless | ZMK | `silakka54-wireless/silakka54.keymap`, built by Actions; ZMK Studio for live tweaks |

Pushing anything under `silakka54-wireless/` builds the `.uf2` files (Actions → latest run → `silakka54-wireless` artifact).
