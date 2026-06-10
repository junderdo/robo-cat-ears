/*
 * Description: Animation mode function header file
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ANIMATION_MODE_H
#define ANIMATION_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#include "types/animation_mode_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ANIMATION_MODE_NVS_NAMESPACE "anim_mode"
#define ANIMATION_MODE_NVS_KEY       "mode"

#define ANIMATION_MODE_TAG "ANIM_MODE"

void animation_mode_init(animation_mode_t *data);

bool animation_mode_serialize(const animation_mode_t *data, uint8_t *output, uint16_t *output_len);

bool animation_mode_deserialize(const uint8_t *input, uint16_t input_len, animation_mode_t *data);

/**
 * @brief Save animation mode data to NVS
 *
 * @param mode Pointer to animation mode data to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t animation_mode_save(const animation_mode_t *mode);

/**
 * @brief Load animation mode data from NVS
 *
 * @param mode Pointer to animation mode data structure to populate
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t animation_mode_load(animation_mode_t *mode);

#ifdef __cplusplus
}
#endif

#endif // ANIMATION_MODE_H
