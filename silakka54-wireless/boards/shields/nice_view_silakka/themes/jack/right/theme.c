/*
 * Jack theme, right half: Jack Skellington awake while linked, asleep in his
 * coffin otherwise. Test clip on every event until the real ones exist.
 *
 * SPDX-License-Identifier: MIT
 */

#include "theme.h"

LV_IMG_DECLARE(jack_a);   // awake, eyes open
LV_IMG_DECLARE(jack_b);   // awake, blink
LV_IMG_DECLARE(jack_c);   // awake, other pose
LV_IMG_DECLARE(coffin_a); // asleep
LV_IMG_DECLARE(coffin_b); // asleep, Zs drifted up

extern const struct clip wheel; // Steamboat Willie at the wheel, 8 s, test clip

const struct theme_side theme_side = {
    .rest = &jack_a,
    .blink = &jack_b,
    .pose = &jack_c,
    .asleep = &coffin_a,
    .asleep_alt = &coffin_b,
    .boot = &wheel,
    .link = &wheel,
    .idle = &wheel,
};
