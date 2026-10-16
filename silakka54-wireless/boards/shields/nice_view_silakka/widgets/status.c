/*
 * Left-half status, top to bottom as the user sees it: battery and output in
 * the first rows, the active Bluetooth profile ring with its host name below,
 * the theme's stage (80 px, stage.c, themes/<name>/left/), the layer name in a
 * thin strip at the bottom.
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include "host_name.h"
#include "stage.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/activity.h>
#include <zmk/events/activity_state_changed.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static lv_coord_t text_width(const char *text, const lv_font_t *font);
static void draw_profile(lv_obj_t *canvas, const struct status_state *state);

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profiles_connected[NICEVIEW_PROFILE_COUNT];
    bool profiles_bonded[NICEVIEW_PROFILE_COUNT];
};

struct layer_status_state {
    zmk_keymap_layer_index_t index;
    const char *label;
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

    // Draw output status: USB glyph, or the Bluetooth badge when the profile is linked
    char output_text[10] = {};

    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded) {
            if (!state->active_profile_connected) {
                strcat(output_text, LV_SYMBOL_CLOSE);
            }
        } else {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    }

    if (output_text[0] != '\0') {
        lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, output_text);
    } else if (state->selected_endpoint.transport == ZMK_TRANSPORT_BLE) {
        draw_bt_badge(canvas, CANVAS_SIZE - 14, 1);
    }

    draw_profile(canvas, state);

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static char profile_names[NICEVIEW_PROFILE_COUNT][16];

static void parse_profile_names(void) {
    char buf[sizeof(CONFIG_NICE_VIEW_SILAKKA_PROFILE_NAMES)];
    strncpy(buf, CONFIG_NICE_VIEW_SILAKKA_PROFILE_NAMES, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    char *cursor = buf;
    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; i++) {
        char *comma = strchr(cursor, ',');
        if (comma) {
            *comma = '\0';
        }
        strncpy(profile_names[i], cursor, sizeof(profile_names[i]) - 1);
        profile_names[i][sizeof(profile_names[i]) - 1] = '\0';
        if (!comma) {
            break;
        }
        cursor = comma + 1;
    }
}

#define NAME_GAP 16

/* Screen rows as the user sees it: 68 status, 80 stage, LAYER_STRIP layer. */
#define LAYER_STRIP 12

static int name_scroll_offset;
static int name_scroll_remaining; /* pixels left in the single pass, 0 = parked */
static char scrolled_name[32];
static bool keyboard_active = true;
static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state);

static void scroll_work_cb(struct k_work *work) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        draw_top(widget->obj, widget->cbuf, &widget->state);
    }
}
K_WORK_DELAYABLE_DEFINE(scroll_work, scroll_work_cb);

static const char *middle_name(const struct status_state *state, int i) {
    const char *host = host_name_get(i);
    if (host[0] != '\0') {
        return host;
    }
    if (!state->profiles_bonded[i]) {
        return "open";
    }
    return profile_names[i];
}

static lv_coord_t text_width(const char *text, const lv_font_t *font) {
    lv_point_t size;
    lv_txt_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/* Active profile under the battery row: ring shows its state, number inside,
 * host name below. Drawn into the top square, which the caller rotates. */
static void draw_profile(lv_obj_t *canvas, const struct status_state *state) {
    lv_draw_arc_dsc_t arc_dsc;
    init_arc_dsc(&arc_dsc, LVGL_FOREGROUND, 2);
    lv_draw_arc_dsc_t arc_dsc_filled;
    init_arc_dsc(&arc_dsc_filled, LVGL_FOREGROUND, 9);
    lv_draw_label_dsc_t label_dsc_black;
    init_label_dsc(&label_dsc_black, LVGL_BACKGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_dsc_name;
    init_label_dsc(&label_dsc_name, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    const int i = state->active_profile_index;
    const int cx = 34, cy = 33;
    const int name_y = 50;

    if (i >= 0 && i < NICEVIEW_PROFILE_COUNT) {
        if (state->profiles_connected[i]) {
            lv_canvas_draw_arc(canvas, cx, cy, 15, 0, 360, &arc_dsc);
        } else if (state->profiles_bonded[i]) {
            const int segments = 8;
            const int gap = 20;
            for (int j = 0; j < segments; ++j)
                lv_canvas_draw_arc(canvas, cx, cy, 15, 360. / segments * j + gap / 2.0,
                                   360. / segments * (j + 1) - gap / 2.0, &arc_dsc);
        }
        lv_canvas_draw_arc(canvas, cx, cy, 9, 0, 359, &arc_dsc_filled);

        char label[2];
        snprintf(label, sizeof(label), "%d", i + 1);
        lv_canvas_draw_text(canvas, cx - 8, cy - 10, 16, &label_dsc_black, label);

        const char *name = middle_name(state, i);
        lv_coord_t w = text_width(name, &lv_font_montserrat_14);
        if (strncmp(name, scrolled_name, sizeof(scrolled_name)) != 0) {
            // New name: one full pass, then park at the start
            strncpy(scrolled_name, name, sizeof(scrolled_name) - 1);
            scrolled_name[sizeof(scrolled_name) - 1] = '\0';
            name_scroll_offset = 0;
            name_scroll_remaining = w + NAME_GAP;
        }
        if (w <= CANVAS_SIZE || CONFIG_NICE_VIEW_SILAKKA_NAME_SCROLL_MS == 0) {
            lv_canvas_draw_text(canvas, 0, name_y, CANVAS_SIZE, &label_dsc_name, name);
        } else {
            // Wider than the screen: marquee, the text drawn twice with a gap so it loops
            lv_draw_label_dsc_t label_dsc_left;
            init_label_dsc(&label_dsc_left, LVGL_FOREGROUND, &lv_font_montserrat_14,
                           LV_TEXT_ALIGN_LEFT);
            int period = w + NAME_GAP;
            name_scroll_offset %= period;
            lv_canvas_draw_text(canvas, -name_scroll_offset, name_y, w + 2, &label_dsc_left, name);
            lv_canvas_draw_text(canvas, period - name_scroll_offset, name_y, w + 2, &label_dsc_left,
                                name);
            if (keyboard_active && name_scroll_remaining > 0) {
                name_scroll_offset += 2;
                name_scroll_remaining -= 2;
                if (name_scroll_remaining <= 0) {
                    name_scroll_offset = 0; // last step redraws parked at the start
                    name_scroll_remaining = 0;
                }
                k_work_schedule_for_queue(zmk_display_work_q(), &scroll_work,
                                          K_MSEC(CONFIG_NICE_VIEW_SILAKKA_NAME_SCROLL_MS));
            }
        }
    }
}

/* Layer name in the bottom strip, small: only its first LAYER_STRIP rows show. */
static void draw_bottom(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw layer
    if (state->layer_label == NULL || strlen(state->layer_label) == 0) {
        char text[10] = {};

        sprintf(text, "LAYER %i", state->layer_index);

        lv_canvas_draw_text(canvas, 0, 2, 68, &label_dsc, text);
    } else {
        lv_canvas_draw_text(canvas, 0, 2, 68, &label_dsc, state->layer_label);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

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
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
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

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;
    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; ++i) {
        widget->state.profiles_connected[i] = state->profiles_connected[i];
        widget->state.profiles_bonded[i] = state->profiles_bonded[i];
    }

    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; ++i) {
        if (!state->profiles_bonded[i]) {
            host_name_clear(i);
        }
    }
    if (state->active_profile_connected) {
        host_name_request(state->active_profile_index);
    }

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void host_name_updated(void) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        draw_top(widget->obj, widget->cbuf, &widget->state);
    }
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    struct output_status_state state = {
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
    for (int i = 0; i < MIN(NICEVIEW_PROFILE_COUNT, ZMK_BLE_PROFILE_COUNT); ++i) {
        state.profiles_connected[i] = zmk_ble_profile_is_connected(i);
        state.profiles_bonded[i] = !zmk_ble_profile_is_open(i);
    }
    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state) {
    widget->state.layer_index = state.index;
    widget->state.layer_label = state.label;

    draw_bottom(widget->obj, widget->cbuf2, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

static int activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    keyboard_active = ev->state == ZMK_ACTIVITY_ACTIVE;
    if (keyboard_active) {
        k_work_schedule_for_queue(zmk_display_work_q(), &scroll_work, K_NO_WAIT);
    } else {
        k_work_cancel_delayable(&scroll_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(widget_name_scroll, activity_listener);
ZMK_SUBSCRIPTION(widget_name_scroll, zmk_activity_state_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_t *bottom = lv_canvas_create(widget->obj);
    lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, LAYER_STRIP - CANVAS_SIZE, 0);
    lv_canvas_set_buffer(bottom, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_obj_t *stage = lv_img_create(widget->obj);
    lv_obj_align(stage, LV_ALIGN_TOP_LEFT, LAYER_STRIP, 0);

    parse_profile_names();
    host_name_set_update_cb(host_name_updated);
    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_output_status_init();
    widget_layer_status_init();
    stage_init(stage, &theme_side);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
