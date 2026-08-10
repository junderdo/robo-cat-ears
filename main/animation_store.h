/*
 * Description: NVS persistence for stored animations (docs/ble-protocol.md S3)
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ANIMATION_STORE_H
#define ANIMATION_STORE_H

#include <stdint.h>
#include "esp_err.h"
#include "nvs.h"

/**
 * @brief Mount the `anim` NVS partition
 *
 * Must run before any other call here. Its own partition, so animations cannot
 * crowd out the BLE bond database in the stock `nvs` partition.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t animation_store_init(void);

/**
 * @brief Write a slot record, replacing whatever the slot held
 *
 * One blob per slot, so the replacement is atomic: a torn write leaves the old
 * record or a cleanly empty slot, never a half-updated one.
 *
 * @param slot Slot index, 0..STORE_SLOT_COUNT-1
 * @param record Record bytes, [animation_id:16][name_len:1][name][wire_format]
 * @param record_len Length of record in bytes
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t animation_store_write(uint8_t slot, const uint8_t *record, uint16_t record_len);

/**
 * @brief Read a slot record
 *
 * @param slot Slot index, 0..STORE_SLOT_COUNT-1
 * @param record Output buffer, at least STORE_RECORD_MAX_SIZE bytes
 * @param record_len Receives the number of bytes read
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if the slot is empty,
 *         error code otherwise
 */
esp_err_t animation_store_read(uint8_t slot, uint8_t *record, uint16_t *record_len);

#endif // ANIMATION_STORE_H
