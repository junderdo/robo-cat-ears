/*
 * Description: Servo calibration functions for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include "servo_calibration.h"

/**
 * @brief Initialize servo calibration data with default values (all zeros)
 * 
 * @param data Pointer to servo calibration data structure to initialize
 */
void servo_calibration_init(servo_calibration_t *data)
{
    if (data == NULL) {
        return;
    }
    data->left_azi = 0;
    data->left_lat = 0;
    data->right_azi = 0;
    data->right_lat = 0;
}

/**
 * @brief Serialize servo calibration data to byte array
 * 
 * Binary format:
 * - Bytes 0-1: left_azi (int16_t, big-endian)
 * - Bytes 2-3: left_lat (int16_t, big-endian)
 * - Bytes 4-5: right_azi (int16_t, big-endian)
 * - Bytes 6-7: right_lat (int16_t, big-endian)
 * Total: 8 bytes
 * 
 * @param data Pointer to servo calibration data structure to serialize
 * @param output Output buffer (must be at least 8 bytes)
 * @param output_len Pointer to store the length of serialized data
 * @return true if serialization successful, false otherwise
 */
bool servo_calibration_serialize(const servo_calibration_t *data, uint8_t *output, uint16_t *output_len)
{
    if (data == NULL || output == NULL || output_len == NULL) {
        return false;
    }
    
    // Serialize each int16_t in big-endian format
    int offset = 0;
    
    // left_azi
    output[offset++] = (uint8_t)((data->left_azi >> 8) & 0xFF);
    output[offset++] = (uint8_t)(data->left_azi & 0xFF);
    
    // left_lat
    output[offset++] = (uint8_t)((data->left_lat >> 8) & 0xFF);
    output[offset++] = (uint8_t)(data->left_lat & 0xFF);
    
    // right_azi
    output[offset++] = (uint8_t)((data->right_azi >> 8) & 0xFF);
    output[offset++] = (uint8_t)(data->right_azi & 0xFF);
    
    // right_lat
    output[offset++] = (uint8_t)((data->right_lat >> 8) & 0xFF);
    output[offset++] = (uint8_t)(data->right_lat & 0xFF);
    
    *output_len = 8;
    return true;
}

/**
 * @brief Deserialize byte array into servo calibration data structure
 * 
 * @param input Input buffer containing serialized calibration data
 * @param input_len Length of input data in bytes
 * @param data Pointer to servo calibration data structure to populate
 * @return true if deserialization successful, false otherwise
 */
bool servo_calibration_deserialize(const uint8_t *input, uint16_t input_len, servo_calibration_t *data)
{
    if (input == NULL || data == NULL) {
        return false;
    }
    
    // Need exactly 8 bytes
    if (input_len != 8) {
        return false;
    }
    
    int offset = 0;
    
    // left_azi (big-endian)
    data->left_azi = (int16_t)(((uint16_t)input[offset] << 8) | input[offset + 1]);
    offset += 2;
    
    // left_lat (big-endian)
    data->left_lat = (int16_t)(((uint16_t)input[offset] << 8) | input[offset + 1]);
    offset += 2;
    
    // right_azi (big-endian)
    data->right_azi = (int16_t)(((uint16_t)input[offset] << 8) | input[offset + 1]);
    offset += 2;
    
    // right_lat (big-endian)
    data->right_lat = (int16_t)(((uint16_t)input[offset] << 8) | input[offset + 1]);
    offset += 2;
    
    return true;
}

/**
 * @brief Load servo calibration data from NVS
 * 
 * @param calibration Pointer to servo calibration data structure to populate
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no calibration saved
 */
esp_err_t servo_load_calibration(servo_calibration_t *calibration)
{
    if (calibration == NULL) {
        ESP_LOGE(SERVO_TAG, "Invalid calibration pointer");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SERVO_CALIBRATION_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(SERVO_TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        // Initialize to defaults if namespace doesn't exist
        servo_calibration_init(calibration);
        return ESP_OK;
    }

    // Read from NVS
    uint8_t serialized_data[8];
    size_t serialized_len = 8;
    err = nvs_get_blob(nvs_handle, SERVO_CALIBRATION_NVS_KEY, serialized_data, &serialized_len);
    
    nvs_close(nvs_handle);

    servo_calibration_init(calibration);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(SERVO_TAG, "Servo calibration not found in NVS, using defaults");
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(SERVO_TAG, "Failed to read from NVS: %s", esp_err_to_name(err));
    }

    // Deserialize calibration data
    if (!servo_calibration_deserialize(serialized_data, (uint16_t)serialized_len, calibration)) {
        ESP_LOGE(SERVO_TAG, "Failed to deserialize calibration data");
    }

    ESP_LOGI(SERVO_TAG, "Servo calibration: left_azi=%d, left_lat=%d, right_azi=%d, right_lat=%d",
             calibration->left_azi, calibration->left_lat, calibration->right_azi, calibration->right_lat);
    return ESP_OK;
}

/**
 * @brief Save servo calibration data to NVS
 * 
 * @param calibration Pointer to servo calibration data to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t servo_save_calibration(const servo_calibration_t *calibration)
{
    if (calibration == NULL) {
        ESP_LOGE(SERVO_TAG, "Invalid calibration pointer");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SERVO_CALIBRATION_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(SERVO_TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Serialize calibration data
    uint8_t serialized_data[8];
    uint16_t serialized_len;
    if (!servo_calibration_serialize(calibration, serialized_data, &serialized_len)) {
        ESP_LOGE(SERVO_TAG, "Failed to serialize calibration data");
        nvs_close(nvs_handle);
        return ESP_ERR_INVALID_ARG;
    }

    // Write to NVS
    err = nvs_set_blob(nvs_handle, SERVO_CALIBRATION_NVS_KEY, serialized_data, serialized_len);
    if (err != ESP_OK) {
        ESP_LOGE(SERVO_TAG, "Failed to write to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(SERVO_TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(SERVO_TAG, "Servo calibration saved to NVS");
    return ESP_OK;
}