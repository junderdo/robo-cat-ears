/*
 * Description: The per-unit device serial reported by CAPABILITY (docs/ble-protocol.md S8.1)
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DEVICE_SERIAL_H
#define DEVICE_SERIAL_H

#include <stdint.h>

/**
 * @brief Bytes of a serial, matching the 48-bit width of the MAC it comes from
 */
#define DEVICE_SERIAL_SIZE 6

/**
 * @brief Bytes of the factory eFuse MAC the serial is derived from
 */
#define DEVICE_SERIAL_MAC_SIZE 6

/**
 * @brief Derive the serial for a factory MAC
 *
 * SHA-256("milklab-ears-serial-v1" || mac), truncated to the first six bytes.
 *
 * The derivation is frozen. Changing the domain string, the hash or the width
 * makes every unit report a different serial after a firmware update, orphaning
 * every registration keyed to the old value, and there is no repair. The `v1`
 * is domain separation, not an upgrade path. See docs/ble-protocol.md S8.1.
 *
 * Yields the reserved all-zero value if the hash fails.
 *
 * @param mac Factory eFuse MAC, DEVICE_SERIAL_MAC_SIZE bytes
 * @param serial Receives DEVICE_SERIAL_SIZE bytes
 */
void device_serial_derive(const uint8_t *mac, uint8_t *serial);

/**
 * @brief The serial of this unit, or the reserved all-zero value
 *
 * All-zero means the eFuse read or the hash failed and this device cannot tell
 * you its serial. It is never a legal serial.
 *
 * @param serial Receives DEVICE_SERIAL_SIZE bytes
 */
void device_serial_get(uint8_t *serial);

#endif // DEVICE_SERIAL_H
