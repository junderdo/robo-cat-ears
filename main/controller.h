/*
 * Description: Controller for handling BLE commands and coordinating device actions
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "esp_gatts_api.h"
#include "esp_err.h"

/**
 * @brief Initialize the controller task and command queue
 * 
 * Must be called before using any other controller functions
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t controller_init(void);

/**
 * @brief Handle BLE GATT read events
 * 
 * Processes read requests from BLE clients
 * 
 * @param gatts_if GATT server interface
 * @param param Pointer to the GATT callback parameter containing read event data
 */
void controller_handle_read(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/**
 * @brief Handle BLE GATT write events
 * 
 * Processes write requests from BLE clients and dispatches appropriate
 * commands (e.g., triggering animations based on received data)
 * 
 * @param param Pointer to the GATT callback parameter containing write event data
 */
void controller_handle_write(esp_ble_gatts_cb_param_t *param);

/**
 * @brief Update the BLE characteristic with current lighting data
 * 
 * This should be called whenever lighting configuration changes to ensure
 * the BLE characteristic reflects the current state.
 */
void controller_update_lighting_characteristic(void);

/**
 * @brief Update the BLE characteristic with current servo calibration data
 * 
 * This should be called whenever servo calibration changes to ensure
 * the BLE characteristic reflects the current state.
 */
void controller_update_servo_calibration_characteristic(void);

#endif // CONTROLLER_H
