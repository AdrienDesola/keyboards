/*
 * Which clip plays on which right-half event. NULL = none, the art shows
 * straight away. Clips come from tools/vid2clip.py as widgets/clip_<name>.c.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "clip.h"

extern const struct clip *const clip_boot; /* power-on, and every wake from deep sleep */
extern const struct clip *const clip_link; /* the left half just linked */
extern const struct clip *const clip_idle; /* typing stopped for CONFIG_ZMK_IDLE_TIMEOUT */
