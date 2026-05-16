/*
 * Description: Data structures for LED lighting control
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LIGHTING_TYPES_H
#define LIGHTING_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of colors in a lighting pattern
 */
#define MAX_LIGHTING_COLORS 32

/**
 * @brief Lighting mode enumeration
 */
typedef enum {
    LIGHTING_MODE_SOLID = 0,      /*!< Solid color mode */
    LIGHTING_MODE_BREATHING = 1,  /*!< Breathing/pulsing effect */
    LIGHTING_MODE_MARQUEE = 2,    /*!< Marquee/scrolling effect */
    LIGHTING_MODE_CHASING = 3,    /*!< Chasing/running lights effect */
    LIGHTING_MODE_RAIN = 4,       /*!< Rain/falling drops effect */
} lighting_mode_t;

/**
 * @brief RGB color structure (24-bit)
 */
typedef struct {
    uint8_t r;  /*!< Red component (0-255) */
    uint8_t g;  /*!< Green component (0-255) */
    uint8_t b;  /*!< Blue component (0-255) */
} rgb_color_t;

/**
 * @brief Lighting data structure
 */
typedef struct {
    lighting_mode_t mode;                       /*!< Lighting mode */
    uint8_t speed;                              /*!< Animation speed (1-100) */
    rgb_color_t colors[MAX_LIGHTING_COLORS];    /*!< Color array */
    uint8_t color_count;                        /*!< Number of colors in use (0-32) */
} lighting_data_t;

// Helper macros for creating RGB colors

/**
 * @brief Create an RGB color from individual components
 */
#define RGB_COLOR(r, g, b) ((rgb_color_t){.r = (r), .g = (g), .b = (b)})

/**
 * @brief Create an RGB color from a 32-bit integer (0xRRGGBB)
 */
#define RGB_COLOR_FROM_UINT32(color) ((rgb_color_t){ \
    .r = (uint8_t)(((color) >> 16) & 0xFF), \
    .g = (uint8_t)(((color) >> 8) & 0xFF), \
    .b = (uint8_t)((color) & 0xFF) \
})

/**
 * @brief Convert an RGB color to a 32-bit integer (0xRRGGBB)
 */
#define RGB_COLOR_TO_UINT32(color) \
    ((uint32_t)(((color).r << 16) | ((color).g << 8) | (color).b))

// Helper functions for initialization

/**
 * @brief Initialize a lighting data structure with default values
 * 
 * @param data Pointer to lighting data structure to initialize
 */
static inline void lighting_data_init(lighting_data_t *data)
{
    data->mode = LIGHTING_MODE_SOLID;
    data->speed = 50;
    data->color_count = 0;
}

/**
 * @brief Add a color to the lighting data structure
 * 
 * @param data Pointer to lighting data structure
 * @param color RGB color to add
 * @return true if color was added successfully, false if array is full
 */
static inline bool lighting_data_add_color(lighting_data_t *data, rgb_color_t color)
{
    if (data->color_count >= MAX_LIGHTING_COLORS) {
        return false;
    }
    data->colors[data->color_count++] = color;
    return true;
}

/**
 * @brief Clear all colors from the lighting data structure
 * 
 * @param data Pointer to lighting data structure
 */
static inline void lighting_data_clear_colors(lighting_data_t *data)
{
    data->color_count = 0;
}

/**
 * @brief Serialize lighting data to byte array
 * 
 * Binary format:
 * - Byte 0: mode (uint8_t)
 * - Byte 1: speed (uint8_t)
 * - Byte 2: color_count (uint8_t)
 * - Bytes 3+: RGB colors (color_count * 3 bytes, R-G-B order)
 * 
 * @param data Pointer to lighting data structure to serialize
 * @param output Output buffer (must be at least 3 + color_count*3 bytes)
 * @param output_len Pointer to store the length of serialized data
 * @return true if serialization successful, false otherwise
 */
static inline bool lighting_data_serialize(const lighting_data_t *data, uint8_t *output, uint16_t *output_len)
{
    if (data == NULL || output == NULL || output_len == NULL) {
        return false;
    }
    
    if (data->color_count > MAX_LIGHTING_COLORS) {
        return false;
    }
    
    // Serialize header
    output[0] = (uint8_t)data->mode;
    output[1] = data->speed;
    output[2] = data->color_count;
    
    // Serialize colors
    uint16_t offset = 3;
    for (uint8_t i = 0; i < data->color_count; i++) {
        output[offset++] = data->colors[i].r;
        output[offset++] = data->colors[i].g;
        output[offset++] = data->colors[i].b;
    }
    
    *output_len = offset;
    return true;
}

/**
 * @brief Deserialize byte array into lighting data structure
 * 
 * @param input Input buffer containing serialized lighting data
 * @param input_len Length of input data in bytes
 * @param data Pointer to lighting data structure to populate
 * @return true if deserialization successful, false otherwise
 */
static inline bool lighting_data_deserialize(const uint8_t *input, uint16_t input_len, lighting_data_t *data)
{
    if (input == NULL || data == NULL) {
        return false;
    }
    
    // Need at least 3 bytes for header
    if (input_len < 3) {
        return false;
    }
    
    // Deserialize header
    data->mode = (lighting_mode_t)input[0];
    data->speed = input[1];
    data->color_count = input[2];
    
    // Validate color count
    if (data->color_count > MAX_LIGHTING_COLORS) {
        return false;
    }
    
    // Validate input length (3 header bytes + color_count * 3 RGB bytes)
    uint16_t expected_len = 3 + (data->color_count * 3);
    if (input_len < expected_len) {
        return false;
    }
    
    // Deserialize colors
    uint16_t offset = 3;
    for (uint8_t i = 0; i < data->color_count; i++) {
        data->colors[i].r = input[offset++];
        data->colors[i].g = input[offset++];
        data->colors[i].b = input[offset++];
    }
    
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // LIGHTING_TYPES_H
