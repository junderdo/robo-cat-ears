/*
 * Description: Host stand-in for the ESP-IDF factory MAC read
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HOST_ESP_MAC_H
#define HOST_ESP_MAC_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t esp_efuse_mac_get_default(uint8_t *mac);

#endif /* HOST_ESP_MAC_H */
