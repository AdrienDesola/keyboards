/*
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/ble.h>
#include <zmk/display.h>
#include "util.h"
#include "host_name.h"

/* Apple hosts hand out the real name only once the link is encrypted, so the
 * read waits a little after the connect event. */
#define HOST_NAME_READ_DELAY K_SECONDS(3)

static char names[NICEVIEW_PROFILE_COUNT][HOST_NAME_MAX + 1];
static host_name_update_cb_t update_cb;

static struct bt_conn *read_conn;
static int read_profile = -1;
static struct bt_gatt_read_params read_params;
static struct bt_uuid_16 name_uuid = BT_UUID_INIT_16(BT_UUID_GAP_DEVICE_NAME_VAL);

static void notify_work_cb(struct k_work *work) {
    if (update_cb) {
        update_cb();
    }
}
K_WORK_DEFINE(notify_work, notify_work_cb);

static void finish_read(void) {
    if (read_conn) {
        bt_conn_unref(read_conn);
        read_conn = NULL;
    }
    read_profile = -1;
}

static uint8_t read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                       const void *data, uint16_t length) {
    if (err) {
        LOG_WRN("Host name read failed (ATT err 0x%02x)", err);
    } else if (data && length > 0 && read_profile >= 0 && read_profile < NICEVIEW_PROFILE_COUNT) {
        size_t n = MIN(length, HOST_NAME_MAX);
        memcpy(names[read_profile], data, n);
        names[read_profile][n] = '\0';
        LOG_INF("Host name for profile %d: %s", read_profile, names[read_profile]);
        k_work_submit_to_queue(zmk_display_work_q(), &notify_work);
    }
    finish_read();
    return BT_GATT_ITER_STOP;
}

static void read_work_cb(struct k_work *work) {
    int profile = zmk_ble_active_profile_index();
    if (profile < 0 || profile >= NICEVIEW_PROFILE_COUNT || names[profile][0] != '\0' ||
        !zmk_ble_active_profile_is_connected() || read_conn != NULL) {
        return;
    }

    struct bt_conn *conn = zmk_ble_active_profile_conn(); /* holds a reference */
    if (!conn) {
        return;
    }

    read_conn = conn;
    read_profile = profile;
    memset(&read_params, 0, sizeof(read_params));
    read_params.func = read_cb;
    read_params.handle_count = 0;
    read_params.by_uuid.uuid = &name_uuid.uuid;
    read_params.by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    read_params.by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;

    int rc = bt_gatt_read(conn, &read_params);
    if (rc) {
        LOG_WRN("Host name read not started (%d)", rc);
        finish_read();
    }
}
K_WORK_DELAYABLE_DEFINE(read_work, read_work_cb);

void host_name_request(int profile) {
    if (profile < 0 || profile >= NICEVIEW_PROFILE_COUNT || names[profile][0] != '\0') {
        return;
    }
    k_work_reschedule(&read_work, HOST_NAME_READ_DELAY);
}

void host_name_clear(int profile) {
    if (profile >= 0 && profile < NICEVIEW_PROFILE_COUNT) {
        names[profile][0] = '\0';
    }
}

const char *host_name_get(int profile) {
    if (profile < 0 || profile >= NICEVIEW_PROFILE_COUNT) {
        return "";
    }
    return names[profile];
}

void host_name_set_update_cb(host_name_update_cb_t cb) { update_cb = cb; }
