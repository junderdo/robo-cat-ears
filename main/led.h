/*
 * Description LED controller header for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LED_H
#define LED_H

#include <stdint.h>
#include <esp_err.h>
#include "types/lighting_types.h"

// LED configuration
#define LED_GPIO_0 5 // GPIO connects to the LED strip
#define LED_STRIP_LED_COUNT 31 // Number of LEDs in the strip
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000) // 10MHz resolution

// Lighting task function pointer type
typedef void (*lighting_task_func_t)(void *);

/**
 * @brief Initialize the LED strip
 * 
 * Configures the RMT peripheral and starts the LED animation task
 */
esp_err_t init_leds(void);

/**
 * @brief Stop the current LED animation task if running
 */
void led_stop_current_task(void);

/**
 * @brief Save lighting configuration to NVS
 * 
 * @param lighting Pointer to lighting data to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t led_save_config(const lighting_data_t *lighting);

/**
 * @brief Load lighting configuration from NVS
 * 
 * @param lighting Pointer to lighting data structure to populate
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no config saved
 */
esp_err_t led_load_config(lighting_data_t *lighting);

/**
 * @brief Start a lighting animation task
 * 
 * @param task_func Task function pointer
 * @param task_name Name of the task
 * @param lighting Pointer to lighting data structure
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t led_start_task(lighting_task_func_t task_func, const char *task_name, const lighting_data_t *lighting);

/**
 * @brief Start LED solid color mode
 * @param params Pointer to lighting_data_t structure
 */
void led_solid_task(void *params);

/**
 * @brief Start LED breathing mode
 * @param params Pointer to lighting_data_t structure
 */
void led_breathing_task(void *params);

/**
 * @brief Start LED marquee scrolling mode
 * @param params Pointer to lighting_data_t structure
 */
void led_marquee_task(void *params);

/**
 * @brief Start LED chasing mode
 * @param params Pointer to lighting_data_t structure
 */
void led_chasing_task(void *params);

/**
 * @brief Start LED rain mode
 * @param params Pointer to lighting_data_t structure
 */
void led_rain_task(void *params);

#endif // LED_H
