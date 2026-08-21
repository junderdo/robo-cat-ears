/*
 * Description: Host stand-in for ESP-IDF logging
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Compiles the format string and its arguments away rather than printing, so a
 * log line still type-checks on the host without adding noise to test output.
 */

#ifndef HOST_ESP_LOG_H
#define HOST_ESP_LOG_H

#include <stdio.h>

#define ESP_LOGE(tag, ...)       \
    do {                         \
        (void)(tag);             \
        if (0) {                 \
            printf(__VA_ARGS__); \
        }                        \
    } while (0)

#endif /* HOST_ESP_LOG_H */
