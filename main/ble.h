/*
 * Description: Bluetooth Low Energy (BLE) server implementation for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Company: Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0
 */

#ifndef BLE_H
#define BLE_H

#include "esp_err.h"

/**
 * @brief Initialize the BLE stack and start advertising
 * 
 * This function initializes the Bluetooth controller, Bluedroid stack,
 * GATT server, and starts BLE advertising.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t init_ble(void);

#endif // BLE_H
