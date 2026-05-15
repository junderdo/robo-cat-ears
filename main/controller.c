/*
 * Description: Controller for handling BLE commands and coordinating device actions
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "controller.h"
#include "servo.h"
#include "led.h"
#include "esp_log.h"
#include "types/ble_packet_types.h"
#include <string.h>

#define CONTROLLER_TAG "CONTROLLER"

// Characteristic handle for data
#define DATA_HANDLE 42

// Define animation function pointer type
typedef void (*animation_func_t)(void);

// Array of animation functions (index 0 is unused, animations are 1-6)
static const animation_func_t animation_functions[] = {
    NULL,           // Index 0 - unused
    do_animation_1, // Index 1
    do_animation_2, // Index 2
    do_animation_3, // Index 3
    do_animation_4, // Index 4
    do_animation_5, // Index 5
    do_animation_6, // Index 6
};

#define NUM_ANIMATIONS (sizeof(animation_functions) / sizeof(animation_functions[0]) - 1)

// Array of lighting task functions (indices match lighting_mode_t enum)
static const lighting_task_func_t lighting_task_functions[] = {
    led_solid_task,     // Index 0 - LIGHTING_MODE_SOLID
    led_breathing_task, // Index 1 - LIGHTING_MODE_BREATHING
    led_marquee_task,   // Index 2 - LIGHTING_MODE_MARQUEE
    led_chasing_task,   // Index 3 - LIGHTING_MODE_CHASING
    led_rain_task,      // Index 4 - LIGHTING_MODE_RAIN
};

#define NUM_LIGHTING_MODES (sizeof(lighting_task_functions) / sizeof(lighting_task_functions[0]))

/**
 * @brief Process animation command and execute the corresponding animation
 *
 * @param command_data Command data buffer containing animation number
 * @param data_len Length of command data
 */
static void process_animation_command(const uint8_t *command_data, uint16_t data_len)
{
    if (command_data == NULL || data_len == 0)
    {
        ESP_LOGE(CONTROLLER_TAG, "Invalid command data");
        return;
    }

    // Parse animation number from data (assuming single byte or ASCII digit)
    uint8_t animation_num;

    // Check if it's an ASCII digit ('1'-'6') or raw byte (1-6)
    if (command_data[0] >= '1' && command_data[0] <= '9')
    {
        animation_num = command_data[0] - '0'; // Convert ASCII to number
    }
    else
    {
        animation_num = command_data[0]; // Use raw byte value
    }

    // Bounds check
    if (animation_num < 1 || animation_num > NUM_ANIMATIONS)
    {
        ESP_LOGE(CONTROLLER_TAG, "Animation number %d out of bounds (valid: 1-%d)",
                 animation_num, NUM_ANIMATIONS);
        return;
    }

    // Execute animation function
    ESP_LOGI(CONTROLLER_TAG, "Executing Animation %d", animation_num);
    animation_functions[animation_num]();
}

/**
 * @brief Process lighting command and start the corresponding LED animation task
 *
 * @param packet Pointer to the unpacked data packet containing lighting data
 */
static void process_lighting_command(const data_packet_t *packet)
{
    if (packet == NULL) {
        ESP_LOGE(CONTROLLER_TAG, "Invalid packet pointer");
        return;
    }

    lighting_data_t lighting;
    if (!data_packet_get_lighting(packet, &lighting)) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to deserialize lighting data");
        return;
    }

    ESP_LOGI(CONTROLLER_TAG, "Lighting command received: mode=%d, speed=%d, colors=%d",
             lighting.mode, lighting.speed, lighting.color_count);

    // Bounds check on mode
    if (lighting.mode >= NUM_LIGHTING_MODES) {
        ESP_LOGE(CONTROLLER_TAG, "Lighting mode %d out of bounds (valid: 0-%d)",
                 lighting.mode, NUM_LIGHTING_MODES - 1);
        return;
    }

    // Task names for each lighting mode
    const char *task_names[] = {
        "led_solid", "led_breathing", "led_marquee", "led_chasing", "led_rain"
    };

    // Start the LED animation task with the selected mode
    esp_err_t ret = led_start_task(
        lighting_task_functions[lighting.mode],
        task_names[lighting.mode],
        &lighting
    );

    if (ret != ESP_OK) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to start LED task for mode %d", lighting.mode);
    } else {
        ESP_LOGI(CONTROLLER_TAG, "Started LED animation: %s", task_names[lighting.mode]);
    }
}

void controller_handle_read(esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(CONTROLLER_TAG, "Characteristic read, conn_id %d, handle %d",
             param->read.conn_id, param->read.handle);
    // Add read handling logic here as needed
}

void controller_handle_write(esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(CONTROLLER_TAG, "Characteristic write, conn_id %d, handle %d",
             param->write.conn_id, param->write.handle);
    ESP_LOGI(CONTROLLER_TAG, "Raw packet data: %.*s", param->write.len, param->write.value);

    // Check if this is the data characteristic
    if (param->write.handle == DATA_HANDLE)
    {
        // Unpack the data packet and process the command
        data_packet_t packet;
        if (data_packet_unpack(param->write.value, param->write.len, &packet))
        {
            if (packet.type == DATA_TYPE_ANIMATION)
            {
                process_animation_command(packet.data, packet.data_len);
            }
            else if (packet.type == DATA_TYPE_LIGHTING)
            {
                process_lighting_command(&packet);
            }
            else
            {
                ESP_LOGI(CONTROLLER_TAG, "Unhandled data type: %d", packet.type);
            }
        }
        else
        {
            ESP_LOGI(CONTROLLER_TAG, "Failed to unpack data packet");
        }
    }
    else
    {
        ESP_LOGI(CONTROLLER_TAG, "Write to unhandled characteristic handle: %d", param->write.handle);
    }

    // TODO: enable larger data input using prepared writes, and handle them in execute write event
}
