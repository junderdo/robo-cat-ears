/*
 * Description: NVS persistence for stored animations (docs/ble-protocol.md S3)
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "animation_store.h"
#include "types/store_types.h"

#define ANIMATION_STORE_TAG "ANIM_STORE"

// Longest decimal slot index plus terminator
#define SLOT_KEY_SIZE 4

static void slot_key(uint8_t slot, char *key)
{
    snprintf(key, SLOT_KEY_SIZE, "%u", slot);
}

esp_err_t animation_store_init(void)
{
    esp_err_t err = nvs_flash_init_partition(STORE_NVS_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(ANIMATION_STORE_TAG, "Reformatting the %s partition: %s",
                 STORE_NVS_PARTITION, esp_err_to_name(err));
        err = nvs_flash_erase_partition(STORE_NVS_PARTITION);
        if (err == ESP_OK) {
            err = nvs_flash_init_partition(STORE_NVS_PARTITION);
        }
    }
    return err;
}

esp_err_t animation_store_write(uint8_t slot, const uint8_t *record, uint16_t record_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(STORE_NVS_PARTITION, STORE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(ANIMATION_STORE_TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    char key[SLOT_KEY_SIZE];
    slot_key(slot, key);

    err = nvs_set_blob(handle, key, record, record_len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(ANIMATION_STORE_TAG, "Failed to write slot %u: %s", slot, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(ANIMATION_STORE_TAG, "Wrote %u bytes to slot %u", record_len, slot);
    return ESP_OK;
}

esp_err_t animation_store_delete(uint8_t slot)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(STORE_NVS_PARTITION, STORE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(ANIMATION_STORE_TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    char key[SLOT_KEY_SIZE];
    slot_key(slot, key);

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Already empty, which is the state the caller asked for
        nvs_close(handle);
        return ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(ANIMATION_STORE_TAG, "Failed to erase slot %u: %s", slot, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(ANIMATION_STORE_TAG, "Erased slot %u", slot);
    return ESP_OK;
}

esp_err_t animation_store_read(uint8_t slot, uint8_t *record, uint16_t *record_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(STORE_NVS_PARTITION, STORE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // The namespace does not exist until the first write, so a virgin device gets NOT_FOUND
        return err;
    }

    char key[SLOT_KEY_SIZE];
    slot_key(slot, key);

    size_t len = *record_len;
    err = nvs_get_blob(handle, key, record, &len);
    nvs_close(handle);

    if (err != ESP_OK) {
        return err;
    }

    *record_len = (uint16_t)len;
    return ESP_OK;
}
