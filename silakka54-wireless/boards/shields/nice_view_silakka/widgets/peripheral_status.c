/*
 * Right-half status: battery and link state in the top strip, Jack Skellington
 * below. Awake while linked (a short blink every ART_BLINK_MS while this half
 * is being typed on; a blink and a bat flap every ART_IDLE_MS otherwise),
 * asleep in his coffin while not linked (the Zs drift once every ART_IDLE_MS).
 * Every move is one or two redraws; nothing runs between them.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

#include "peripheral_status.h"

LV_IMG_DECLARE(jack_a);   // awake, eyes open
LV_IMG_DECLARE(jack_b);   // awake, blink
LV_IMG_DECLARE(jack_c);   // awake, bat wings down
LV_IMG_DECLARE(coffin_a); // asleep
LV_IMG_DECLARE(coffin_b); // asleep, Zs drifted up

#define BLINK_HOLD_MS 250
#define FLAP_HOLD_MS 600
#define ZZ_HOLD_MS 1000

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static lv_obj_t *art;
static bool linked;
static bool typing = true;
static int step; // position in the current motion sequence

struct peripheral_status_state {
    bool connected;
};

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Link to the central: badge when linked, X otherwise
    if (state->connected) {
        draw_bt_badge(canvas, CANVAS_SIZE - 14, 1);
    } else {
        lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, LV_SYMBOL_CLOSE);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

/* ---- motion ---------------------------------------------------------- */

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
    if (!art) {
        return;
    }
    lv_img_set_src(art, linked ? &jack_a : &coffin_a);
    if (!linked) {
        schedule(CONFIG_NICE_VIEW_SILAKKA_ART_IDLE_MS);
    } else if (typing) {
        schedule(CONFIG_NICE_VIEW_SILAKKA_ART_BLINK_MS);
    } else {
        schedule(CONFIG_NICE_VIEW_SILAKKA_ART_IDLE_MS);
    }
}

static void motion_work_cb(struct k_work *work) {
    if (!art) {
        return;
    }
    if (!linked) {
        // asleep: Zs up for a second, then back
        if (step == 0) {
            lv_img_set_src(art, &coffin_b);
            step = 1;
            schedule(ZZ_HOLD_MS);
        } else {
            lv_img_set_src(art, &coffin_a);
            step = 0;
            schedule(CONFIG_NICE_VIEW_SILAKKA_ART_IDLE_MS);
        }
        return;
    }
    if (typing) {
        // awake and in use: blink
        if (step == 0) {
            lv_img_set_src(art, &jack_b);
            step = 1;
            schedule(BLINK_HOLD_MS);
        } else {
            lv_img_set_src(art, &jack_a);
            step = 0;
            schedule(CONFIG_NICE_VIEW_SILAKKA_ART_BLINK_MS);
        }
        return;
    }
    // awake and idle: blink, bat flaps, back to still
    switch (step) {
    case 0:
        lv_img_set_src(art, &jack_b);
        step = 1;
        schedule(BLINK_HOLD_MS);
        break;
    case 1:
        lv_img_set_src(art, &jack_c);
        step = 2;
        schedule(FLAP_HOLD_MS);
        break;
    default:
        lv_img_set_src(art, &jack_a);
        step = 0;
        schedule(CONFIG_NICE_VIEW_SILAKKA_ART_IDLE_MS);
        break;
    }
}

static int activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    bool now_typing = ev->state == ZMK_ACTIVITY_ACTIVE;
    if (now_typing != typing) {
        typing = now_typing;
        k_work_schedule_for_queue(zmk_display_work_q(), &motion_work, K_NO_WAIT);
        step = 0;
        restart_motion();
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(widget_art_motion, activity_listener);
ZMK_SUBSCRIPTION(widget_art_motion, zmk_activity_state_changed);

/* ---- battery --------------------------------------------------------- */

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

/* ---- link state ------------------------------------------------------ */

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_top(widget->obj, widget->cbuf, &widget->state);

    if (state.connected != linked) {
        linked = state.connected;
        restart_motion();
    }
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    art = lv_img_create(widget->obj);
    lv_obj_align(art, LV_ALIGN_TOP_LEFT, 0, 0);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
    linked = zmk_split_bt_peripheral_is_connected();
    restart_motion();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
