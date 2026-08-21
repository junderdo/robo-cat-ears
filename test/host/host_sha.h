/*
 * Description: Test control over the stubbed SHA-256
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HOST_SHA_H
#define HOST_SHA_H

/**
 * @brief Make the next mbedtls_sha256 calls hash normally
 */
void host_sha_set_success(void);

/**
 * @brief Make the next mbedtls_sha256 calls fail without writing their output
 */
void host_sha_set_failure(void);

#endif /* HOST_SHA_H */
