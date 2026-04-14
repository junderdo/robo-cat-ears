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

#define SERVO_TAG "SERVO"

esp_err_t reset_servos(void)
{
    ESP_LOGI(SERVO_TAG, "Resetting servos to center position (90 degrees)");
    esp_err_t err = ESP_OK;
    for (int i = 0; i < 4; i++)
    {
        err = iot_servo_write_angle(LEDC_LOW_SPEED_MODE, i, 90.0);
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
    ESP_LOGI(SERVO_TAG, "Initializing 4 servos on GPIO 2-5");
    
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

// TODO: Implement general animation function that can take in parameter for different patterns instead of hardcoding each one
// return esp_err_t such that errors can be propagated and handled in the main app instead of just logging and continuing

void do_animation_1(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 1: Happy wiggle");
    
    // Start with ears up and perked
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 80));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 100));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90));
    vTaskDelay(200 / portTICK_PERIOD_MS);
    
    // Quick back and forth movements with rotation
    for (int i = 0; i < 2; i++) {
        // Move ears back with slight outward rotation
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 85));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 95));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 75));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 105));
        vTaskDelay(200 / portTICK_PERIOD_MS);
        
        // Move ears forward with inward rotation
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 135));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 45));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 105));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 75));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    // Big happy wiggle
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 145));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 35));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 110));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 70));
    vTaskDelay(300 / portTICK_PERIOD_MS);
    
    // Return to center with alternating movement
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 80));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90));
    vTaskDelay(150 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 100));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90));
    vTaskDelay(150 / portTICK_PERIOD_MS);
    
    ESP_LOGI(SERVO_TAG, "Animation 1 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_2(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 2: Sad");
    for (int i = 0; i < 3; i++) {
        // move ears forward/down
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 145));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 35));
        // rotate outward
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 30));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 150));
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(SERVO_TAG, "Animation 2 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_3(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 3: Playful bounce");
    // Very rapid playful movements - extended version
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
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 95));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 85));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 105));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 75));
        vTaskDelay(180 / portTICK_PERIOD_MS);
        
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 125));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 55));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 75));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 105));
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
    for (int i = 0; i < 3; i++) {
        // Left ear down, right ear up
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 130)); // Left ear down
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 80)); // Right ear up
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 100)); // Slight tilt
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 80)); // Slight tilt
        vTaskDelay(600 / portTICK_PERIOD_MS);
        
        // Right ear down, left ear up
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 85)); // Left ear up
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 60)); // Right ear down
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 80)); // Slight tilt
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 100)); // Slight tilt
        vTaskDelay(600 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(SERVO_TAG, "Animation 4 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_5(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 5: Listening/Radar");
    // Ears rotate side to side like listening/scanning
    for (int i = 0; i < 2; i++) {
        // Rotate both ears outward
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 60)); // Right ear rotate out
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 120)); // Left ear rotate out
        vTaskDelay(400 / portTICK_PERIOD_MS);
        
        // Rotate both ears inward
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 120)); // Right ear rotate in
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 60)); // Left ear rotate in
        vTaskDelay(400 / portTICK_PERIOD_MS);
        
        // Back to center
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    // Independent rotation - one ear tracks left, other right
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 60)); // Right ear left
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 60)); // Left ear left
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 120)); // Right ear right
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 120)); // Left ear right
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    ESP_LOGI(SERVO_TAG, "Animation 5 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_6(void)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 6: Excited twitch");
    // Rapid excited movements
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
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 100));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 80));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 110));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 70));
        vTaskDelay(200 / portTICK_PERIOD_MS);
        
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 130));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 50));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 70));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 110));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(SERVO_TAG, "Animation 6 complete");
    ESP_ERROR_CHECK(reset_servos());
}
