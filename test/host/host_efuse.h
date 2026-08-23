/*
 * Description: Test control over the stubbed factory MAC read
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HOST_EFUSE_H
#define HOST_EFUSE_H

#include <stdint.h>

/**
 * @brief Make the next esp_efuse_mac_get_default calls succeed with this MAC
 */
void host_efuse_set_mac(const uint8_t mac[6]);

/**
 * @brief Make the next esp_efuse_mac_get_default calls fail
 */
void host_efuse_set_failure(void);

#endif /* HOST_EFUSE_H */
