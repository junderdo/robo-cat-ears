/*
 * Description: Transport for the 0x06 animation store surface (docs/ble-protocol.md S5, S11.7)
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "store.h"
#include "ble.h"
#include "animation_store.h"
#include "types/ble_packet_types.h"
#include "types/custom_animation_types.h"
#include "types/store_types.h"

#define STORE_TAG "STORE"

// Reclaims the buffer from a client that vanished without disconnecting. A merely
// slow client sees NO_ACTIVE_TRANSFER on its next chunk, which says the same thing.
#define STORE_CHUNK_TIMEOUT_US (5 * 1000 * 1000)

// Reassembly state. The connection is exclusive and chunks arrive in order, so a
// transfer is tracked by the next chunk expected rather than a received-set.
static uint8_t request_buffer[STORE_REQUEST_MAX_PAYLOAD_SIZE];
static uint16_t request_len = 0;
static uint8_t request_corr = 0;
static uint8_t request_opcode = 0;
static uint8_t request_chunk_count = 0;
static uint8_t request_next_chunk = 0;
static int64_t request_last_chunk_us = 0;

static bool transfer_active(void)
{
    return request_chunk_count > 0;
}

static uint16_t max_chunk_bytes(void)
{
    uint16_t negotiated = ble_get_max_chunk_bytes();
    return negotiated < STORE_MAX_FRAME_SIZE ? negotiated : STORE_MAX_FRAME_SIZE;
}

static void respond(uint8_t corr, store_status_t status, const uint8_t *payload, uint16_t payload_len)
{
    // Only the controller task responds, one response at a time, so one frame buffer serves
    static uint8_t frame[STORE_MAX_FRAME_SIZE];

    uint16_t payload_per_chunk = max_chunk_bytes() - STORE_FRAME_HEADER_SIZE;
    uint16_t chunk_count = payload_len == 0 ? 1 : (payload_len + payload_per_chunk - 1) / payload_per_chunk;

    for (uint16_t chunk_index = 0; chunk_index < chunk_count; chunk_index++)
    {
        uint16_t offset = chunk_index * payload_per_chunk;
        uint16_t chunk_len = payload_len - offset;
        if (chunk_len > payload_per_chunk)
        {
            chunk_len = payload_per_chunk;
        }

        frame[0] = DATA_TYPE_STORE;
        frame[1] = corr;
        frame[2] = status;
        frame[3] = (uint8_t)chunk_index;
        frame[4] = (uint8_t)chunk_count;
        if (chunk_len > 0)
        {
            memcpy(&frame[STORE_FRAME_HEADER_SIZE], &payload[offset], chunk_len);
        }

        if (!ble_indicate_data_notify(frame, STORE_FRAME_HEADER_SIZE + chunk_len))
        {
            ESP_LOGW(STORE_TAG, "Response to corr %u failed at chunk %u of %u", corr, chunk_index + 1, chunk_count);
            return;
        }
    }
}

static void respond_capability(uint8_t corr)
{
    uint16_t chunk_bytes = max_chunk_bytes();
    const uint8_t record[] = {
        STORE_PROTOCOL_VERSION,
        STORE_SLOT_COUNT,
        (uint8_t)(chunk_bytes >> 8),
        (uint8_t)(chunk_bytes & 0xff),
    };

    ESP_LOGI(STORE_TAG, "CAPABILITY: version %d, %d slots, %u max chunk bytes",
             STORE_PROTOCOL_VERSION, STORE_SLOT_COUNT, chunk_bytes);
    respond(corr, STORE_STATUS_OK, record, sizeof(record));
}

// Bytes of a slot record that precede the name, i.e. [animation_id:16][name_len:1].
// A STORE request payload is one slot byte followed by exactly such a record.
#define STORE_RECORD_PREFIX_SIZE (STORE_ANIMATION_ID_SIZE + 1)

/**
 * @brief Whether a name is 1-32 bytes of well-formed UTF-8 with no control characters
 */
static bool name_is_valid(const uint8_t *name, uint8_t name_len)
{
    // The lowest code point each sequence length is allowed to encode, indexed by
    // trailing-byte count. Anything below is an overlong encoding.
    static const uint32_t shortest_encoding[] = {0, 0x80, 0x800, 0x10000};

    if (name_len < STORE_NAME_MIN_BYTES || name_len > STORE_NAME_MAX_BYTES)
    {
        return false;
    }

    for (uint8_t i = 0; i < name_len;)
    {
        uint8_t lead = name[i];
        uint8_t trailing;
        uint32_t code_point;

        if (lead < 0x80)
        {
            if (lead < 0x20 || lead == 0x7f)
            {
                return false;
            }
            i++;
            continue;
        }
        else if ((lead & 0xe0) == 0xc0)
        {
            trailing = 1;
            code_point = lead & 0x1f;
        }
        else if ((lead & 0xf0) == 0xe0)
        {
            trailing = 2;
            code_point = lead & 0x0f;
        }
        else if ((lead & 0xf8) == 0xf0)
        {
            trailing = 3;
            code_point = lead & 0x07;
        }
        else
        {
            return false;
        }

        if (i + trailing >= name_len)
        {
            return false;
        }

        for (uint8_t t = 1; t <= trailing; t++)
        {
            uint8_t byte = name[i + t];
            if ((byte & 0xc0) != 0x80)
            {
                return false;
            }
            code_point = (code_point << 6) | (byte & 0x3f);
        }

        if (code_point < shortest_encoding[trailing] || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff))
        {
            return false;
        }

        i += trailing + 1;
    }

    return true;
}

static void handle_list(uint8_t corr)
{
    // The controller task is the only caller, and both are too large for its stack
    static uint8_t response[STORE_LIST_MAX_RESPONSE_SIZE];
    static uint8_t record[STORE_RECORD_MAX_SIZE];

    uint8_t entry_count = 0;
    uint16_t response_len = 1;

    for (uint8_t slot = 0; slot < STORE_SLOT_COUNT; slot++)
    {
        uint16_t record_len = STORE_RECORD_MAX_SIZE;
        esp_err_t err = animation_store_read(slot, record, &record_len);
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            continue;
        }
        if (err != ESP_OK)
        {
            ESP_LOGW(STORE_TAG, "Skipping slot %u in LIST: %s", slot, esp_err_to_name(err));
            continue;
        }

        // A record is validated whole before it is written, so a mis-shaped one means
        // corrupt flash. Bounding name_len by the same cap STORE enforces is what keeps
        // the response within STORE_LIST_MAX_RESPONSE_SIZE.
        uint8_t name_len = record_len < STORE_RECORD_PREFIX_SIZE ? 0 : record[STORE_ANIMATION_ID_SIZE];
        if (name_len < STORE_NAME_MIN_BYTES || name_len > STORE_NAME_MAX_BYTES ||
            record_len < STORE_RECORD_PREFIX_SIZE + name_len)
        {
            ESP_LOGW(STORE_TAG, "Skipping slot %u in LIST: %u byte record names %u bytes",
                     slot, record_len, name_len);
            continue;
        }

        response[response_len++] = slot;
        memcpy(&response[response_len], record, STORE_RECORD_PREFIX_SIZE + name_len);
        response_len += STORE_RECORD_PREFIX_SIZE + name_len;
        entry_count++;
    }

    response[0] = entry_count;

    ESP_LOGI(STORE_TAG, "LIST: %u of %d slots occupied, %u bytes", entry_count, STORE_SLOT_COUNT, response_len);
    respond(corr, STORE_STATUS_OK, response, response_len);
}

static void handle_store(uint8_t corr, const uint8_t *payload, uint16_t payload_len)
{
    // Too large for the controller task's stack, and only that task gets here
    static custom_animation_t animation;

    if (payload_len < 1 + STORE_RECORD_PREFIX_SIZE)
    {
        ESP_LOGW(STORE_TAG, "STORE payload is %u bytes, too short for its prefix", payload_len);
        respond(corr, STORE_STATUS_MALFORMED_REQUEST, NULL, 0);
        return;
    }

    // The payload is a slot byte followed by the record that slot will hold verbatim
    uint8_t slot = payload[0];
    const uint8_t *record = &payload[1];
    uint16_t record_len = payload_len - 1;
    uint8_t name_len = record[STORE_ANIMATION_ID_SIZE];

    if (record_len < STORE_RECORD_PREFIX_SIZE + name_len)
    {
        ESP_LOGW(STORE_TAG, "STORE payload is %u bytes, too short for a %u byte name", payload_len, name_len);
        respond(corr, STORE_STATUS_MALFORMED_REQUEST, NULL, 0);
        return;
    }

    if (slot >= STORE_SLOT_COUNT)
    {
        ESP_LOGW(STORE_TAG, "STORE into slot %u, beyond %d slots", slot, STORE_SLOT_COUNT);
        respond(corr, STORE_STATUS_SLOT_OUT_OF_RANGE, NULL, 0);
        return;
    }

    // Runs before the animation validator so a bad name does not cost a deserialize
    const uint8_t *name = &record[STORE_RECORD_PREFIX_SIZE];
    if (!name_is_valid(name, name_len))
    {
        ESP_LOGW(STORE_TAG, "STORE into slot %u has an invalid %u byte name", slot, name_len);
        respond(corr, STORE_STATUS_INVALID_NAME, NULL, 0);
        return;
    }

    const uint8_t *wire_format = name + name_len;
    uint16_t wire_format_len = record_len - STORE_RECORD_PREFIX_SIZE - name_len;

    // custom_animation_deserialize tolerates surplus bytes; persisting them would make
    // client and ears disagree about what was sent, permanently (S11.4)
    if (wire_format_len < 1 ||
        wire_format_len != 1 + (uint16_t)wire_format[0] * CUSTOM_ANIMATION_KEYFRAME_SIZE)
    {
        ESP_LOGW(STORE_TAG, "STORE into slot %u has %u wire format bytes, not the exact length",
                 slot, wire_format_len);
        respond(corr, STORE_STATUS_MALFORMED_REQUEST, NULL, 0);
        return;
    }

    // The same gate stream-and-play uses, so both accept exactly the same animations (S9.3)
    if (!custom_animation_deserialize(wire_format, wire_format_len, &animation))
    {
        ESP_LOGW(STORE_TAG, "STORE into slot %u failed to deserialize", slot);
        respond(corr, STORE_STATUS_INVALID_ANIMATION, NULL, 0);
        return;
    }

    esp_err_t err = animation_store_write(slot, record, record_len);
    if (err != ESP_OK)
    {
        respond(corr, STORE_STATUS_STORAGE_FAILURE, NULL, 0);
        return;
    }

    ESP_LOGI(STORE_TAG, "STORE: slot %u holds %u keyframes named '%.*s'",
             slot, animation.keyframe_count, name_len, name);
    respond(corr, STORE_STATUS_OK, NULL, 0);
}

static void dispatch(uint8_t corr, uint8_t opcode, const uint8_t *payload, uint16_t payload_len)
{
    switch (opcode)
    {
    case STORE_OPCODE_CAPABILITY:
        respond_capability(corr);
        break;
    case STORE_OPCODE_LIST:
        handle_list(corr);
        break;
    case STORE_OPCODE_STORE:
        handle_store(corr, payload, payload_len);
        break;
    default:
        ESP_LOGW(STORE_TAG, "Unsupported sub-opcode 0x%02x (%u byte payload)", opcode, payload_len);
        respond(corr, STORE_STATUS_UNSUPPORTED_OPCODE, NULL, 0);
        break;
    }
}

void store_process_request(const uint8_t *data, uint16_t data_len)
{
    if (data == NULL)
    {
        return;
    }

    if (!ble_is_data_notify_enabled())
    {
        ESP_LOGW(STORE_TAG, "Dropping store request: ABF2 is not subscribed for indications");
        return;
    }

    if (data_len < STORE_REQUEST_HEADER_SIZE)
    {
        ESP_LOGW(STORE_TAG, "Store request is %u bytes, shorter than its header", data_len);
        if (data_len >= 1)
        {
            respond(data[0], STORE_STATUS_MALFORMED_REQUEST, NULL, 0);
        }
        return;
    }

    uint8_t corr = data[0];
    uint8_t opcode = data[1];
    uint8_t chunk_index = data[2];
    uint8_t chunk_count = data[3];
    const uint8_t *payload = &data[STORE_REQUEST_HEADER_SIZE];
    uint16_t payload_len = data_len - STORE_REQUEST_HEADER_SIZE;

    // A frame this malformed belongs to no transfer, so it does not kill one in flight
    if (chunk_count == 0 || chunk_index >= chunk_count)
    {
        ESP_LOGW(STORE_TAG, "Store request has chunk %u of %u", chunk_index, chunk_count);
        respond(corr, STORE_STATUS_MALFORMED_REQUEST, NULL, 0);
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (transfer_active() && (now_us - request_last_chunk_us) > STORE_CHUNK_TIMEOUT_US)
    {
        ESP_LOGW(STORE_TAG, "Discarding transfer %u: no chunk within %d s", request_corr,
                 STORE_CHUNK_TIMEOUT_US / 1000000);
        store_reset();
    }

    if (chunk_index == 0)
    {
        // A new transfer supersedes anything still in flight
        request_corr = corr;
        request_opcode = opcode;
        request_chunk_count = chunk_count;
        request_next_chunk = 0;
        request_len = 0;
    }
    else if (!transfer_active())
    {
        ESP_LOGW(STORE_TAG, "Chunk %u of transfer %u arrived with nothing in flight", chunk_index, corr);
        respond(corr, STORE_STATUS_NO_ACTIVE_TRANSFER, NULL, 0);
        return;
    }
    else if (corr != request_corr || opcode != request_opcode ||
             chunk_count != request_chunk_count || chunk_index != request_next_chunk)
    {
        ESP_LOGW(STORE_TAG, "Discarding transfer %u: got chunk %u, expected %u",
                 request_corr, chunk_index, request_next_chunk);
        store_reset();
        respond(corr, STORE_STATUS_CHUNK_OUT_OF_ORDER, NULL, 0);
        return;
    }

    if (request_len + payload_len > sizeof(request_buffer))
    {
        ESP_LOGW(STORE_TAG, "Discarding transfer %u: exceeds %d bytes", corr, (int)sizeof(request_buffer));
        store_reset();
        respond(corr, STORE_STATUS_TOO_LARGE, NULL, 0);
        return;
    }

    memcpy(&request_buffer[request_len], payload, payload_len);
    request_len += payload_len;
    request_next_chunk++;
    request_last_chunk_us = now_us;

    if (request_next_chunk < request_chunk_count)
    {
        return;
    }

    uint16_t complete_len = request_len;
    store_reset();
    dispatch(corr, opcode, request_buffer, complete_len);
}

void store_reset(void)
{
    request_chunk_count = 0;
    request_next_chunk = 0;
    request_len = 0;
}
