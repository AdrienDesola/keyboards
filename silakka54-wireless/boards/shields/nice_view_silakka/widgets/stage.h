/*
 * The stage: one lv_img driven by the theme. Link state comes straight from
 * the Bluetooth stack (the split link is the one where this half is central on
 * the left, peripheral on the right), typing state from ZMK activity.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include "theme.h"

void stage_init(lv_obj_t *img, const struct theme_side *theme);
