/*
 * Description: Servo calibration function header file
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <esp_err.h>

#include "types/servo_calibration_types.h"

#define SERVO_CALIBRATION_NVS_NAMESPACE "servo_cal"
#define SERVO_CALIBRATION_NVS_KEY "calibration"

#define SERVO_TAG "SERVO"

void servo_calibration_init(servo_calibration_t *data);

bool servo_calibration_serialize(const servo_calibration_t *data, uint8_t *output, uint16_t *output_len);

bool servo_calibration_deserialize(const uint8_t *input, uint16_t input_len, servo_calibration_t *data);

/**
 * @brief Save servo calibration data to NVS
 * 
 * @param calibration Pointer to servo calibration data to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t servo_save_calibration(const servo_calibration_t *calibration);

/**
 * @brief Load servo calibration data from NVS
 * 
 * @param calibration Pointer to servo calibration data structure to populate
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no calibration saved
 */
esp_err_t servo_load_calibration(servo_calibration_t *calibration);
