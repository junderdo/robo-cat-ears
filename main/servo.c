/*
 * Description: Servo control and animation functions for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "servo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "iot_servo.h"
#include "nvs_flash.h"
#include "nvs.h"

#define SERVO_TAG "SERVO"
#define SERVO_CALIBRATION_NVS_NAMESPACE "servo_cal"
#define SERVO_CALIBRATION_NVS_KEY "calibration"

esp_err_t reset_servos(void)
{
    // Load calibration data if available and apply offsets
    servo_calibration_t calibration;
    ESP_ERROR_CHECK(servo_load_calibration(&calibration));

    ESP_LOGI(SERVO_TAG, "Resetting servos to center position (90 degrees)");
    esp_err_t err = ESP_OK;
    for (int i = 0; i < 4; i++)
    {
        err = move_servo(i, 90.0, &calibration);
        if (err) {
            ESP_LOGE(SERVO_TAG, "Failed to reset servo %d: %s", i, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(SERVO_TAG, "Servos reset to center position");
    return err;
}

esp_err_t init_servos(void)
{
    ESP_LOGI(SERVO_TAG, "Initializing 4 servos on GPIO 1-4");
    
    // Configure 4 servos
    servo_config_t servo_cfg = {
        .max_angle = SERVO_MAX_ANGLE,
        .min_width_us = SERVO_MIN_PULSEWIDTH_US,
        .max_width_us = SERVO_MAX_PULSEWIDTH_US,
        .freq = SERVO_FREQ,
        .timer_number = LEDC_TIMER_0,
        .channels = {
            .servo_pin = {SERVO_PULSE_GPIO_0, SERVO_PULSE_GPIO_1, SERVO_PULSE_GPIO_2, SERVO_PULSE_GPIO_3},
            .ch = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3},
        },
        .channel_number = 4,
    };
    
    // Initialize servos
    esp_err_t err = iot_servo_init(LEDC_LOW_SPEED_MODE, &servo_cfg);
    if (err) {
        ESP_LOGE(SERVO_TAG, "Failed to initialize servos: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(SERVO_TAG, "All 4 servos initialized at center position (90 degrees)");

    // Reset servos to center position
    return reset_servos();
}

esp_err_t move_servo(uint8_t channel, float angle, servo_calibration_t *calibration)
{
    ESP_LOGI(SERVO_TAG, "Moving servo channel %d to angle %.1f", channel, angle);
    
    float offset = 0;
    float calibrated_angle = angle;
    if (calibration != NULL) {
        // Apply calibration offsets based on channel
        switch (channel) {
            case SERVO_LEFT_AZIMUTH: // Left azimuth
                offset = calibration->left_azi / 1000.0f * SERVO_MAX_ANGLE;
                break;
            case SERVO_LEFT_LATITUDE: // Left latitude
                offset = calibration->left_lat / 1000.0f * SERVO_MAX_ANGLE;
                break;
            case SERVO_RIGHT_AZIMUTH: // Right azimuth
                offset = calibration->right_azi / 1000.0f * SERVO_MAX_ANGLE;
                break;
            case SERVO_RIGHT_LATITUDE: // Right latitude
                offset = calibration->right_lat / 1000.0f * SERVO_MAX_ANGLE;
                break;
            default:
                ESP_LOGW(SERVO_TAG, "Invalid servo channel: %d", channel);
                break;
        }
        ESP_LOGI(SERVO_TAG, "servo offset for channel %d: %.1f", channel, offset);

        calibrated_angle += offset;
        // Clamp calibrated angle to valid range
        if (calibrated_angle < 0) calibrated_angle = 0;
        if (calibrated_angle > SERVO_MAX_ANGLE) calibrated_angle = SERVO_MAX_ANGLE;
    }

    return iot_servo_write_angle(LEDC_LOW_SPEED_MODE, channel, calibrated_angle);
}

// TODO: Implement general animation function that can take in parameter for different patterns instead of hardcoding each one
// return esp_err_t such that errors can be propagated and handled in the main app instead of just logging and continuing

void do_animation_1(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 1: Happy wiggle");

    // Load calibration data if available and apply offsets
    servo_calibration_t calibration;
    ESP_ERROR_CHECK(servo_load_calibration(&calibration));
    
    // Start with ears up and perked
    ESP_ERROR_CHECK(move_servo(1, 80, &calibration));
    ESP_ERROR_CHECK(move_servo(3, 100, &calibration));
    ESP_ERROR_CHECK(move_servo(0, 90, &calibration));
    ESP_ERROR_CHECK(move_servo(2, 90, &calibration));
    vTaskDelay(200 / portTICK_PERIOD_MS);
    
    // Quick back and forth movements with rotation
    for (int i = 0; i < 2; i++) {
        // Move ears back with slight outward rotation
        ESP_ERROR_CHECK(move_servo(1, 85, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 95, &calibration));
        ESP_ERROR_CHECK(move_servo(0, 75, &calibration));
        ESP_ERROR_CHECK(move_servo(2, 105, &calibration));
        vTaskDelay(200 / portTICK_PERIOD_MS);
        
        // Move ears forward with inward rotation
        ESP_ERROR_CHECK(move_servo(1, 135, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 45, &calibration));
        ESP_ERROR_CHECK(move_servo(0, 105, &calibration));
        ESP_ERROR_CHECK(move_servo(2, 75, &calibration));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    // Big happy wiggle
    ESP_ERROR_CHECK(move_servo(1, 145, &calibration));
    ESP_ERROR_CHECK(move_servo(3, 35, &calibration));
    ESP_ERROR_CHECK(move_servo(0, 110, &calibration));
    ESP_ERROR_CHECK(move_servo(2, 70, &calibration));
    vTaskDelay(300 / portTICK_PERIOD_MS);
    
    // Return to center with alternating movement
    ESP_ERROR_CHECK(move_servo(1, 80, &calibration));
    ESP_ERROR_CHECK(move_servo(0, 90, &calibration));
    vTaskDelay(150 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(move_servo(3, 100, &calibration));
    ESP_ERROR_CHECK(move_servo(2, 90, &calibration));
    vTaskDelay(150 / portTICK_PERIOD_MS);
    
    ESP_LOGI(SERVO_TAG, "Animation 1 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_2(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 2: Sad");

    // Load calibration data if available and apply offsets
    servo_calibration_t calibration;
    ESP_ERROR_CHECK(servo_load_calibration(&calibration));

    for (int i = 0; i < 3; i++) {
        // move ears forward/down
        ESP_ERROR_CHECK(move_servo(1, 145, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 35, &calibration));
        // rotate outward
        ESP_ERROR_CHECK(move_servo(0, 30, &calibration));
        ESP_ERROR_CHECK(move_servo(2, 150, &calibration));
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(SERVO_TAG, "Animation 2 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_3(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 3: Playful bounce");
    // Very rapid playful movements - extended version

    // Load calibration data if available and apply offsets
    servo_calibration_t calibration;
    ESP_ERROR_CHECK(servo_load_calibration(&calibration));

    for (int i = 0; i < 8; i++) {
        // Quick bouncy positions
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 80 + (i % 4) * 12));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 100 - (i % 4) * 12));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90 + ((i % 2) * 25 - 12)));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90 + ((i % 2) * 25 - 12)));
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Medium bounces
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(move_servo(1, 95, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 85, &calibration));
        ESP_ERROR_CHECK(move_servo(0, 105, &calibration));
        ESP_ERROR_CHECK(move_servo(2, 75, &calibration));
        vTaskDelay(180 / portTICK_PERIOD_MS);
        
        ESP_ERROR_CHECK(move_servo(1, 125, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 55, &calibration));
        ESP_ERROR_CHECK(move_servo(0, 75, &calibration));
        ESP_ERROR_CHECK(move_servo(2, 105, &calibration));
        vTaskDelay(180 / portTICK_PERIOD_MS);
    }
    
    // Final rapid flutter
    for (int i = 0; i < 4; i++) {
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 85 + (i % 2) * 20));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 95 - (i % 2) * 20));
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(SERVO_TAG, "Animation 3 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_4(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 4: Curious tilt");
    // Alternate ears - one up, one down

    // Load calibration data if available and apply offsets
    servo_calibration_t calibration;
    ESP_ERROR_CHECK(servo_load_calibration(&calibration));

    for (int i = 0; i < 3; i++) {
        // Left ear down, right ear up
        ESP_ERROR_CHECK(move_servo(1, 130, &calibration)); // Left ear down
        ESP_ERROR_CHECK(move_servo(3, 80, &calibration)); // Right ear up
        ESP_ERROR_CHECK(move_servo(0, 100, &calibration)); // Slight tilt
        ESP_ERROR_CHECK(move_servo(2, 80, &calibration)); // Slight tilt
        vTaskDelay(600 / portTICK_PERIOD_MS);
        
        // Right ear down, left ear up
        ESP_ERROR_CHECK(move_servo(1, 85, &calibration)); // Left ear up
        ESP_ERROR_CHECK(move_servo(3, 60, &calibration)); // Right ear down
        ESP_ERROR_CHECK(move_servo(0, 80, &calibration)); // Slight tilt
        ESP_ERROR_CHECK(move_servo(2, 100, &calibration)); // Slight tilt
        vTaskDelay(600 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(SERVO_TAG, "Animation 4 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_5(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 5: Listening/Radar");
    // Ears rotate side to side like listening/scanning

    // Load calibration data if available and apply offsets
    servo_calibration_t calibration;
    ESP_ERROR_CHECK(servo_load_calibration(&calibration));

    for (int i = 0; i < 2; i++) {
        // Rotate both ears outward
        ESP_ERROR_CHECK(move_servo(0, 60, &calibration)); // Right ear rotate out
        ESP_ERROR_CHECK(move_servo(2, 120, &calibration)); // Left ear rotate out
        vTaskDelay(400 / portTICK_PERIOD_MS);
        
        // Rotate both ears inward
        ESP_ERROR_CHECK(move_servo(0, 120, &calibration)); // Right ear rotate in
        ESP_ERROR_CHECK(move_servo(2, 60, &calibration)); // Left ear rotate in
        vTaskDelay(400 / portTICK_PERIOD_MS);
        
        // Back to center
        ESP_ERROR_CHECK(move_servo(0, 90, &calibration));
        ESP_ERROR_CHECK(move_servo(2, 90, &calibration));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    // Independent rotation - one ear tracks left, other right
    ESP_ERROR_CHECK(move_servo(0, 60, &calibration)); // Right ear left
    ESP_ERROR_CHECK(move_servo(2, 60, &calibration)); // Left ear left
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    ESP_ERROR_CHECK(move_servo(0, 120, &calibration)); // Right ear right
    ESP_ERROR_CHECK(move_servo(2, 120, &calibration)); // Left ear right
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    ESP_LOGI(SERVO_TAG, "Animation 5 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_6(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 6: Excited twitch");
    // Rapid excited movements

    // Load calibration data if available and apply offsets
    servo_calibration_t calibration;
    ESP_ERROR_CHECK(servo_load_calibration(&calibration));

    for (int i = 0; i < 6; i++) {
        // Quick twitch positions
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 80 + (i % 3) * 15));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 100 - (i % 3) * 15));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90 + ((i % 2) * 20 - 10)));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90 + ((i % 2) * 20 - 10)));
        vTaskDelay(120 / portTICK_PERIOD_MS);
    }
    
    // Big excited wiggle
    for (int i = 0; i < 2; i++) {
        ESP_ERROR_CHECK(move_servo(1, 100, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 80, &calibration));
        ESP_ERROR_CHECK(move_servo(0, 110, &calibration));
        ESP_ERROR_CHECK(move_servo(2, 70, &calibration));
        vTaskDelay(200 / portTICK_PERIOD_MS);
        
        ESP_ERROR_CHECK(move_servo(1, 130, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 50, &calibration));
        ESP_ERROR_CHECK(move_servo(0, 70, &calibration));
        ESP_ERROR_CHECK(move_servo(2, 110, &calibration));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(SERVO_TAG, "Animation 6 complete");
    ESP_ERROR_CHECK(reset_servos());
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
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(SERVO_TAG, "Servo calibration loaded from NVS: left_azi=%d, left_lat=%d, right_azi=%d, right_lat=%d",
             calibration->left_azi, calibration->left_lat, calibration->right_azi, calibration->right_lat);
    return ESP_OK;
}
