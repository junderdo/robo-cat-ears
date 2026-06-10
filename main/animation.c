/*
 * Description: Animation functions for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include <stdlib.h>

#include "animation.h"
#include "animation_mode.h"
#include "servo.h"
#include "servo_calibration.h"

/*
    TODO: add animation easing and interpolation functions for smoother, more natural movements.
    Maybe implement a simple keyframe system where each animation is defined by a series of
    key positions and durations, and the code interpolates between them.
*/

static TaskHandle_t animation_mode_task_handle = NULL;

static void animation_mode_task(void *arg)
{
    int16_t frequency = *(int16_t *)arg;
    free(arg);

    // Convert frequency (times/hour) to delay between animations in ms.
    // Add a small random jitter (+/- 10%) so the timing feels natural.
    uint32_t interval_ms = (uint32_t)((3600000UL) / (uint32_t)frequency);

    ESP_LOGI(SERVO_TAG, "Animation mode task started: frequency=%d/hr, interval=%lu ms",
             frequency, (unsigned long)interval_ms);

    while (1) {
        // Wait the interval, split into 1 s ticks so stop is responsive
        uint32_t remaining = interval_ms;
        while (remaining > 0) {
            uint32_t sleep = remaining > 1000 ? 1000 : remaining;
            vTaskDelay(sleep / portTICK_PERIOD_MS);
            remaining -= sleep;
        }

        // Pick animation 1 or 2 at random
        int8_t anim_id = (esp_random() & 1) ? 1 : 2;
        ESP_LOGI(SERVO_TAG, "Animation mode: triggering animation %d", anim_id);
        do_animation(anim_id);
    }
}

void start_animation_mode(void)
{
    if (animation_mode_task_handle != NULL) {
        ESP_LOGW(SERVO_TAG, "Animation mode already running");
        return;
    }

    animation_mode_t mode;
    esp_err_t err = animation_mode_load(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(SERVO_TAG, "Failed to load animation mode: %s", esp_err_to_name(err));
        return;
    }

    if (mode.frequency <= 0) {
        ESP_LOGI(SERVO_TAG, "Animation mode frequency is 0, not starting task");
        return;
    }

    // Heap-allocate the frequency so the task has a stable copy
    int16_t *freq_arg = malloc(sizeof(int16_t));
    if (freq_arg == NULL) {
        ESP_LOGE(SERVO_TAG, "Failed to allocate frequency argument");
        return;
    }
    *freq_arg = mode.frequency;

    BaseType_t ret = xTaskCreate(
        animation_mode_task,
        "anim_mode",
        4096,
        freq_arg,
        3,
        &animation_mode_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(SERVO_TAG, "Failed to create animation mode task");
        free(freq_arg);
        animation_mode_task_handle = NULL;
    } else {
        ESP_LOGI(SERVO_TAG, "Animation mode task started (frequency=%d/hr)", mode.frequency);
        // Immediately run one animation as activation feedback
        int8_t anim_id = (esp_random() & 1) ? 1 : 2;
        ESP_LOGI(SERVO_TAG, "Animation mode: immediate trigger animation %d", anim_id);
        do_animation(anim_id);
    }
}

void stop_animation_mode(void)
{
    if (animation_mode_task_handle == NULL) {
        ESP_LOGW(SERVO_TAG, "Animation mode task is not running");
        return;
    }

    vTaskDelete(animation_mode_task_handle);
    animation_mode_task_handle = NULL;
    ESP_LOGI(SERVO_TAG, "Animation mode task stopped");
}

void do_animation(int8_t animation_id)
{
    // Load calibration data if available and apply offsets
    servo_calibration_t calibration;
    ESP_ERROR_CHECK(servo_load_calibration(&calibration));

    switch (animation_id) {
        case 1:
            do_animation_1(calibration);
            break;
        case 2:
            do_animation_2(calibration);
            break;
        case 3:
            do_animation_3(calibration);
            break;
        case 4:
            do_animation_4(calibration);
            break;
        case 5:
            do_animation_5(calibration);
            break;
        case 6:
            do_animation_6(calibration);
            break;
        case 7:
            do_animation_7(calibration);
            break;
        case 8:
            do_animation_8(calibration);
            break;
        default:
            ESP_LOGW(SERVO_TAG, "Invalid animation number: %d", animation_id);
    }
}

void do_animation_1(servo_calibration_t calibration)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 1: Left ear listening intently");
    
    // Move left ear up and left
    ESP_ERROR_CHECK(move_servo(1, 80, &calibration)); // Left latitude up
    ESP_ERROR_CHECK(move_servo(0, 60, &calibration)); // Left azimuth leftwards
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Add slight rotation and hold
    ESP_ERROR_CHECK(move_servo(0, 50, &calibration)); // Rotate left ear slightly more forward
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Return to center
    ESP_ERROR_CHECK(reset_servos());
    ESP_LOGI(SERVO_TAG, "Animation 1 complete");
}

void do_animation_2(servo_calibration_t calibration)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 2: Right ear listening intently");
    
    // Move right ear up and forward
    ESP_ERROR_CHECK(move_servo(3, 100, &calibration)); // Right latitude up
    ESP_ERROR_CHECK(move_servo(2, 120, &calibration)); // Right azimuth rightwards
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Add slight rotation and hold
    ESP_ERROR_CHECK(move_servo(2, 130, &calibration)); // Rotate right ear slightly more forward
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // Return to center
    ESP_ERROR_CHECK(reset_servos());
    ESP_LOGI(SERVO_TAG, "Animation 2 complete");
}

void do_animation_3(servo_calibration_t calibration)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 3: Happy wiggle");
    
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
    
    ESP_LOGI(SERVO_TAG, "Animation 3 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_4(servo_calibration_t calibration)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 4: Sad");

    for (int i = 0; i < 3; i++) {
        // move ears forward/down
        ESP_ERROR_CHECK(move_servo(1, 145, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 35, &calibration));
        // rotate outward
        ESP_ERROR_CHECK(move_servo(0, 30, &calibration));
        ESP_ERROR_CHECK(move_servo(2, 150, &calibration));
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(SERVO_TAG, "Animation 4 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_5(servo_calibration_t calibration)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 5: Playful bounce");
    // Very rapid playful movements - extended version

    for (int i = 0; i < 8; i++) {
        // Quick bouncy positions
        ESP_ERROR_CHECK(move_servo(1, 80 + (i % 4) * 12, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 100 - (i % 4) * 12, &calibration));
        ESP_ERROR_CHECK(move_servo(0, 90 + ((i % 2) * 25 - 12), &calibration));
        ESP_ERROR_CHECK(move_servo(2, 90 + ((i % 2) * 25 - 12), &calibration));
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
        ESP_ERROR_CHECK(move_servo(1, 85 + (i % 2) * 20, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 95 - (i % 2) * 20, &calibration));
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(SERVO_TAG, "Animation 5 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_6(servo_calibration_t calibration)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 6: Curious tilt");
    // Alternate ears - one up, one down

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
    
    ESP_LOGI(SERVO_TAG, "Animation 6 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_7(servo_calibration_t calibration)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 7: Listening/Radar");
    // Ears rotate side to side like listening/scanning

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
    
    ESP_LOGI(SERVO_TAG, "Animation 7 complete");
    ESP_ERROR_CHECK(reset_servos());
}

void do_animation_8(servo_calibration_t calibration)
{
    ESP_LOGI(SERVO_TAG, "Starting animation 8: Excited twitch");
    // Rapid excited movements

    for (int i = 0; i < 6; i++) {
        // Quick twitch positions
        ESP_ERROR_CHECK(move_servo(1, 80 + (i % 3) * 15, &calibration));
        ESP_ERROR_CHECK(move_servo(3, 100 - (i % 3) * 15, &calibration));
        ESP_ERROR_CHECK(move_servo(0, 90 + ((i % 2) * 20 - 10), &calibration));
        ESP_ERROR_CHECK(move_servo(2, 90 + ((i % 2) * 20 - 10), &calibration));
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
    
    ESP_LOGI(SERVO_TAG, "Animation 8 complete");
    ESP_ERROR_CHECK(reset_servos());
}