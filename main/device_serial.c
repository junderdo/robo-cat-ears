/*
 * Description: The per-unit device serial reported by CAPABILITY (docs/ble-protocol.md S8.1)
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "mbedtls/sha256.h"

#include "device_serial.h"

#define DEVICE_SERIAL_TAG "DEVICE_SERIAL"

// Frozen once any client has persisted a serial. Read device_serial.h before touching this.
#define DEVICE_SERIAL_DOMAIN "milklab-ears-serial-v1"
#define DEVICE_SERIAL_DOMAIN_SIZE (sizeof(DEVICE_SERIAL_DOMAIN) - 1)

#define DEVICE_SERIAL_DIGEST_SIZE 32

void device_serial_derive(const uint8_t *mac, uint8_t *serial)
{
    uint8_t preimage[DEVICE_SERIAL_DOMAIN_SIZE + DEVICE_SERIAL_MAC_SIZE];
    memcpy(preimage, DEVICE_SERIAL_DOMAIN, DEVICE_SERIAL_DOMAIN_SIZE);
    memcpy(preimage + DEVICE_SERIAL_DOMAIN_SIZE, mac, DEVICE_SERIAL_MAC_SIZE);

    uint8_t digest[DEVICE_SERIAL_DIGEST_SIZE];
    if (mbedtls_sha256(preimage, sizeof(preimage), digest, 0) != 0)
    {
        ESP_LOGE(DEVICE_SERIAL_TAG, "SHA-256 failed; reporting the reserved all-zero serial");
        memset(serial, 0, DEVICE_SERIAL_SIZE);
        return;
    }

    memcpy(serial, digest, DEVICE_SERIAL_SIZE);
}

void device_serial_get(uint8_t *serial)
{
    uint8_t mac[DEVICE_SERIAL_MAC_SIZE];
    esp_err_t err = esp_efuse_mac_get_default(mac);

    if (err != ESP_OK)
    {
        ESP_LOGE(DEVICE_SERIAL_TAG, "eFuse MAC read failed (%s); reporting the reserved all-zero serial",
                 esp_err_to_name(err));
        memset(serial, 0, DEVICE_SERIAL_SIZE);
        return;
    }

    device_serial_derive(mac, serial);
}
