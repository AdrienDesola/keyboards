/*
 * Flipbook player. See clip.h for the format.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zmk/display.h>

#include "clip.h"

#define CLIP_W 140
#define CLIP_H 68
#define CLIP_STRIDE ((CLIP_W + 7) / 8)
#define CLIP_PIXELS (CLIP_W * CLIP_H)
#define PALETTE_BYTES 8
#define MAX_FPS 15

/* Same layout as the frames in art.c: LVGL indexed 1-bit, palette first. */
static uint8_t buf[PALETTE_BYTES + CLIP_STRIDE * CLIP_H];
static const lv_img_dsc_t clip_img = {
    .header.cf = LV_IMG_CF_INDEXED_1BIT,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = CLIP_W,
    .header.h = CLIP_H,
    .data_size = sizeof(buf),
    .data = buf,
};

static lv_obj_t *target;
static const struct clip *current;
static uint16_t frame;
static void (*done_cb)(void);

/* Ink is bit 0 with this palette, the polarity gen_jack.py settled on for this
 * board. The converter writes 1 = ink, so the buffer starts all ones and every
 * frame, the first included, toggles its 1-runs into it. */
static void reset_buffer(void) {
    static const uint8_t palette[PALETTE_BYTES] = {
#if CONFIG_NICE_VIEW_SILAKKA_WIDGET_INVERTED
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff,
#else
        0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff,
#endif
    };
    memcpy(buf, palette, PALETTE_BYTES);
    memset(buf + PALETTE_BYTES, 0xff, sizeof(buf) - PALETTE_BYTES);
}

static void toggle_run(uint32_t pix, uint32_t n) {
    uint8_t *pixels = buf + PALETTE_BYTES;
    while (n--) {
        uint32_t row = pix / CLIP_W;
        uint32_t col = pix - row * CLIP_W;
        pixels[row * CLIP_STRIDE + (col >> 3)] ^= 0x80 >> (col & 7);
        pix++;
    }
}

static void decode(const uint8_t *p, const uint8_t *end) {
    uint32_t pix = 0;
    while (p < end && pix < CLIP_PIXELS) {
        uint8_t b = *p++;
        uint32_t n = b & 0x7f;
        if (n == 0 || pix + n > CLIP_PIXELS) {
            return; /* corrupt frame: leave the rest as is */
        }
        if (b & 0x80) {
            toggle_run(pix, n);
        }
        pix += n;
    }
}

static void tick_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(tick_work, tick_cb);

static void tick_cb(struct k_work *work) {
    const struct clip *c = current;
    if (!c || !target) {
        return;
    }
    decode(c->data + c->offsets[frame], c->data + c->offsets[frame + 1]);
    lv_img_cache_invalidate_src(&clip_img);
    lv_img_set_src(target, &clip_img);
    lv_obj_invalidate(target);

    frame++;
    if (frame < c->frames) {
        uint32_t fps = MIN(c->fps ? c->fps : 1, MAX_FPS);
        k_work_schedule_for_queue(zmk_display_work_q(), &tick_work, K_MSEC(1000 / fps));
        return;
    }
    current = NULL;
    if (done_cb) {
        done_cb();
    }
}

void clip_play(lv_obj_t *img, const struct clip *clip, void (*done)(void)) {
    clip_stop();
    if (!clip || clip->frames == 0) {
        return;
    }
    target = img;
    current = clip;
    frame = 0;
    done_cb = done;
    reset_buffer();
    k_work_schedule_for_queue(zmk_display_work_q(), &tick_work, K_NO_WAIT);
}

void clip_stop(void) {
    k_work_cancel_delayable(&tick_work);
    current = NULL;
}

bool clip_playing(void) { return current != NULL; }
