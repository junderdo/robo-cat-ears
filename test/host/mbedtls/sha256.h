/*
 * Description: Host stand-in for mbedtls SHA-256
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lets main/device_serial.c compile off-target unchanged. Only the one-shot
 * entry point the firmware calls is provided; host_sha.c implements it.
 */

#ifndef HOST_MBEDTLS_SHA256_H
#define HOST_MBEDTLS_SHA256_H

#include <stddef.h>

int mbedtls_sha256(const unsigned char *input, size_t ilen, unsigned char *output, int is224);

#endif /* HOST_MBEDTLS_SHA256_H */
