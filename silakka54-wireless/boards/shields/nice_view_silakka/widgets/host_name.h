/*
 * Reads the connected host's GAP "Device Name" over GATT and caches it per
 * Bluetooth profile, so the status screen can show "MacBook" instead of "1".
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#define HOST_NAME_MAX 24

typedef void (*host_name_update_cb_t)(void);

/* Called from the display work queue. Starts a read for the active profile if
 * it is connected and no name is cached yet. */
void host_name_request(int profile);
void host_name_clear(int profile);
const char *host_name_get(int profile);
void host_name_set_update_cb(host_name_update_cb_t cb);
