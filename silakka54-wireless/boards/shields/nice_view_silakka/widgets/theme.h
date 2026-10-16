/*
 * A theme is what each screen shows below its status strip: a resting still,
 * optional extra stills for small moves, and clips for the big moments.
 * Themes live in themes/<name>/ and CONFIG_NICE_VIEW_SILAKKA_THEME picks one
 * at build time; nothing from the other themes is compiled in.
 *
 * Any field may be NULL. Stills are LVGL indexed 1-bit images in the
 * display's orientation (140x68 on the right, 80x68 on the left stage).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include "clip.h"

struct theme_side {
    const lv_img_dsc_t *rest;       /* linked, nothing going on */
    const lv_img_dsc_t *blink;      /* held 250 ms: every ART_BLINK_MS while typing, once per ART_IDLE_MS idle */
    const lv_img_dsc_t *pose;       /* held 60 s once per ART_IDLE_MS idle, after the blink */
    const lv_img_dsc_t *asleep;     /* not linked */
    const lv_img_dsc_t *asleep_alt; /* held 1 s once per ART_IDLE_MS while not linked */
    const struct clip *boot;        /* power-on and every wake from deep sleep, while not yet linked */
    const struct clip *link;        /* the other half just linked; cuts anything short, both halves start together */
    const struct clip *idle;        /* typing stopped while linked */
};

/* Defined by themes/<name>/theme.c for the half being built. */
extern const struct theme_side theme_side;
