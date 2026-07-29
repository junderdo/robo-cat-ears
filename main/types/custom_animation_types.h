/*
 * Description: Data structures for custom keyframe animations
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CUSTOM_ANIMATION_TYPES_H
#define CUSTOM_ANIMATION_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of keyframes in a custom animation
 */
#define CUSTOM_ANIMATION_MAX_KEYFRAMES 64

/**
 * @brief Serialized size of a single keyframe
 */
#define CUSTOM_ANIMATION_KEYFRAME_SIZE 12

/**
 * @brief Maximum serialized size of a custom animation (count byte + keyframes)
 */
#define CUSTOM_ANIMATION_MAX_SERIALIZED_SIZE \
    (1 + CUSTOM_ANIMATION_MAX_KEYFRAMES * CUSTOM_ANIMATION_KEYFRAME_SIZE)

/**
 * @brief Size of the chunk header that precedes each slice of a transfer
 *
 * An animation can outgrow a single BLE write, so it is sent as a sequence of
 * chunks: [transfer_id:1][chunk_index:1][chunk_count:1][payload].
 */
#define CUSTOM_ANIMATION_CHUNK_HEADER_SIZE 3

/**
 * @brief Easing curve applied to a keyframe transition
 */
typedef enum {
    EASE_TYPE_NONE = 0,     /*!< Linear */
    EASE_TYPE_SINE = 1,     /*!< Sinusoidal */
    EASE_TYPE_CUBIC = 2,    /*!< Cubic */
    EASE_TYPE_ELASTIC = 3,  /*!< Elastic (overshoots) */
} ease_type_t;

#define EASE_TYPE_MAX EASE_TYPE_ELASTIC

/**
 * @brief A single animation keyframe
 *
 * Holds the target angle of all four servos and the time at which they should
 * reach it, relative to the start of the animation.
 *
 * Easing is split across the keyframe boundary: the transition from the
 * previous keyframe to this one is driven by the previous keyframe's ease_out
 * (departure) followed by this keyframe's ease_in (arrival). The two meet
 * exactly halfway between the two poses; if they do not fill the segment the
 * servos hold at that halfway pose in between.
 */
typedef struct {
    uint16_t time_ms;       /*!< Time of this keyframe, relative to animation start */
    uint8_t angles[4];      /*!< Servo angles (0-180), indexed by SERVO_* channel */
    uint8_t ease_in_type;   /*!< ease_type_t for arriving at this keyframe */
    uint8_t ease_out_type;  /*!< ease_type_t for departing this keyframe */
    uint16_t ease_in_ms;    /*!< Duration of the arrival easing */
    uint16_t ease_out_ms;   /*!< Duration of the departure easing */
} animation_keyframe_t;

/**
 * @brief A custom animation: an ordered collection of keyframes
 */
typedef struct {
    uint8_t keyframe_count;
    animation_keyframe_t keyframes[CUSTOM_ANIMATION_MAX_KEYFRAMES];
} custom_animation_t;

/**
 * @brief Serialize a custom animation to a byte array
 *
 * Binary format (big-endian):
 *   [keyframe_count:1]
 *   then keyframe_count repetitions of:
 *   [time_ms:2][left_azi:1][left_lat:1][right_azi:1][right_lat:1]
 *   [ease_in_type:1][ease_out_type:1][ease_in_ms:2][ease_out_ms:2]
 *
 * @param anim Animation to serialize
 * @param output Output buffer, at least CUSTOM_ANIMATION_MAX_SERIALIZED_SIZE bytes
 * @param output_len Receives the number of bytes written
 * @return true if successful, false otherwise
 */
static inline bool custom_animation_serialize(const custom_animation_t *anim, uint8_t *output, uint16_t *output_len)
{
    if (anim == NULL || output == NULL || output_len == NULL) {
        return false;
    }

    if (anim->keyframe_count == 0 || anim->keyframe_count > CUSTOM_ANIMATION_MAX_KEYFRAMES) {
        return false;
    }

    uint16_t offset = 0;
    output[offset++] = anim->keyframe_count;

    for (uint8_t i = 0; i < anim->keyframe_count; i++) {
        const animation_keyframe_t *kf = &anim->keyframes[i];

        output[offset++] = (kf->time_ms >> 8) & 0xFF;
        output[offset++] = kf->time_ms & 0xFF;
        output[offset++] = kf->angles[0];
        output[offset++] = kf->angles[1];
        output[offset++] = kf->angles[2];
        output[offset++] = kf->angles[3];
        output[offset++] = kf->ease_in_type;
        output[offset++] = kf->ease_out_type;
        output[offset++] = (kf->ease_in_ms >> 8) & 0xFF;
        output[offset++] = kf->ease_in_ms & 0xFF;
        output[offset++] = (kf->ease_out_ms >> 8) & 0xFF;
        output[offset++] = kf->ease_out_ms & 0xFF;
    }

    *output_len = offset;
    return true;
}

/**
 * @brief Deserialize a custom animation from a byte array
 *
 * Rejects animations that are malformed or that playback cannot honour:
 * out-of-range angles or easing types, and keyframe times that move backwards.
 *
 * @param data Serialized animation bytes
 * @param data_len Length of the serialized data
 * @param anim Animation structure to populate
 * @return true if successful, false otherwise
 */
static inline bool custom_animation_deserialize(const uint8_t *data, uint16_t data_len, custom_animation_t *anim)
{
    if (data == NULL || anim == NULL || data_len < 1) {
        return false;
    }

    uint8_t count = data[0];
    if (count == 0 || count > CUSTOM_ANIMATION_MAX_KEYFRAMES) {
        return false;
    }

    if (data_len < (uint16_t)(1 + count * CUSTOM_ANIMATION_KEYFRAME_SIZE)) {
        return false;
    }

    anim->keyframe_count = count;

    uint16_t offset = 1;
    uint16_t previous_time_ms = 0;

    for (uint8_t i = 0; i < count; i++) {
        animation_keyframe_t *kf = &anim->keyframes[i];

        kf->time_ms = ((uint16_t)data[offset] << 8) | data[offset + 1];
        offset += 2;
        kf->angles[0] = data[offset++];
        kf->angles[1] = data[offset++];
        kf->angles[2] = data[offset++];
        kf->angles[3] = data[offset++];
        kf->ease_in_type = data[offset++];
        kf->ease_out_type = data[offset++];
        kf->ease_in_ms = ((uint16_t)data[offset] << 8) | data[offset + 1];
        offset += 2;
        kf->ease_out_ms = ((uint16_t)data[offset] << 8) | data[offset + 1];
        offset += 2;

        for (uint8_t ch = 0; ch < 4; ch++) {
            if (kf->angles[ch] > 180) {
                return false;
            }
        }

        if (kf->ease_in_type > EASE_TYPE_MAX || kf->ease_out_type > EASE_TYPE_MAX) {
            return false;
        }

        if (i > 0 && kf->time_ms < previous_time_ms) {
            return false;
        }
        previous_time_ms = kf->time_ms;
    }

    return true;
}

#ifdef __cplusplus
}
#endif

#endif // CUSTOM_ANIMATION_TYPES_H
