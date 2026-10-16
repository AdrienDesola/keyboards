/*
 * Flipbook player. See clip.h for the format.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zmk/display.h>

#include "clip.h"

#define MAX_W 140
#define MAX_H 68
#define MAX_STRIDE ((MAX_W + 7) / 8)
#define PALETTE_BYTES 8
#define MAX_FPS 15

/* Same layout as the frames in art.c: LVGL indexed 1-bit, palette first. */
static uint8_t buf[PALETTE_BYTES + MAX_STRIDE * MAX_H];
static lv_img_dsc_t clip_img = {
    .header.cf = LV_IMG_CF_INDEXED_1BIT,
    .header.always_zero = 0,
    .header.reserved = 0,
    .data = buf,
};
static uint32_t clip_w, clip_h, clip_stride, clip_pixels;

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
    memset(buf + PALETTE_BYTES, 0xff, clip_stride * clip_h);
}

static void toggle_run(uint32_t pix, uint32_t n) {
    uint8_t *pixels = buf + PALETTE_BYTES;
    while (n--) {
        uint32_t row = pix / clip_w;
        uint32_t col = pix - row * clip_w;
        pixels[row * clip_stride + (col >> 3)] ^= 0x80 >> (col & 7);
        pix++;
    }
}

static void decode(const uint8_t *p, const uint8_t *end) {
    uint32_t pix = 0;
    while (p < end && pix < clip_pixels) {
        uint8_t b = *p++;
        uint32_t n = b & 0x7f;
        if (n == 0 || pix + n > clip_pixels) {
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
    if (!clip || clip->frames == 0 || clip->w == 0 || clip->w > MAX_W || clip->h == 0 ||
        clip->h > MAX_H) {
        return;
    }
    clip_w = clip->w;
    clip_h = clip->h;
    clip_stride = (clip_w + 7) / 8;
    clip_pixels = clip_w * clip_h;
    clip_img.header.w = clip_w;
    clip_img.header.h = clip_h;
    clip_img.data_size = PALETTE_BYTES + clip_stride * clip_h;
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
