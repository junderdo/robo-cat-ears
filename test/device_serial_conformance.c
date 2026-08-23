/*
 * Description: Host test pinning the device serial derivation to independent vectors
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The vectors come from test/gen_serial_vectors.py, which implements
 * docs/ble-protocol.md S8.1 with hashlib. Agreement therefore means the
 * firmware matches the spec, not merely that it matches itself.
 *
 * The derivation is frozen (S8.1): a failure here means every registered pair
 * of ears in the world would report a different serial after a firmware
 * update. Change the firmware to match the vectors, never the reverse.
 *
 * Runs on the host: test/host/ supplies stand-ins for mbedtls and the eFuse
 * read. Build with `make -C test`.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "device_serial.h"
#include "device_serial_vectors.h"
#include "host_efuse.h"
#include "host_sha.h"

static int failures = 0;

/**
 * @brief Record a failed expectation against a named case
 *
 * @param name Case name
 * @param format printf-style description of what went wrong
 */
static void fail(const char *name, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    printf("  FAIL %s: ", name);
    vprintf(format, args);
    printf("\n");
    va_end(args);
    failures++;
}

/**
 * @brief Print a serial as the hex a client would display
 *
 * @param label Prefix identifying which serial this is
 * @param serial Bytes to print
 */
static void print_serial(const char *label, const uint8_t *serial)
{
    printf("       %s ", label);
    for (size_t i = 0; i < DEVICE_SERIAL_SIZE; i++) {
        printf("%02x", serial[i]);
    }
    printf("\n");
}

/**
 * @brief Check a derived serial against what the vector calls for
 *
 * @param name Case name
 * @param actual Serial the firmware produced
 * @param expected Serial the vector calls for
 * @return true if they match
 */
static bool check_serial(const char *name, const uint8_t *actual, const uint8_t *expected)
{
    if (memcmp(actual, expected, DEVICE_SERIAL_SIZE) == 0) {
        return true;
    }

    fail(name, "derived serial does not match the vector");
    print_serial("got ", actual);
    print_serial("want", expected);
    return false;
}

/**
 * @brief Derive one vector's MAC and compare, both directly and through the eFuse read
 *
 * @param vector Case to run; failures are counted, not returned
 */
static void run_vector(const serial_vector_t *vector)
{
    uint8_t derived[DEVICE_SERIAL_SIZE];

    device_serial_derive(vector->mac, derived);
    if (!check_serial(vector->name, derived, vector->serial)) {
        return;
    }

    // The reserved all-zero value must never be reachable from a MAC the
    // hardware could actually hold, or a healthy unit would report the value
    // that means "cannot tell you".
    uint8_t zero[DEVICE_SERIAL_SIZE] = {0};
    if (memcmp(derived, zero, DEVICE_SERIAL_SIZE) == 0) {
        fail(vector->name, "derives to the reserved all-zero serial");
        return;
    }

    host_efuse_set_mac(vector->mac);

    uint8_t from_efuse[DEVICE_SERIAL_SIZE];
    device_serial_get(from_efuse);
    if (!check_serial(vector->name, from_efuse, vector->serial)) {
        return;
    }

    // "Identical across reboots" is this function being a function of the
    // eFuse MAC alone, with no state carried between calls.
    uint8_t again[DEVICE_SERIAL_SIZE];
    device_serial_get(again);
    check_serial(vector->name, again, vector->serial);
}

/**
 * @brief Check that a failed eFuse read yields the reserved all-zero serial
 */
static void run_efuse_failure(void)
{
    const char *name = "efuse_read_failure";
    uint8_t zero[DEVICE_SERIAL_SIZE] = {0};
    uint8_t serial[DEVICE_SERIAL_SIZE];

    memset(serial, 0x5a, sizeof(serial));
    host_efuse_set_failure();
    device_serial_get(serial);

    if (memcmp(serial, zero, DEVICE_SERIAL_SIZE) != 0) {
        fail(name, "a failed eFuse read did not produce six zero bytes");
        print_serial("got ", serial);
        return;
    }

    printf("  ok   %s\n", name);
}

/**
 * @brief Check that a failed hash yields the reserved all-zero serial
 *
 * Without this the digest buffer stays uninitialised and the record carries six
 * bytes of stack, which S11.8 forbids as a made-up value.
 */
static void run_sha_failure(void)
{
    const char *name = "sha_failure";
    uint8_t zero[DEVICE_SERIAL_SIZE] = {0};
    uint8_t serial[DEVICE_SERIAL_SIZE];

    memset(serial, 0x5a, sizeof(serial));
    host_efuse_set_mac(SERIAL_VECTORS[0].mac);
    host_sha_set_failure();
    device_serial_get(serial);
    host_sha_set_success();

    if (memcmp(serial, zero, DEVICE_SERIAL_SIZE) != 0) {
        fail(name, "a failed hash did not produce six zero bytes");
        print_serial("got ", serial);
        return;
    }

    printf("  ok   %s\n", name);
}

/**
 * @brief Check that distinct MACs give distinct serials
 */
static void run_distinctness(void)
{
    const char *name = "distinct_macs_distinct_serials";

    for (size_t i = 0; i < SERIAL_VECTOR_COUNT; i++) {
        for (size_t j = i + 1; j < SERIAL_VECTOR_COUNT; j++) {
            if (memcmp(SERIAL_VECTORS[i].serial, SERIAL_VECTORS[j].serial, DEVICE_SERIAL_SIZE) == 0) {
                fail(name, "%s and %s share a serial", SERIAL_VECTORS[i].name, SERIAL_VECTORS[j].name);
                return;
            }
        }
    }

    printf("  ok   %s\n", name);
}

int main(void)
{
    printf("device serial conformance: %zu vectors\n", (size_t)SERIAL_VECTOR_COUNT);

    for (size_t i = 0; i < SERIAL_VECTOR_COUNT; i++) {
        int failures_before = failures;
        run_vector(&SERIAL_VECTORS[i]);
        if (failures == failures_before) {
            printf("  ok   %s\n", SERIAL_VECTORS[i].name);
        }
    }

    run_distinctness();
    run_efuse_failure();
    run_sha_failure();

    if (failures > 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }

    printf("the derivation matches the independent vectors\n");
    return 0;
}
