/*
 * Description: Servo control and animation functions for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "iot_servo.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "servo.h"
#include "servo_calibration.h"

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
                offset = calibration->left_azi;
                break;
            case SERVO_LEFT_LATITUDE: // Left latitude
                offset = calibration->left_lat;
                break;
            case SERVO_RIGHT_AZIMUTH: // Right azimuth
                offset = calibration->right_azi;
                break;
            case SERVO_RIGHT_LATITUDE: // Right latitude
                offset = calibration->right_lat;
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
