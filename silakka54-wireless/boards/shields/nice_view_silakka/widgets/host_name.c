/*
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/settings/settings.h>
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

/* Names persist in the settings area under nv_host/<profile>, so a slot keeps
 * its host's name across deep sleep and reboots until the bond is cleared.
 * ZMK runs settings_load() before the display comes up, so the first draw
 * already sees them. */
#define SETTINGS_PREFIX "nv_host"

static void settings_key(int profile, char *out, size_t out_max) {
    snprintf(out, out_max, SETTINGS_PREFIX "/%d", profile);
}

/* Flash writes wait for a radio-free window, so they must never run on the
 * Bluetooth receive thread (where the name arrives) nor on the display
 * thread. Both paths hand the write to the system work queue. */
static char pending_name[HOST_NAME_MAX + 1];
static int pending_profile = -1;
static uint32_t delete_mask;

static void store_work_cb(struct k_work *work) {
    char key[16];
    uint32_t mask = delete_mask;
    delete_mask = 0;
    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; ++i) {
        if (mask & BIT(i)) {
            settings_key(i, key, sizeof(key));
            settings_delete(key);
        }
    }
    int profile = pending_profile;
    pending_profile = -1;
    if (profile >= 0 && profile < NICEVIEW_PROFILE_COUNT) {
        settings_key(profile, key, sizeof(key));
        settings_save_one(key, pending_name, strlen(pending_name));
    }
}
K_WORK_DEFINE(store_work, store_work_cb);

static int host_name_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                  void *cb_arg) {
    if (name == NULL || name[0] < '0' || name[0] > '9') {
        return -ENOENT;
    }
    int profile = atoi(name);
    if (profile < 0 || profile >= NICEVIEW_PROFILE_COUNT || len > HOST_NAME_MAX) {
        return -EINVAL;
    }
    ssize_t rc = read_cb(cb_arg, names[profile], len);
    if (rc < 0) {
        names[profile][0] = '\0';
        return rc;
    }
    names[profile][rc] = '\0';
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(nv_host, SETTINGS_PREFIX, NULL, host_name_settings_set, NULL, NULL);

static struct bt_conn *read_conn;
static int read_profile = -1;
static struct bt_gatt_read_params read_params;
static struct bt_uuid_16 name_uuid = BT_UUID_INIT_16(BT_UUID_GAP_DEVICE_NAME_VAL);

/* The built-in Montserrat fonts only carry ASCII (plus a few symbols). Fold the
 * typographic quotes macOS uses in "Adrien's MacBook" to ASCII and drop
 * anything else outside ASCII. */
static void sanitize_name(const uint8_t *in, size_t len, char *out, size_t out_max) {
    size_t o = 0;
    for (size_t i = 0; i < len && o < out_max;) {
        uint8_t c = in[i];
        if (c < 0x80) {
            if (c >= 0x20) {
                out[o++] = (char)c;
            }
            i++;
            continue;
        }
        size_t seq = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
        if (seq == 3 && i + 2 < len && in[i] == 0xE2 && in[i + 1] == 0x80 &&
            (in[i + 2] == 0x98 || in[i + 2] == 0x99)) {
            out[o++] = '\''; // U+2018 / U+2019
        } else if (seq == 3 && i + 2 < len && in[i] == 0xE2 && in[i + 1] == 0x80 &&
                   (in[i + 2] == 0x9C || in[i + 2] == 0x9D)) {
            out[o++] = '"'; // U+201C / U+201D
        }
        i += seq;
    }
    out[o] = '\0';
}

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
        char fresh[HOST_NAME_MAX + 1];
        sanitize_name(data, length, fresh, HOST_NAME_MAX);
        if (strcmp(fresh, names[read_profile]) != 0) {
            strcpy(names[read_profile], fresh);
            LOG_INF("Host name for profile %d: %s", read_profile, fresh);
            strcpy(pending_name, fresh);
            pending_profile = read_profile;
            k_work_submit(&store_work);
        }
        k_work_submit_to_queue(zmk_display_work_q(), &notify_work);
    }
    finish_read();
    return BT_GATT_ITER_STOP;
}

static void read_work_cb(struct k_work *work) {
    int profile = zmk_ble_active_profile_index();
    if (profile < 0 || profile >= NICEVIEW_PROFILE_COUNT ||
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
    if (profile < 0 || profile >= NICEVIEW_PROFILE_COUNT) {
        return;
    }
    k_work_reschedule(&read_work, HOST_NAME_READ_DELAY);
}

void host_name_clear(int profile) {
    if (profile >= 0 && profile < NICEVIEW_PROFILE_COUNT && names[profile][0] != '\0') {
        names[profile][0] = '\0';
        delete_mask |= BIT(profile);
        k_work_submit(&store_work);
    }
}

const char *host_name_get(int profile) {
    if (profile < 0 || profile >= NICEVIEW_PROFILE_COUNT) {
        return "";
    }
    return names[profile];
}

void host_name_set_update_cb(host_name_update_cb_t cb) { update_cb = cb; }
