/*
 * Event to clip mapping. Edit here to change what plays when.
 *
 * SPDX-License-Identifier: MIT
 */

#include "clips.h"

extern const struct clip wheel; /* test clip: Steamboat Willie at the wheel, 8 s */

const struct clip *const clip_boot = &wheel;
const struct clip *const clip_link = &wheel;
const struct clip *const clip_idle = &wheel;
