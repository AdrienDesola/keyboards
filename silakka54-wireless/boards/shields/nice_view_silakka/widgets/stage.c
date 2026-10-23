/*
 * Stage: resting stills with small moves, clips at the big moments.
 * Every move is one or two redraws; nothing runs between them.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/activity.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>

#include "stage.h"

#define BLINK_HOLD_MS 250
#define POSE_HOLD_MS 60000
#define ALT_HOLD_MS 1000
#define SPEED_SAMPLE_MS 250
#define SPEED_SAMPLES 8 /* the typing rate is read over the last 2 s */
#define SKATE_MAX_FPS 12
#define SPRINT_PRESSES 10 /* in the 2 s window, about 60 WPM */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define SPLIT_LINK_ROLE BT_CONN_ROLE_CENTRAL
#else
#define SPLIT_LINK_ROLE BT_CONN_ROLE_PERIPHERAL
#endif

static lv_obj_t *art;
static const struct theme_side *th;
static bool linked;
static bool typing = true;
static bool crouching; /* Ctrl is down */
static bool crouched;  /* the crouch clip is playing, or its last frame is on screen */
static bool skating; /* the clip playing is one of the skate loops */
static const struct clip *skate_now;
static int step;

/* ---- typing rate ----------------------------------------------------- */

static atomic_t presses; /* key presses since the last sample, counted on any thread */
static uint8_t samples[SPEED_SAMPLES];
static int sample_at;

static void sample_presses(void) {
    samples[sample_at] = MIN(atomic_set(&presses, 0), UINT8_MAX);
    sample_at = (sample_at + 1) % SPEED_SAMPLES;
}

struct pace {
    const struct clip *clip;
    uint8_t fps; /* 0 holds the frame */
};

/* Nothing pressed in the last second: the glide. Up to SPRINT_PRESSES in the
 * last 2 s: the stride, two frames a second plus one per key. Faster: the
 * sprint at full rate. A theme without a glide holds the stride instead, one
 * without a sprint keeps striding at full rate. */
static struct pace skate_pace(void) {
    int window = 0, recent = 0;
    for (int i = 0; i < SPEED_SAMPLES; i++) {
        int age = (sample_at - 1 - i + SPEED_SAMPLES) % SPEED_SAMPLES;
        window += samples[age];
        if (i < SPEED_SAMPLES / 2) {
            recent += samples[age];
        }
    }
    if (!recent) {
        return th->glide ? (struct pace){th->glide, th->glide->fps} : (struct pace){th->skate, 0};
    }
    // two presses of slack once sprinting, so a steady 60 WPM doesn't flip gaits
    int sprint_at = skate_now == th->sprint ? SPRINT_PRESSES - 2 : SPRINT_PRESSES;
    if (window >= sprint_at && th->sprint) {
        return (struct pace){th->sprint, SKATE_MAX_FPS};
    }
    return (struct pace){th->skate, MIN(2 + window, SKATE_MAX_FPS)};
}

static void show(const lv_img_dsc_t *img) {
    if (img) {
        lv_img_set_src(art, img);
    }
}

static void motion_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(motion_work, motion_work_cb);

static void schedule(int ms) {
    if (ms > 0) {
        k_work_schedule_for_queue(zmk_display_work_q(), &motion_work, K_MSEC(ms));
    }
}

/* Cancelling the motion from inside its own callback (a gait change) would make
 * Zephyr refuse the next schedule, and the typing rate would stop being read. */
static void cancel_motion(void) {
    if (!(k_work_delayable_busy_get(&motion_work) & K_WORK_RUNNING)) {
        k_work_cancel_delayable(&motion_work);
    }
}

static void restart_motion(void) {
    step = 0;
    skating = false;
    bool was_crouched = crouched;
    crouched = false;
    cancel_motion();
    if (!art || !th) {
        return;
    }
    if (linked && typing && th->skate) {
        if (crouching && th->crouch) {
            // the clip's last frame hands back here and stays while Ctrl is down
            if (!was_crouched) {
                clip_play(art, th->crouch, restart_motion);
            }
            crouched = true;
            return;
        }
        // the loop's last frame hands back here, which starts it over
        struct pace p = skate_pace();
        clip_play(art, p.clip, restart_motion);
        clip_set_fps(p.fps);
        skate_now = p.clip;
        skating = true;
        schedule(SPEED_SAMPLE_MS);
        return;
    }
    if (!linked) {
        show(th->asleep ? th->asleep : th->rest);
        if (th->asleep_alt) {
            schedule(CONFIG_NICE_VIEW_SILAKKA_ART_IDLE_MS);
        }
        return;
    }
    show(th->rest);
    if (typing && th->blink) {
        schedule(CONFIG_NICE_VIEW_SILAKKA_ART_BLINK_MS);
    } else if (!typing && (th->blink || th->pose)) {
        schedule(CONFIG_NICE_VIEW_SILAKKA_ART_IDLE_MS);
    }
}

/* A clip replaces the motion until its last frame, then restart_motion() takes over.
 * With no clip the motion restarts at once. */
static void start_clip(const struct clip *clip) {
    if (!clip) {
        restart_motion();
        return;
    }
    cancel_motion();
    skating = false;
    crouched = false;
    clip_play(art, clip, restart_motion);
}

static void motion_work_cb(struct k_work *work) {
    if (skating) {
        sample_presses();
        struct pace p = skate_pace();
        if (p.clip != skate_now) {
            clip_stop(); // a new gait starts at once, from its first frame
            restart_motion();
            return;
        }
        clip_set_fps(p.fps);
        schedule(SPEED_SAMPLE_MS);
        return;
    }
    if (!art || !th || clip_playing()) {
        return;
    }
    if (!linked) {
        if (step == 0) {
            show(th->asleep_alt);
            step = 1;
            schedule(ALT_HOLD_MS);
        } else {
            show(th->asleep);
            step = 0;
            schedule(CONFIG_NICE_VIEW_SILAKKA_ART_IDLE_MS);
        }
        return;
    }
    if (typing) {
        if (step == 0) {
            show(th->blink);
            step = 1;
            schedule(BLINK_HOLD_MS);
        } else {
            show(th->rest);
            step = 0;
            schedule(CONFIG_NICE_VIEW_SILAKKA_ART_BLINK_MS);
        }
        return;
    }
    // idle: blink, pose for a minute, back; missing stills are skipped
    if (step == 0 && th->blink) {
        show(th->blink);
        step = 1;
        schedule(BLINK_HOLD_MS);
    } else if (step <= 1 && th->pose) {
        show(th->pose);
        step = 2;
        schedule(POSE_HOLD_MS);
    } else {
        show(th->rest);
        step = 0;
        schedule(CONFIG_NICE_VIEW_SILAKKA_ART_IDLE_MS);
    }
}

/* ---- link state, straight from the Bluetooth stack ------------------- */

static void link_work_cb(struct k_work *work);
K_WORK_DEFINE(link_work, link_work_cb);
static bool link_now;

static void link_work_cb(struct k_work *work) {
    if (link_now == linked) {
        return;
    }
    linked = link_now;
    if (linked) {
        start_clip(th ? th->link : NULL);
    } else {
        clip_stop();
        restart_motion();
    }
}

static bool is_split_link(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.type == BT_CONN_TYPE_LE &&
           info.role == SPLIT_LINK_ROLE;
}

static void on_connected(struct bt_conn *conn, uint8_t err) {
    if (err == 0 && is_split_link(conn)) {
        link_now = true;
        k_work_submit_to_queue(zmk_display_work_q(), &link_work);
    }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (is_split_link(conn)) {
        link_now = false;
        k_work_submit_to_queue(zmk_display_work_q(), &link_work);
    }
}

BT_CONN_CB_DEFINE(stage_conn_cb) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

/* bt_conn_foreach also lists connections still being set up (advertising to
 * hosts, the split link being created), whose role reads as central; only a
 * finished connection counts. The callbacks above need no such check. */
static void count_split_links(struct bt_conn *conn, void *data) {
    struct bt_conn_info info;
    if (is_split_link(conn) && bt_conn_get_info(conn, &info) == 0 &&
        info.state == BT_CONN_STATE_CONNECTED) {
        *(bool *)data = true;
    }
}

/* ---- typing state ---------------------------------------------------- */

/* Activity events arrive on whichever thread noted the key press; LVGL and the
 * clip player only run on the display thread, so hand the change over. */
static void typing_work_cb(struct k_work *work);
K_WORK_DEFINE(typing_work, typing_work_cb);
static bool typing_now = true;

static void typing_work_cb(struct k_work *work) {
    if (typing_now == typing) {
        return;
    }
    typing = typing_now;
    if (typing) {
        memset(samples, 0, sizeof(samples)); // the last rate is from before the break
        clip_stop(); // a key press cuts any clip short
        restart_motion();
    } else if (clip_playing() && !skating) {
        // let it finish; restart_motion() reads the new state afterwards
    } else if (linked) {
        start_clip(th ? th->idle : NULL);
    } else {
        restart_motion();
    }
}

static int activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    typing_now = ev->state == ZMK_ACTIVITY_ACTIVE;
    k_work_submit_to_queue(zmk_display_work_q(), &typing_work);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(stage_activity, activity_listener);
ZMK_SUBSCRIPTION(stage_activity, zmk_activity_state_changed);

/* ---- crouch and shot ------------------------------------------------- */

static void crouch_work_cb(struct k_work *work) {
    if (skating || crouched || (!clip_playing() && linked && typing)) {
        clip_stop();
        restart_motion();
    }
}
K_WORK_DEFINE(crouch_work, crouch_work_cb);

/* A shot doesn't cut another clip short. The left only ever shoots and the
 * right alone draws whether it goes in, so the halves never have to agree. */
static void shot_work_cb(struct k_work *work) {
    if (!th || !th->shot || !linked || (clip_playing() && !skating)) {
        return;
    }
    start_clip(th->miss && sys_rand32_get() % 5 ? th->miss : th->shot);
}
K_WORK_DEFINE(shot_work, shot_work_cb);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
/* Only the central has keycodes: Enter and Ctrl as the keymap resolved them. */
static int keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || ev->usage_page != HID_USAGE_KEY) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    switch (ev->keycode) {
    case HID_USAGE_KEY_KEYBOARD_RETURN_ENTER:
        if (ev->state) {
            k_work_submit_to_queue(zmk_display_work_q(), &shot_work);
        }
        break;
    case HID_USAGE_KEY_KEYBOARD_LEFTCONTROL:
    case HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL:
        crouching = ev->state;
        k_work_submit_to_queue(zmk_display_work_q(), &crouch_work);
        break;
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(stage_keycode, keycode_listener);
ZMK_SUBSCRIPTION(stage_keycode, zmk_keycode_state_changed);

#else
/* The peripheral has no keymap, so it reads Enter's tap off the key itself,
 * the way the balanced hold-tap does: released inside the tapping term, and
 * no other key both pressed and released meanwhile (that's the Nav layer). */
static int64_t enter_down; /* 0: up, or already a hold */

static void read_shot(const struct zmk_position_state_changed *ev) {
    if (ev->position != CONFIG_NICE_VIEW_SILAKKA_SHOT_POSITION) {
        if (!ev->state) {
            enter_down = 0;
        }
        return;
    }
    if (ev->state) {
        enter_down = ev->timestamp;
        return;
    }
    if (enter_down && ev->timestamp - enter_down < CONFIG_NICE_VIEW_SILAKKA_SHOT_TAP_MS) {
        k_work_submit_to_queue(zmk_display_work_q(), &shot_work);
    }
    enter_down = 0;
}
#endif

static int position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (ev->state) {
        atomic_inc(&presses);
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    read_shot(ev);
#endif
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(stage_position, position_listener);
ZMK_SUBSCRIPTION(stage_position, zmk_position_state_changed);

void stage_init(lv_obj_t *img, const struct theme_side *theme) {
    art = img;
    th = theme;
    bool any = false;
    bt_conn_foreach(BT_CONN_TYPE_LE, count_split_links, &any);
    linked = link_now = any;
    if (linked) {
        restart_motion();
    } else {
        start_clip(th ? th->boot : NULL);
    }
}
