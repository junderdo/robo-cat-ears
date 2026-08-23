/*
 * Description: Host stand-in for the ESP-IDF error type
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HOST_ESP_ERR_H
#define HOST_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1

const char *esp_err_to_name(esp_err_t code);

#endif /* HOST_ESP_ERR_H */
