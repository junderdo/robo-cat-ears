/*
 * Description: Data structures for animation mode values
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ANIMATION_MODE_TYPES_H
#define ANIMATION_MODE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Animation mode data structure
 * 
 * Stores the identifier for an animation mode.
 */
typedef struct {
    int16_t mode_id;    /*!< Animation mode identifier */
    int16_t frequency;      /*!< Animation frequency -- how many times per hour > */
} animation_mode_t;

#ifdef __cplusplus
}
#endif

#endif // ANIMATION_MODE_TYPES_H
