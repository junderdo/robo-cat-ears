/*
 * Description: Controller for handling BLE commands and coordinating device actions
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_gatts_api.h"

#include "controller.h"
#include "types/ble_packet_types.h"
#include "ble.h"
#include "animation.h"
#include "servo.h"
#include "servo_calibration.h"
#include "animation_mode.h"
#include "led.h"

#define CONTROLLER_TAG "CONTROLLER"

// Characteristic handle for data
#define DATA_HANDLE 42

// Command queue configuration
#define COMMAND_QUEUE_LENGTH 10
#define CONTROLLER_TASK_STACK_SIZE 4096
#define CONTROLLER_TASK_PRIORITY 4

// Command types
typedef enum {
    CMD_TYPE_ANIMATION,
    CMD_TYPE_LIGHTING,
    CMD_TYPE_SERVO_CALIBRATION,
    CMD_TYPE_ANIMATION_MODE,
} command_type_t;

// Command structure for queue
typedef struct {
    command_type_t type;
    data_packet_t packet;
} controller_command_t;

// Queue and task handles
static QueueHandle_t command_queue = NULL;
static TaskHandle_t controller_task_handle = NULL;

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
    uint8_t animation_id;

    // Check if it's an ASCII digit ('1'-'6') or raw byte (1-6)
    if (command_data[0] >= '1' && command_data[0] <= '9')
    {
        animation_id = command_data[0] - '0'; // Convert ASCII to number
    }
    else
    {
        animation_id = command_data[0]; // Use raw byte value
    }

    // Bounds check
    if (animation_id < 1 || animation_id > NUM_ANIMATIONS)
    {
        ESP_LOGE(CONTROLLER_TAG, "Animation ID %d out of bounds (valid: 1-%d)",
                 animation_id, NUM_ANIMATIONS);
        return;
    }

    // Execute animation function
    ESP_LOGI(CONTROLLER_TAG, "Executing Animation ID %d", animation_id);
    do_animation(animation_id);
}

/**
 * @brief Update the BLE characteristic value with current lighting data
 *
 * This function updates the DATA_HANDLE characteristic value so that when
 * a client reads it, they get the current lighting configuration.
 */
void controller_update_lighting_characteristic(void)
{
    // Use static storage to avoid stack overflow in BLE callback context
    static lighting_data_t current_lighting;
    static uint8_t packed_data[BLE_PACKET_MAX_SIZE];
    
    // Get the actual GATT handle for the DATA NOTIFY characteristic (ABF2)
    // This is the proper characteristic for server-to-client data
    uint16_t handle = ble_get_data_notify_handle();
    if (handle == 0) {
        ESP_LOGW(CONTROLLER_TAG, "Data notify handle not initialized yet");
        return;
    }
    
    // Load current lighting configuration
    esp_err_t err = led_load_config(&current_lighting);
    
    if (err != ESP_OK) {
        ESP_LOGW(CONTROLLER_TAG, "Failed to load lighting config, using default");
        // Use default/empty lighting data
        memset(&current_lighting, 0, sizeof(lighting_data_t));
        current_lighting.mode = LIGHTING_MODE_SOLID;
        current_lighting.speed = 50;
        current_lighting.color_count = 1;
        current_lighting.colors[0].r = 255;
        current_lighting.colors[0].g = 255;
        current_lighting.colors[0].b = 255;
    }

    ESP_LOGI(CONTROLLER_TAG, "Updating characteristic with lighting data: mode=%d, speed=%d, colors=%d",
             current_lighting.mode, current_lighting.speed, current_lighting.color_count);

    // Pack lighting data into a data packet
    data_packet_t response_packet;
    if (!data_packet_pack_lighting(&response_packet, &current_lighting)) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to pack lighting data");
        return;
    }

    // Pack the data packet into a byte array
    uint16_t packed_len;
    if (!data_packet_pack(&response_packet, packed_data, &packed_len)) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to pack data packet");
        return;
    }

    // Update BOTH the buffer AND the GATT attribute value for ESP_GATT_AUTO_RSP
    // The buffer pointer in the attribute table points to spp_data_notify_val
    uint8_t* notify_buffer = ble_get_data_notify_buffer();
    memcpy(notify_buffer, packed_data, packed_len);
    
    // CRITICAL: Update the attribute length in GATT table so AUTO_RSP knows the actual size
    esp_err_t ret = esp_ble_gatts_set_attr_value(handle, packed_len, packed_data);
    if (ret != ESP_OK) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to update characteristic value (handle=%d): %d", handle, ret);
    } else {
        ESP_LOGI(CONTROLLER_TAG, "Updated characteristic value (handle=%d, %d bytes)", handle, packed_len);
    }
}

/**
 * @brief Update the BLE characteristic value with current servo calibration data
 *
 * This function updates the DATA_HANDLE characteristic value so that when
 * a client reads it, they get the current servo calibration configuration.
 */
void controller_update_servo_calibration_characteristic(void)
{
    // Use static storage to avoid stack overflow in BLE callback context
    static servo_calibration_t current_calibration;
    static uint8_t packed_data[BLE_PACKET_MAX_SIZE];
    
    // Get the actual GATT handle for the DATA NOTIFY characteristic (ABF2)
    // This is the proper characteristic for server-to-client data
    uint16_t handle = ble_get_data_notify_handle();
    if (handle == 0) {
        ESP_LOGW(CONTROLLER_TAG, "Data notify handle not initialized yet");
        return;
    }
    
    // Load current servo calibration
    esp_err_t err = servo_load_calibration(&current_calibration);
    
    if (err != ESP_OK) {
        ESP_LOGW(CONTROLLER_TAG, "Failed to load servo calibration, using default");
        // Use default/zero calibration data
        servo_calibration_init(&current_calibration);
    }

    ESP_LOGI(CONTROLLER_TAG, "Updating characteristic with servo calibration: left_azi=%d, left_lat=%d, right_azi=%d, right_lat=%d",
             current_calibration.left_azi, current_calibration.left_lat, 
             current_calibration.right_azi, current_calibration.right_lat);

    // Pack servo calibration data into a data packet
    data_packet_t response_packet;

    uint16_t serialized_len = 0;
    // Manually serialize to check length
    servo_calibration_serialize(&current_calibration, response_packet.data, &serialized_len);
    ESP_LOGI(CONTROLLER_TAG, "[DEBUG] servo_calibration_serialize len = %d", serialized_len);

    if (!data_packet_pack_servo_calibration(&response_packet, &current_calibration)) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to pack servo calibration data");
        return;
    }

    ESP_LOGI(CONTROLLER_TAG, "[DEBUG] data_packet_t.data_len = %d", response_packet.data_len);

    // Pack the data packet into a byte array
    uint16_t packed_len;
    if (!data_packet_pack(&response_packet, packed_data, &packed_len)) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to pack data packet");
        return;
    }
    ESP_LOGI(CONTROLLER_TAG, "[DEBUG] packed_len = %d", packed_len);

    // Update BOTH the buffer AND the GATT attribute value for ESP_GATT_AUTO_RSP
    // The buffer pointer in the attribute table points to spp_data_notify_val
    uint8_t* notify_buffer = ble_get_data_notify_buffer();
    memcpy(notify_buffer, packed_data, packed_len);
    
    // CRITICAL: Update the attribute length in GATT table so AUTO_RSP knows the actual size
    esp_err_t ret = esp_ble_gatts_set_attr_value(handle, packed_len, packed_data);
    if (ret != ESP_OK) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to update characteristic value (handle=%d): %d", handle, ret);
    } else {
        ESP_LOGI(CONTROLLER_TAG, "Updated characteristic value (handle=%d, %d bytes)", handle, packed_len);
    }
}

/**
 * @brief Update the BLE characteristic value with current animation mode data
 */
void controller_update_animation_mode_characteristic(void)
{
    static animation_mode_t current_mode;
    static uint8_t packed_data[BLE_PACKET_MAX_SIZE];

    uint16_t handle = ble_get_data_notify_handle();
    if (handle == 0) {
        ESP_LOGW(CONTROLLER_TAG, "Data notify handle not initialized yet");
        return;
    }

    esp_err_t err = animation_mode_load(&current_mode);
    if (err != ESP_OK) {
        ESP_LOGW(CONTROLLER_TAG, "Failed to load animation mode, using default");
        animation_mode_init(&current_mode);
    }

    ESP_LOGI(CONTROLLER_TAG, "Updating characteristic with animation mode: mode_id=%d, frequency=%d",
             current_mode.mode_id, current_mode.frequency);

    data_packet_t response_packet;
    if (!data_packet_pack_animation_mode(&response_packet, &current_mode)) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to pack animation mode data");
        return;
    }

    uint16_t packed_len;
    if (!data_packet_pack(&response_packet, packed_data, &packed_len)) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to pack data packet");
        return;
    }

    uint8_t *notify_buffer = ble_get_data_notify_buffer();
    memcpy(notify_buffer, packed_data, packed_len);

    esp_err_t ret = esp_ble_gatts_set_attr_value(handle, packed_len, packed_data);
    if (ret != ESP_OK) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to update characteristic value (handle=%d): %d", handle, ret);
    } else {
        ESP_LOGI(CONTROLLER_TAG, "Updated characteristic value (handle=%d, %d bytes)", handle, packed_len);
    }
}

/**
 * @brief Process animation mode command and save animation mode data
 *
 * @param packet Pointer to the unpacked data packet containing animation mode data
 */
static void process_animation_mode_command(const data_packet_t *packet)
{
    if (packet == NULL) {
        ESP_LOGE(CONTROLLER_TAG, "Invalid packet pointer for animation mode");
        return;
    }

    animation_mode_t mode;
    if (!data_packet_get_animation_mode(packet, &mode)) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to deserialize animation mode data");
        return;
    }

    ESP_LOGI(CONTROLLER_TAG, "Animation mode received: mode_id=%d, frequency=%d",
             mode.mode_id, mode.frequency);

    esp_err_t ret = animation_mode_save(&mode);
    if (ret != ESP_OK) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to save animation mode to NVS: %d", ret);
    } else {
        ESP_LOGI(CONTROLLER_TAG, "Animation mode saved successfully");
        controller_update_animation_mode_characteristic();
    }
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
        // Update the BLE characteristic with the new lighting data
        controller_update_lighting_characteristic();
    }
}

/**
 * @brief Process servo calibration command and save calibration data
 *
 * @param packet Pointer to the unpacked data packet containing calibration data
 */
static void process_servo_calibration_command(const data_packet_t *packet)
{
    if (packet == NULL) {
        ESP_LOGE(CONTROLLER_TAG, "Invalid packet pointer for servo calibration");
        return;
    }

    servo_calibration_t calibration;
    if (!data_packet_get_servo_calibration(packet, &calibration)) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to deserialize servo calibration data");
        return;
    }

    ESP_LOGI(CONTROLLER_TAG, "Servo calibration received: left_azi=%d, left_lat=%d, right_azi=%d, right_lat=%d",
             calibration.left_azi, calibration.left_lat, calibration.right_azi, calibration.right_lat);

    // Save calibration to NVS
    esp_err_t ret = servo_save_calibration(&calibration);
    if (ret != ESP_OK) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to save servo calibration to NVS: %d", ret);
    } else {
        // update servo positions immediately based on new calibration
        reset_servos();
        ESP_LOGI(CONTROLLER_TAG, "Servo calibration saved successfully");
        // Update the BLE characteristic to reflect the new calibration
        controller_update_servo_calibration_characteristic();
    }
}

void controller_handle_read(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(CONTROLLER_TAG, "READ EVENT: handle=%d, conn_id=%d, trans_id=%lu, offset=%d, is_long=%d, need_rsp=%d",
             param->read.handle, param->read.conn_id, param->read.trans_id, 
             param->read.offset, param->read.is_long, param->read.need_rsp);
    
    esp_gatt_rsp_t rsp;
    memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
    rsp.attr_value.handle = param->read.handle;
    
    // Check if this is the ABF2 (data notify) characteristic
    if (param->read.handle == ble_get_data_notify_handle())
    {
        uint8_t* notify_buffer = ble_get_data_notify_buffer();
        
        uint16_t length = 0;
        const uint8_t *attr_value = NULL;
        esp_ble_gatts_get_attr_value(param->read.handle, &length, &attr_value);
        
        rsp.attr_value.len = length;
        if (length > 0 && length <= ESP_GATT_MAX_ATTR_LEN) {
            memcpy(rsp.attr_value.value, notify_buffer, length);
        }
        
        ESP_LOGI(CONTROLLER_TAG, "Sending response: length=%d", length);
        
        // Send response IMMEDIATELY - this is the critical operation
        esp_err_t ret = esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, 
                                    length > 0 ? ESP_GATT_OK : ESP_GATT_ERROR, &rsp);
        
        // Log result immediately
        ESP_LOGI(CONTROLLER_TAG, "esp_ble_gatts_send_response returned: %d (0x%x) %s", 
                 ret, ret, ret == ESP_OK ? "SUCCESS" : "FAILED");
    }
    else
    {
        // Other characteristics - send empty response
        rsp.attr_value.len = 0;
        esp_err_t ret = esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, 
                                    ESP_GATT_OK, &rsp);
        ESP_LOGI(CONTROLLER_TAG, "Read from other handle: %d, send_response returned: %d", param->read.handle, ret);
    }
}

void controller_handle_write(esp_ble_gatts_cb_param_t *param)
{
    // Use static storage to avoid stack overflow in BLE callback context (BTC_TASK has limited stack)
    static data_packet_t packet;
    static controller_command_t cmd;
    
    ESP_LOGI(CONTROLLER_TAG, "Characteristic write, conn_id %d, handle %d",
             param->write.conn_id, param->write.handle);
    ESP_LOGI(CONTROLLER_TAG, "Raw packet data: %.*s", param->write.len, param->write.value);

    // Check if this is the data characteristic
    if (param->write.handle == DATA_HANDLE)
    {
        // Unpack the data packet
        if (data_packet_unpack(param->write.value, param->write.len, &packet))
        {
            // Create command for queue
            memcpy(&cmd.packet, &packet, sizeof(data_packet_t));
            
            if (packet.type == DATA_TYPE_ANIMATION)
            {
                cmd.type = CMD_TYPE_ANIMATION;
            }
            else if (packet.type == DATA_TYPE_LIGHTING)
            {
                cmd.type = CMD_TYPE_LIGHTING;
            }
            else if (packet.type == DATA_TYPE_SERVO_CALIBRATION)
            {
                cmd.type = CMD_TYPE_SERVO_CALIBRATION;
            }
            else if (packet.type == DATA_TYPE_ANIMATION_MODE)
            {
                cmd.type = CMD_TYPE_ANIMATION_MODE;
            }
            else
            {
                ESP_LOGI(CONTROLLER_TAG, "Unhandled data type: %d", packet.type);
                return;
            }
            
            // Queue the command for processing by controller task
            // Use a short timeout to avoid blocking the BLE callback
            if (xQueueSend(command_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(CONTROLLER_TAG, "Command queue full, dropping command");
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

/**
 * @brief Controller task that processes commands from the queue
 * 
 * This task runs independently and processes all heavy operations
 * (NVS writes, task creation, extensive logging) outside of the
 * BLE callback context to prevent stack overflow.
 */
static void controller_task(void *arg)
{
    controller_command_t cmd;
    
    ESP_LOGI(CONTROLLER_TAG, "Controller task started");
    
    while (1) {
        // Wait for commands from the queue
        if (xQueueReceive(command_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case CMD_TYPE_ANIMATION:
                    process_animation_command(cmd.packet.data, cmd.packet.data_len);
                    break;
                    
                case CMD_TYPE_LIGHTING:
                    process_lighting_command(&cmd.packet);
                    break;
                
                case CMD_TYPE_SERVO_CALIBRATION:
                    process_servo_calibration_command(&cmd.packet);
                    break;

                case CMD_TYPE_ANIMATION_MODE:
                    process_animation_mode_command(&cmd.packet);
                    break;
                    
                default:
                    ESP_LOGW(CONTROLLER_TAG, "Unknown command type: %d", cmd.type);
                    break;
            }
        }
    }
}

esp_err_t controller_init(void)
{
    // Create command queue
    command_queue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(controller_command_t));
    if (command_queue == NULL) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to create command queue");
        return ESP_FAIL;
    }
    
    // Create controller task
    BaseType_t ret = xTaskCreate(
        controller_task,
        "controller",
        CONTROLLER_TASK_STACK_SIZE,
        NULL,
        CONTROLLER_TASK_PRIORITY,
        &controller_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(CONTROLLER_TAG, "Failed to create controller task");
        vQueueDelete(command_queue);
        command_queue = NULL;
        return ESP_FAIL;
    }
    
    ESP_LOGI(CONTROLLER_TAG, "Controller initialized");
    return ESP_OK;
}
