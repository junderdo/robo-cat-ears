/*
 * Description: Controller for handling BLE commands and coordinating device actions
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "esp_gatts_api.h"

/**
 * @brief Handle BLE GATT read events
 * 
 * Processes read requests from BLE clients
 * 
 * @param param Pointer to the GATT callback parameter containing read event data
 */
void controller_handle_read(esp_ble_gatts_cb_param_t *param);

/**
 * @brief Handle BLE GATT write events
 * 
 * Processes write requests from BLE clients and dispatches appropriate
 * commands (e.g., triggering animations based on received data)
 * 
 * @param param Pointer to the GATT callback parameter containing write event data
 */
void controller_handle_write(esp_ble_gatts_cb_param_t *param);

#endif // CONTROLLER_H
