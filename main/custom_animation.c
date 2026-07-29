/*
 * Description: Keyframe animation playback for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "custom_animation.h"
#include "servo.h"
#include "servo_calibration.h"

/* Servos accept a new pulse width once per 20 ms PWM frame, so there is
   nothing to gain from a faster tick. */
#define ANIMATION_TICK_MS 20

/* Period of the elastic oscillation, matching the common easing definitions. */
#define ELASTIC_C4 ((2.0f * (float)M_PI) / 3.0f)

/**
 * @brief Easing curve for departing a keyframe: starts at rest, accelerates
 *
 * @param type ease_type_t value
 * @param x Normalized progress through the departure (0..1)
 * @return Normalized distance covered; elastic may fall outside 0..1
 */
static float ease_depart(uint8_t type, float x)
{
    switch (type) {
        case EASE_TYPE_SINE:
            return 1.0f - cosf((x * (float)M_PI) / 2.0f);
        case EASE_TYPE_CUBIC:
            return x * x * x;
        case EASE_TYPE_ELASTIC:
            if (x <= 0.0f) return 0.0f;
            if (x >= 1.0f) return 1.0f;
            return -powf(2.0f, 10.0f * x - 10.0f) * sinf((x * 10.0f - 10.75f) * ELASTIC_C4);
        case EASE_TYPE_NONE:
        default:
            return x;
    }
}

/**
 * @brief Easing curve for arriving at a keyframe: decelerates, ends at rest
 *
 * @param type ease_type_t value
 * @param x Normalized progress through the arrival (0..1)
 * @return Normalized distance covered; elastic may fall outside 0..1
 */
static float ease_arrive(uint8_t type, float x)
{
    switch (type) {
        case EASE_TYPE_SINE:
            return sinf((x * (float)M_PI) / 2.0f);
        case EASE_TYPE_CUBIC:
            return 1.0f - powf(1.0f - x, 3.0f);
        case EASE_TYPE_ELASTIC:
            if (x <= 0.0f) return 0.0f;
            if (x >= 1.0f) return 1.0f;
            return powf(2.0f, -10.0f * x) * sinf((x * 10.0f - 0.75f) * ELASTIC_C4) + 1.0f;
        case EASE_TYPE_NONE:
        default:
            return x;
    }
}

/**
 * @brief Drive all four servos to a pose
 *
 * @param from Pose to interpolate from
 * @param to Pose to interpolate towards
 * @param progress Fraction of the way from `from` to `to`
 * @param calibration Servo calibration offsets
 */
static void apply_pose(const uint8_t from[4], const uint8_t to[4], float progress, servo_calibration_t *calibration)
{
    for (uint8_t ch = 0; ch < 4; ch++) {
        float angle = (float)from[ch] + ((float)to[ch] - (float)from[ch]) * progress;

        // Elastic overshoots by design, so the pose can leave the servo range
        if (angle < 0.0f) angle = 0.0f;
        if (angle > SERVO_MAX_ANGLE) angle = SERVO_MAX_ANGLE;

        esp_err_t err = move_servo(ch, angle, calibration);
        if (err != ESP_OK) {
            ESP_LOGW(SERVO_TAG, "Failed to move servo %d during animation: %s", ch, esp_err_to_name(err));
        }
    }
}

/**
 * @brief Play the transition between two keyframes
 *
 * The segment is covered by `from`'s departure easing followed by `to`'s
 * arrival easing, which meet exactly halfway between the two poses. Any time
 * the two do not account for is spent holding that halfway pose. If the two
 * durations overrun the segment they are scaled down proportionally.
 *
 * @param from Keyframe being left
 * @param to Keyframe being reached
 * @param calibration Servo calibration offsets
 */
static void play_segment(const animation_keyframe_t *from, const animation_keyframe_t *to,
                         servo_calibration_t *calibration)
{
    uint32_t segment_ms = (uint32_t)(to->time_ms - from->time_ms);

    if (segment_ms == 0) {
        apply_pose(from->angles, to->angles, 1.0f, calibration);
        return;
    }

    float depart_ms = (float)from->ease_out_ms;
    float arrive_ms = (float)to->ease_in_ms;

    if (depart_ms + arrive_ms > (float)segment_ms) {
        float scale = (float)segment_ms / (depart_ms + arrive_ms);
        depart_ms *= scale;
        arrive_ms *= scale;
    }

    // The arrival easing occupies the tail of the segment; everything between
    // the end of the departure and the start of the arrival is a hold.
    float arrive_start_ms = (float)segment_ms - arrive_ms;

    TickType_t last_wake = xTaskGetTickCount();

    for (uint32_t t = 0; t < segment_ms; t += ANIMATION_TICK_MS) {
        float progress;

        if ((float)t < depart_ms) {
            progress = 0.5f * ease_depart(from->ease_out_type, (float)t / depart_ms);
        } else if ((float)t < arrive_start_ms || arrive_ms <= 0.0f) {
            progress = 0.5f;
        } else {
            progress = 0.5f + 0.5f * ease_arrive(to->ease_in_type, ((float)t - arrive_start_ms) / arrive_ms);
        }

        apply_pose(from->angles, to->angles, progress, calibration);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ANIMATION_TICK_MS));
    }

    // Land exactly on the authored pose regardless of where the tick fell
    apply_pose(from->angles, to->angles, 1.0f, calibration);
}

void custom_animation_play(const custom_animation_t *anim)
{
    if (anim == NULL || anim->keyframe_count == 0) {
        ESP_LOGW(SERVO_TAG, "Custom animation has no keyframes");
        return;
    }

    servo_calibration_t calibration;
    esp_err_t err = servo_load_calibration(&calibration);
    if (err != ESP_OK) {
        ESP_LOGW(SERVO_TAG, "Failed to load calibration, playing uncalibrated: %s", esp_err_to_name(err));
        servo_calibration_init(&calibration);
    }

    ESP_LOGI(SERVO_TAG, "Playing custom animation: %d keyframes, %d ms",
             anim->keyframe_count, anim->keyframes[anim->keyframe_count - 1].time_ms);

    // The first keyframe is the starting pose, taken up immediately
    apply_pose(anim->keyframes[0].angles, anim->keyframes[0].angles, 1.0f, &calibration);

    for (uint8_t i = 1; i < anim->keyframe_count; i++) {
        play_segment(&anim->keyframes[i - 1], &anim->keyframes[i], &calibration);
    }

    ESP_LOGI(SERVO_TAG, "Custom animation complete");
}
