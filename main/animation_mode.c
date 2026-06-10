/*
 * Description: Animation mode NVS storage and BLE serialization functions
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <stdbool.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include "animation_mode.h"

/**
 * @brief Initialize animation mode data with default values
 *
 * @param data Pointer to animation mode data structure to initialize
 */
void animation_mode_init(animation_mode_t *data)
{
    if (data == NULL) {
        return;
    }
    data->mode_id = 0;
    data->frequency = 0;
}

/**
 * @brief Serialize animation mode data to byte array
 *
 * Binary format:
 * - Bytes 0-1: mode_id (int16_t, big-endian)
 * - Bytes 2-3: frequency (int16_t, big-endian)
 * Total: 4 bytes
 *
 * @param data Pointer to animation mode data structure to serialize
 * @param output Output buffer (must be at least 4 bytes)
 * @param output_len Pointer to store the length of serialized data
 * @return true if serialization successful, false otherwise
 */
bool animation_mode_serialize(const animation_mode_t *data, uint8_t *output, uint16_t *output_len)
{
    if (data == NULL || output == NULL || output_len == NULL) {
        return false;
    }

    int offset = 0;

    // mode_id (big-endian)
    output[offset++] = (uint8_t)((data->mode_id >> 8) & 0xFF);
    output[offset++] = (uint8_t)(data->mode_id & 0xFF);

    // frequency (big-endian)
    output[offset++] = (uint8_t)((data->frequency >> 8) & 0xFF);
    output[offset++] = (uint8_t)(data->frequency & 0xFF);

    *output_len = 4;
    return true;
}

/**
 * @brief Deserialize byte array into animation mode data structure
 *
 * @param input Input buffer containing serialized animation mode data
 * @param input_len Length of input data in bytes
 * @param data Pointer to animation mode data structure to populate
 * @return true if deserialization successful, false otherwise
 */
bool animation_mode_deserialize(const uint8_t *input, uint16_t input_len, animation_mode_t *data)
{
    if (input == NULL || data == NULL) {
        return false;
    }

    // Need exactly 4 bytes
    if (input_len != 4) {
        return false;
    }

    int offset = 0;

    // mode_id (big-endian)
    data->mode_id = (int16_t)(((uint16_t)input[offset] << 8) | input[offset + 1]);
    offset += 2;

    // frequency (big-endian)
    data->frequency = (int16_t)(((uint16_t)input[offset] << 8) | input[offset + 1]);
    offset += 2;

    return true;
}

/**
 * @brief Save animation mode data to NVS
 *
 * @param mode Pointer to animation mode data to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t animation_mode_save(const animation_mode_t *mode)
{
    if (mode == NULL) {
        ESP_LOGE(ANIMATION_MODE_TAG, "Invalid mode pointer");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ANIMATION_MODE_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(ANIMATION_MODE_TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t serialized_data[4];
    uint16_t serialized_len;
    if (!animation_mode_serialize(mode, serialized_data, &serialized_len)) {
        ESP_LOGE(ANIMATION_MODE_TAG, "Failed to serialize animation mode data");
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_set_blob(nvs_handle, ANIMATION_MODE_NVS_KEY, serialized_data, serialized_len);
    if (err != ESP_OK) {
        ESP_LOGE(ANIMATION_MODE_TAG, "Failed to write to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(ANIMATION_MODE_TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(ANIMATION_MODE_TAG, "Animation mode saved to NVS");
    return ESP_OK;
}

/**
 * @brief Load animation mode data from NVS
 *
 * @param mode Pointer to animation mode data structure to populate
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t animation_mode_load(animation_mode_t *mode)
{
    if (mode == NULL) {
        ESP_LOGE(ANIMATION_MODE_TAG, "Invalid mode pointer");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ANIMATION_MODE_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(ANIMATION_MODE_TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        animation_mode_init(mode);
        return ESP_OK;
    }

    uint8_t serialized_data[4];
    size_t serialized_len = 4;
    err = nvs_get_blob(nvs_handle, ANIMATION_MODE_NVS_KEY, serialized_data, &serialized_len);

    nvs_close(nvs_handle);

    animation_mode_init(mode);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(ANIMATION_MODE_TAG, "Animation mode not found in NVS, using defaults");
        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGE(ANIMATION_MODE_TAG, "Failed to read from NVS: %s", esp_err_to_name(err));
        return err;
    }

    if (!animation_mode_deserialize(serialized_data, (uint16_t)serialized_len, mode)) {
        ESP_LOGE(ANIMATION_MODE_TAG, "Failed to deserialize animation mode data");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(ANIMATION_MODE_TAG, "Animation mode loaded: mode_id=%d, frequency=%d",
             mode->mode_id, mode->frequency);
    return ESP_OK;
}
