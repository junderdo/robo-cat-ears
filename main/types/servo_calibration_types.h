/*
 * Description: Data structures for servo calibration values
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SERVO_CALIBRATION_TYPES_H
#define SERVO_CALIBRATION_TYPES_H

#include <stdint.h>

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

#ifdef __cplusplus
}
#endif

#endif // SERVO_CALIBRATION_TYPES_H
