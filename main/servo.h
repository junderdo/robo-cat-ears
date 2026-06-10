/*
 * Description: Servo control and animation functions for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <esp_err.h>
#include "types/servo_calibration_types.h"

#ifndef SERVO_H
#define SERVO_H

// Servo configuration
#define SERVO_MIN_PULSEWIDTH_US 500  // Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH_US 2500  // Maximum pulse width in microsecond
#define SERVO_MAX_ANGLE         180    // Maximum angle (0-180 degrees)
#define SERVO_FREQ              50     // 50Hz for standard servos
#if CONFIG_IDF_TARGET_ESP32S3
#define SERVO_PULSE_GPIO_0      2      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_1      3      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_2      4      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_3      5      // GPIO connects to the PWM signal line
#elif CONFIG_IDF_TARGET_ESP32C3
#define SERVO_PULSE_GPIO_0      1      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_1      2      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_2      3      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_3      4      // GPIO connects to the PWM signal line
#else
#error "Unsupported IDF target: SERVO_PULSE_GPIO pins not defined"
#endif

#define SERVO_LEFT_AZIMUTH      0
#define SERVO_LEFT_LATITUDE     1
#define SERVO_RIGHT_AZIMUTH     2
#define SERVO_RIGHT_LATITUDE    3

/**
 * @brief Initialize the servo motors
 * 
 * Configures 4 servos on GPIO pins 2-5 and resets them to center position
 */
esp_err_t init_servos(void);

/**
 * @brief Reset all servos to center position (90 degrees)
 */
esp_err_t reset_servos(void);

/**
 * @brief Move a specific servo to a given angle
 * 
 * @param channel Servo channel (0-3)
 * @param angle Target angle (0-180 degrees)
 * @param calibration Pointer to servo calibration data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t move_servo(uint8_t channel, float angle, servo_calibration_t *calibration);

#endif // SERVO_H
