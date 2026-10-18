/*
 * Stage: resting stills with small moves, clips at the big moments.
 * Every move is one or two redraws; nothing runs between them.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/activity.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

#include "stage.h"

#define BLINK_HOLD_MS 250
#define POSE_HOLD_MS 60000
#define ALT_HOLD_MS 1000

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define SPLIT_LINK_ROLE BT_CONN_ROLE_CENTRAL
#else
#define SPLIT_LINK_ROLE BT_CONN_ROLE_PERIPHERAL
#endif

static lv_obj_t *art;
static const struct theme_side *th;
static bool linked;
static bool typing = true;
static int step;

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

static void restart_motion(void) {
    step = 0;
    k_work_cancel_delayable(&motion_work);
    if (!art || !th) {
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
    k_work_cancel_delayable(&motion_work);
    clip_play(art, clip, restart_motion);
}

static void motion_work_cb(struct k_work *work) {
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

static void count_split_links(struct bt_conn *conn, void *data) {
    if (is_split_link(conn)) {
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
        clip_stop(); // a key press cuts any clip short
        restart_motion();
    } else if (clip_playing()) {
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
