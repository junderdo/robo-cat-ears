/*
 * Description: Data structures for servo calibration values
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SERVO_CALIBRATION_TYPES_H
#define SERVO_CALIBRATION_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Servo calibration offset data structure
 * 
 * Stores offset values for each servo to calibrate their positions.
 * These are signed integer offsets applied to servo pulse widths.
 */
typedef struct {
    int16_t left_azi;    /*!< Left azimuth servo offset (-1000 to +1000) */
    int16_t left_lat;    /*!< Left latitude servo offset (-1000 to +1000) */
    int16_t right_azi;   /*!< Right azimuth servo offset (-1000 to +1000) */
    int16_t right_lat;   /*!< Right latitude servo offset (-1000 to +1000) */
} servo_calibration_t;

/**
 * @brief Initialize servo calibration data with default values (all zeros)
 * 
 * @param data Pointer to servo calibration data structure to initialize
 */
static inline void servo_calibration_init(servo_calibration_t *data)
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
static inline bool servo_calibration_serialize(const servo_calibration_t *data, uint8_t *output, uint16_t *output_len)
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
static inline bool servo_calibration_deserialize(const uint8_t *input, uint16_t input_len, servo_calibration_t *data)
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

#ifdef __cplusplus
}
#endif

#endif // SERVO_CALIBRATION_TYPES_H
