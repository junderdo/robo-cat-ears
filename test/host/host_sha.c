/*
 * Description: Host stand-in for mbedtls SHA-256, backed by OpenSSL
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <openssl/sha.h>

#include "host_sha.h"
#include "mbedtls/sha256.h"

static int stub_result = 0;

void host_sha_set_success(void)
{
    stub_result = 0;
}

void host_sha_set_failure(void)
{
    stub_result = -1;
}

int mbedtls_sha256(const unsigned char *input, size_t ilen, unsigned char *output, int is224)
{
    (void)is224;

    // Leaving output untouched on failure is what makes the caller's digest
    // buffer uninitialised, which is the case the test is here to pin.
    if (stub_result != 0) {
        return stub_result;
    }

    SHA256(input, ilen, output);
    return 0;
}
