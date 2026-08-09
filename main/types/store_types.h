/*
 * Description: Wire types for the 0x06 animation store surface (docs/ble-protocol.md S4-S9)
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef STORE_TYPES_H
#define STORE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Protocol version reported by CAPABILITY, bumped only on breaking changes
 */
#define STORE_PROTOCOL_VERSION 1

/**
 * @brief Number of animation slots, indices 0..STORE_SLOT_COUNT-1
 */
#define STORE_SLOT_COUNT 16

/**
 * @brief Bytes of frame header, both directions:
 *
 * request  [0x06][corr][sub_opcode][chunk_index][chunk_count]
 * response [0x06][corr][status][chunk_index][chunk_count]
 */
#define STORE_FRAME_HEADER_SIZE 5

/**
 * @brief Header bytes a request carries after data_packet_unpack strips the type byte
 */
#define STORE_REQUEST_HEADER_SIZE (STORE_FRAME_HEADER_SIZE - 1)

/**
 * @brief Reassembly buffer size: a worst-case STORE request payload
 *
 * [slot:1][animation_id:16][name_len:1][name:32][wire_format:769]
 */
#define STORE_REQUEST_MAX_PAYLOAD_SIZE 819

/**
 * @brief Sub-opcodes, carried in the first header byte after corr
 */
typedef enum {
    STORE_OPCODE_CAPABILITY = 0x01,    /*!< Report protocol version, slot count, max chunk bytes */
    STORE_OPCODE_LIST = 0x02,          /*!< List occupied slots */
    STORE_OPCODE_STORE = 0x03,         /*!< Write an animation to a slot */
    STORE_OPCODE_DELETE = 0x04,        /*!< Empty a slot */
    STORE_OPCODE_PLAY = 0x05,          /*!< Play the animation in a slot */
    STORE_OPCODE_GET_ANIMATION = 0x06, /*!< Reserved, unimplemented */
    STORE_OPCODE_RENAME = 0x07,        /*!< Reserved, unimplemented */
} store_opcode_t;

/**
 * @brief Response status, carried in every response frame
 */
typedef enum {
    STORE_STATUS_OK = 0x00,
    STORE_STATUS_UNSUPPORTED_OPCODE = 0x01,
    STORE_STATUS_MALFORMED_REQUEST = 0x02,
    STORE_STATUS_SLOT_OUT_OF_RANGE = 0x03,
    STORE_STATUS_SLOT_EMPTY = 0x04,
    STORE_STATUS_INVALID_NAME = 0x05,
    STORE_STATUS_INVALID_ANIMATION = 0x06,
    STORE_STATUS_TOO_LARGE = 0x07,
    STORE_STATUS_CHUNK_OUT_OF_ORDER = 0x08,
    STORE_STATUS_STORAGE_FAILURE = 0x09,
    STORE_STATUS_NO_ACTIVE_TRANSFER = 0x0A,
} store_status_t;

#ifdef __cplusplus
}
#endif

#endif // STORE_TYPES_H
