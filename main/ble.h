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
#include <stdbool.h>
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

/**
 * @brief Whether the client has subscribed to indications on ABF2
 *
 * @return true once the client writes the CCCD, false again on disconnect
 */
bool ble_is_data_notify_enabled(void);

/**
 * @brief Largest frame the client can exchange, the negotiated MTU minus 3
 *
 * Reported to the client in the store capability record, since the ATT MTU is
 * invisible to Web Bluetooth (ble-protocol.md S1.4).
 *
 * @return Usable frame size in bytes
 */
uint16_t ble_get_max_chunk_bytes(void);

/**
 * @brief Indicate a store response on ABF2, chunked to fit the negotiated MTU
 *
 * Blocks until every chunk is confirmed, so it must be called from the controller
 * task rather than a BLE callback.
 *
 * @param corr Correlation id, echoed from the request
 * @param status Response status, repeated in every chunk
 * @param payload Response payload, may be NULL when payload_len is 0
 * @param payload_len Length of payload in bytes
 * @return true if every chunk was delivered and confirmed
 */
bool ble_send_store_response(uint8_t corr, uint8_t status, const uint8_t *payload, uint16_t payload_len);

#endif // BLE_H
