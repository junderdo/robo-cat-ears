/*
 * Description: Test control over the stubbed factory MAC read
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>

#include "esp_err.h"
#include "esp_mac.h"
#include "host_efuse.h"

static uint8_t stub_mac[6];
static esp_err_t stub_result = ESP_OK;

void host_efuse_set_mac(const uint8_t mac[6])
{
    memcpy(stub_mac, mac, sizeof(stub_mac));
    stub_result = ESP_OK;
}

void host_efuse_set_failure(void)
{
    // Poison the buffer too: a caller that ignores the error must not find a
    // plausible MAC left behind by an earlier success.
    memset(stub_mac, 0xa5, sizeof(stub_mac));
    stub_result = ESP_FAIL;
}

esp_err_t esp_efuse_mac_get_default(uint8_t *mac)
{
    memcpy(mac, stub_mac, sizeof(stub_mac));
    return stub_result;
}

const char *esp_err_to_name(esp_err_t code)
{
    return code == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}
