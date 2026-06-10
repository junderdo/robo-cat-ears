/*
 * Description: BLE packet data structures for packed data transmission
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

 /*
 TODO: fix this whole "types" abstraction layer, it's really just a collection of helper functions
 for packing/unpacking data to send over BLE. Maybe rename to "ble_packet_utils" or something?
 and split into separate files for each data type (animation, lighting, servo calibration)
 */

#ifndef BLE_PACKET_TYPES_H
#define BLE_PACKET_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "lighting_types.h"
#include "servo_calibration_types.h"
#include "../servo_calibration.h"
#include "animation_mode_types.h"
#include "../animation_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum payload size for BLE packet
 * 
 * Max BLE MTU is 512 bytes, minus 1 byte for type field
 */
#define BLE_PACKET_MAX_PAYLOAD_SIZE 511

/**
 * @brief Maximum total packet size including type byte
 */
#define BLE_PACKET_MAX_SIZE 512

/**
 * @brief Data type enumeration for packed data transmission
 */
typedef enum {
    DATA_TYPE_ANIMATION = 0x01,           /*!< Animation command data */
    DATA_TYPE_LIGHTING = 0x02,            /*!< Lighting control data */
    DATA_TYPE_SERVO_CALIBRATION = 0x03,   /*!< Servo calibration offset data */
    DATA_TYPE_ANIMATION_MODE = 0x04,      /*!< Animation mode data */
} data_type_t;

/**
 * @brief Packed data structure for transmission over ABF1 characteristic
 * 
 * This structure packs a type byte followed by the data payload.
 * Format: [type:1byte][data:variable]
 * Max total size: 512 bytes (BLE MTU limit)
 */
typedef struct {
    data_type_t type;                           /*!< Data type identifier */
    uint8_t data[BLE_PACKET_MAX_PAYLOAD_SIZE];  /*!< Payload data (e.g., JSON string) */
    uint16_t data_len;                          /*!< Length of valid data in bytes */
} data_packet_t;

/**
 * @brief Pack the data packet into a byte array for transmission
 * 
 * @param packet Pointer to the packet structure to pack
 * @param output Output buffer for packed data (must be at least BLE_PACKET_MAX_SIZE bytes)
 * @param output_len Pointer to store the length of packed data
 * @return true if packing successful, false otherwise
 */
static inline bool data_packet_pack(const data_packet_t *packet, uint8_t *output, uint16_t *output_len)
{
    if (packet == NULL || output == NULL || output_len == NULL) {
        return false;
    }
    
    if (packet->data_len > BLE_PACKET_MAX_PAYLOAD_SIZE) {
        return false;
    }
    
    // Pack type byte
    output[0] = (uint8_t)packet->type;
    
    // Pack data payload
    memcpy(&output[1], packet->data, packet->data_len);
    
    // Total length is type byte + data length
    *output_len = 1 + packet->data_len;
    
    return true;
}

/**
 * @brief Unpack a byte array into a DataPacket
 * 
 * @param packed Pointer to packed byte array from BLE
 * @param packed_len Length of packed data in bytes
 * @param packet Pointer to output packet structure
 * @return true if unpacking successful, false otherwise
 */
static inline bool data_packet_unpack(const uint8_t *packed, uint16_t packed_len, data_packet_t *packet)
{
    if (packed == NULL || packet == NULL) {
        return false;
    }
    
    if (packed_len < 1 || packed_len > BLE_PACKET_MAX_SIZE) {
        return false;
    }
    
    // Unpack type byte
    packet->type = (data_type_t)packed[0];
    
    // Validate type
    if (packet->type != DATA_TYPE_ANIMATION && packet->type != DATA_TYPE_LIGHTING &&
        packet->type != DATA_TYPE_SERVO_CALIBRATION && packet->type != DATA_TYPE_ANIMATION_MODE) {
        return false;
    }
    
    // Unpack data payload
    packet->data_len = packed_len - 1;
    if (packet->data_len > 0) {
        memcpy(packet->data, &packed[1], packet->data_len);
    }
    
    return true;
}

/**
 * @brief Pack lighting data into a data packet
 * 
 * @param packet Pointer to the packet structure to populate
 * @param lighting Pointer to lighting data to pack
 * @return true if successful, false otherwise
 */
static inline bool data_packet_pack_lighting(data_packet_t *packet, const lighting_data_t *lighting)
{
    if (packet == NULL || lighting == NULL) {
        return false;
    }
    
    uint16_t serialized_len;
    if (!lighting_data_serialize(lighting, packet->data, &serialized_len)) {
        return false;
    }
    
    packet->type = DATA_TYPE_LIGHTING;
    packet->data_len = serialized_len;
    
    return true;
}

/**
 * @brief Get lighting data from a data packet
 * 
 * This function deserializes the packet data into a lighting_data_t structure.
 * The packet type must be DATA_TYPE_LIGHTING.
 * 
 * @param packet Pointer to the packet structure containing lighting data
 * @param lighting Pointer to lighting data structure to populate
 * @return true if successful, false otherwise
 */
static inline bool data_packet_get_lighting(const data_packet_t *packet, lighting_data_t *lighting)
{
    if (packet == NULL || lighting == NULL) {
        return false;
    }
    
    if (packet->type != DATA_TYPE_LIGHTING) {
        return false;
    }
    
    return lighting_data_deserialize(packet->data, packet->data_len, lighting);
}

/**
 * @brief Pack servo calibration data into a data packet
 * 
 * @param packet Pointer to the packet structure to populate
 * @param calibration Pointer to servo calibration data to pack
 * @return true if successful, false otherwise
 */
static inline bool data_packet_pack_servo_calibration(data_packet_t *packet, const servo_calibration_t *calibration)
{
    if (packet == NULL || calibration == NULL) {
        return false;
    }
    
    uint16_t serialized_len;
    if (!servo_calibration_serialize(calibration, packet->data, &serialized_len)) {
        return false;
    }
    
    packet->type = DATA_TYPE_SERVO_CALIBRATION;
    packet->data_len = serialized_len;
    
    return true;
}

/**
 * @brief Get servo calibration data from a data packet
 * 
 * This function deserializes the packet data into a servo_calibration_t structure.
 * The packet type must be DATA_TYPE_SERVO_CALIBRATION.
 * 
 * @param packet Pointer to the packet structure containing calibration data
 * @param calibration Pointer to servo calibration data structure to populate
 * @return true if successful, false otherwise
 */
static inline bool data_packet_get_servo_calibration(const data_packet_t *packet, servo_calibration_t *calibration)
{
    if (packet == NULL || calibration == NULL) {
        return false;
    }
    
    if (packet->type != DATA_TYPE_SERVO_CALIBRATION) {
        return false;
    }
    
    return servo_calibration_deserialize(packet->data, packet->data_len, calibration);
}

/**
 * @brief Pack animation mode data into a data packet
 * 
 * @param packet Pointer to the packet structure to populate
 * @param mode Pointer to animation mode data to pack
 * @return true if successful, false otherwise
 */
static inline bool data_packet_pack_animation_mode(data_packet_t *packet, const animation_mode_t *mode)
{
    if (packet == NULL || mode == NULL) {
        return false;
    }
    
    uint16_t serialized_len;
    if (!animation_mode_serialize(mode, packet->data, &serialized_len)) {
        return false;
    }
    
    packet->type = DATA_TYPE_ANIMATION_MODE;
    packet->data_len = serialized_len;
    
    return true;
}

/**
 * @brief Get animation mode data from a data packet
 * 
 * This function deserializes the packet data into an animation_mode_t structure.
 * The packet type must be DATA_TYPE_ANIMATION_MODE.
 * 
 * @param packet Pointer to the packet structure containing animation mode data
 * @param mode Pointer to animation mode data structure to populate
 * @return true if successful, false otherwise
 */
static inline bool data_packet_get_animation_mode(const data_packet_t *packet, animation_mode_t *mode)
{
    if (packet == NULL || mode == NULL) {
        return false;
    }
    
    if (packet->type != DATA_TYPE_ANIMATION_MODE) {
        return false;
    }
    
    return animation_mode_deserialize(packet->data, packet->data_len, mode);
}

#ifdef __cplusplus
}
#endif

#endif // BLE_PACKET_TYPES_H
