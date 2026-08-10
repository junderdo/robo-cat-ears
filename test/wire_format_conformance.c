/*
 * Description: Host test pinning the animation wire format to the shared fixture
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The fixture is shared with the web app (docs/spec/wire-format-fixture.json in
 * github.com/junderdo/milk-lab-creations), which tests packWireFormat against
 * the same bytes. If this fails, the two implementations have drifted.
 *
 * Runs on the host: custom_animation_types.h is header-only and pulls in no
 * ESP-IDF. Build with `make -C test`.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "custom_animation_types.h"
#include "wire_format_fixture.h"

static int failures = 0;

/**
 * @brief Record a failed expectation against a fixture case
 *
 * @param name Fixture case name
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
 * @brief Report the first byte at which two buffers differ
 *
 * @param name Fixture case name
 * @param actual Bytes produced by the serializer
 * @param expected Bytes the fixture calls for
 * @param len Length of both buffers
 */
static void report_byte_mismatch(const char *name, const uint8_t *actual, const uint8_t *expected, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (actual[i] != expected[i]) {
            fail(name, "byte %u is 0x%02x, fixture says 0x%02x", i, actual[i], expected[i]);
            return;
        }
    }
}

/**
 * @brief Compare a deserialized animation against the fixture's keyframes
 *
 * @param name Fixture case name
 * @param actual Animation produced by custom_animation_deserialize
 * @param expected Keyframes the fixture calls for
 * @return true if every field of every keyframe matches
 */
static bool check_keyframes_match(const char *name, const custom_animation_t *actual, const custom_animation_t *expected)
{
    if (actual->keyframe_count != expected->keyframe_count) {
        fail(name, "deserialize produced %u keyframes, fixture has %u", actual->keyframe_count,
             expected->keyframe_count);
        return false;
    }

    for (uint8_t i = 0; i < expected->keyframe_count; i++) {
        const animation_keyframe_t *a = &actual->keyframes[i];
        const animation_keyframe_t *e = &expected->keyframes[i];

        if (a->time_ms != e->time_ms || a->ease_in_type != e->ease_in_type || a->ease_out_type != e->ease_out_type ||
            a->ease_in_ms != e->ease_in_ms || a->ease_out_ms != e->ease_out_ms ||
            memcmp(a->angles, e->angles, sizeof(e->angles)) != 0) {
            fail(name, "keyframe %u differs from the fixture", i);
            printf("       got  time=%u angles=%u,%u,%u,%u ease=%u/%u %u/%u ms\n", a->time_ms, a->angles[0],
                   a->angles[1], a->angles[2], a->angles[3], a->ease_in_type, a->ease_out_type, a->ease_in_ms,
                   a->ease_out_ms);
            printf("       want time=%u angles=%u,%u,%u,%u ease=%u/%u %u/%u ms\n", e->time_ms, e->angles[0],
                   e->angles[1], e->angles[2], e->angles[3], e->ease_in_type, e->ease_out_type, e->ease_in_ms,
                   e->ease_out_ms);
            return false;
        }
    }

    return true;
}

/**
 * @brief Serialize, deserialize and round trip one fixture case
 *
 * @param fixture Case to run; failures are counted, not returned
 */
static void run_case(const fixture_case_t *fixture)
{
    uint8_t serialized[CUSTOM_ANIMATION_MAX_SERIALIZED_SIZE];
    uint16_t serialized_len = 0;

    if (!custom_animation_serialize(&fixture->anim, serialized, &serialized_len)) {
        fail(fixture->name, "serialize rejected the fixture animation");
        return;
    }

    if (serialized_len != fixture->byte_len) {
        fail(fixture->name, "serialize wrote %u bytes, fixture is %u", serialized_len, fixture->byte_len);
        return;
    }

    if (memcmp(serialized, fixture->bytes, serialized_len) != 0) {
        report_byte_mismatch(fixture->name, serialized, fixture->bytes, serialized_len);
        return;
    }

    custom_animation_t decoded;
    memset(&decoded, 0, sizeof(decoded));

    if (!custom_animation_deserialize(fixture->bytes, fixture->byte_len, &decoded)) {
        fail(fixture->name, "deserialize rejected the fixture bytes");
        return;
    }

    if (!check_keyframes_match(fixture->name, &decoded, &fixture->anim)) {
        return;
    }

    uint8_t reserialized[CUSTOM_ANIMATION_MAX_SERIALIZED_SIZE];
    uint16_t reserialized_len = 0;

    if (!custom_animation_serialize(&decoded, reserialized, &reserialized_len)) {
        fail(fixture->name, "serialize rejected the deserialized animation");
        return;
    }

    if (reserialized_len != fixture->byte_len || memcmp(reserialized, fixture->bytes, reserialized_len) != 0) {
        fail(fixture->name, "round trip did not reproduce the fixture bytes");
    }
}

int main(void)
{
    printf("wire format conformance: %zu cases\n", (size_t)FIXTURE_CASE_COUNT);

    for (size_t i = 0; i < FIXTURE_CASE_COUNT; i++) {
        int failures_before = failures;
        run_case(&FIXTURE_CASES[i]);
        if (failures == failures_before) {
            printf("  ok   %s\n", FIXTURE_CASES[i].name);
        }
    }

    if (failures > 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }

    printf("all cases match the shared fixture\n");
    return 0;
}
