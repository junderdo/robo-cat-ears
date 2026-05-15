/*
 * Description: Bluetooth Low Energy (BLE) server implementation for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BLE_H
#define BLE_H

#include "esp_err.h"
#include "esp_gatt_defs.h"
#include <stdint.h>

/**
 * @brief Initialize the BLE stack and start advertising
 * 
 * This function initializes the Bluetooth controller, Bluedroid stack,
 * GATT server, and starts BLE advertising.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t init_ble(void);

/**
 * @brief Get the current BLE connection ID
 * 
 * @return Current connection ID, or 0xffff if not connected
 */
uint16_t ble_get_conn_id(void);

/**
 * @brief Get the current BLE GATT server interface
 * 
 * @return Current GATTS interface, or 0xff if not initialized
 */
esp_gatt_if_t ble_get_gatts_if(void);

/**
 * @brief Get the GATT handle for the data receive characteristic
 * 
 * @return Handle for SPP_IDX_SPP_DATA_RECV_VAL, or 0 if not initialized
 */
uint16_t ble_get_data_recv_handle(void);

/**
 * @brief Get the GATT handle for the data notify characteristic
 * 
 * @return Handle for SPP_IDX_SPP_DATA_NTY_VAL, or 0 if not initialized
 */
uint16_t ble_get_data_notify_handle(void);

/**
 * @brief Get pointer to the data notify value buffer for direct updates
 * 
 * @return Pointer to spp_data_notify_val buffer
 */
uint8_t* ble_get_data_notify_buffer(void);

#endif // BLE_H
