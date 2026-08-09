/*
 * Description: Transport for the 0x06 animation store surface (docs/ble-protocol.md S5, S11.7)
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

#include "store.h"
#include "ble.h"
#include "types/ble_packet_types.h"
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

static void dispatch(uint8_t corr, uint8_t opcode, const uint8_t *payload, uint16_t payload_len)
{
    switch (opcode)
    {
    case STORE_OPCODE_CAPABILITY:
        respond_capability(corr);
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
