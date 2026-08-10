/*
 * Description: Transport for the 0x06 animation store surface (docs/ble-protocol.md S5, S11.7)
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef STORE_H
#define STORE_H

#include <stdint.h>

/**
 * @brief Take one frame of a store request, dispatching once the request is complete
 *
 * Reassembles chunks generically, so a sub-opcode handler only ever sees a whole
 * request. Responds on ABF2, except to a client that has not subscribed: such a
 * request is logged and dropped without side effects.
 *
 * Must be called from the controller task - sending a response blocks on the
 * indication confirmation.
 *
 * @param data Frame with the 0x06 type byte already stripped
 * @param data_len Length of data in bytes
 */
void store_process_request(const uint8_t *data, uint16_t data_len);

/**
 * @brief Discard any transfer in flight
 *
 * Called on disconnect and on unsubscribe - nothing can be resumed into.
 */
void store_reset(void);

#endif // STORE_H
