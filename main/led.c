/*
 * Description LED controller for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LED_TAG "LED"
#define NVS_NAMESPACE "lighting"
#define NVS_KEY "config"

// Global LED strip variables
static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static TaskHandle_t led_task_handle = NULL;
static volatile bool led_task_should_stop = false;
static SemaphoreHandle_t led_task_mutex = NULL;

// Helper function to send LED data
static esp_err_t led_send_data(const uint8_t *led_strip_pixels, size_t length)
{
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    
    esp_err_t ret = rmt_transmit(led_chan, led_encoder, led_strip_pixels, length, &tx_config);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return rmt_tx_wait_all_done(led_chan, portMAX_DELAY);
}

void led_stop_current_task(void)
{
    TaskHandle_t task_to_stop = NULL;
    
    // Get the task handle under mutex protection
    if (xSemaphoreTake(led_task_mutex, portMAX_DELAY) == pdTRUE) {
        task_to_stop = led_task_handle;
        xSemaphoreGive(led_task_mutex);
    }
    
    if (task_to_stop != NULL) {
        ESP_LOGI(LED_TAG, "Signaling LED task to stop");
        
        // Signal the task to stop
        led_task_should_stop = true;
        
        // Wait for the task to actually exit (max 1 second)
        // Check if task still exists using FreeRTOS API
        for (int i = 0; i < 100; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
            
            // Check if task still exists
            if (eTaskGetState(task_to_stop) == eDeleted) {
                break;
            }
        }
        
        // If task still exists, force delete it as a last resort
        if (eTaskGetState(task_to_stop) != eDeleted) {
            ESP_LOGW(LED_TAG, "Task didn't exit gracefully, forcing deletion");
            vTaskDelete(task_to_stop);
        }
        
        // Clear the handle under mutex protection
        if (xSemaphoreTake(led_task_mutex, portMAX_DELAY) == pdTRUE) {
            led_task_handle = NULL;
            xSemaphoreGive(led_task_mutex);
        }
        
        ESP_LOGI(LED_TAG, "LED task stopped");
    }
}

/**
 * @brief Save lighting configuration to NVS
 * 
 * @param lighting Pointer to lighting data to save
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t led_save_config(const lighting_data_t *lighting)
{
    if (lighting == NULL) {
        ESP_LOGE(LED_TAG, "Cannot save NULL lighting config");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(LED_TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Write the lighting data structure
    err = nvs_set_blob(nvs_handle, NVS_KEY, lighting, sizeof(lighting_data_t));
    if (err != ESP_OK) {
        ESP_LOGE(LED_TAG, "Error writing lighting config to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit the changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(LED_TAG, "Error committing NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(LED_TAG, "Lighting config saved to NVS: mode=%d, speed=%d, colors=%d",
                 lighting->mode, lighting->speed, lighting->color_count);
    }

    nvs_close(nvs_handle);
    return err;
}

/**
 * @brief Load lighting configuration from NVS
 * 
 * @param lighting Pointer to lighting data structure to populate
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no config saved, error code otherwise
 */
esp_err_t led_load_config(lighting_data_t *lighting)
{
    if (lighting == NULL) {
        ESP_LOGE(LED_TAG, "Cannot load into NULL lighting config");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(LED_TAG, "No saved lighting config found in NVS");
        } else {
            ESP_LOGE(LED_TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        }
        return err;
    }

    // Read the lighting data structure
    size_t required_size = sizeof(lighting_data_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY, lighting, &required_size);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(LED_TAG, "No saved lighting config found");
        } else {
            ESP_LOGE(LED_TAG, "Error reading lighting config from NVS: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
        return err;
    }

    ESP_LOGI(LED_TAG, "Lighting config loaded from NVS: mode=%d, speed=%d, colors=%d",
             lighting->mode, lighting->speed, lighting->color_count);

    nvs_close(nvs_handle);
    return ESP_OK;
}

/**
 * @brief Start a lighting animation task
 * 
 * @param task_func Task function pointer
 * @param task_name Name of the task
 * @param params Parameters to pass to the task (will be copied)
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t led_start_task(lighting_task_func_t task_func, const char *task_name, const lighting_data_t *lighting)
{
    // Stop any existing task first
    led_stop_current_task();
    
    // Reset the stop flag for the new task
    led_task_should_stop = false;
    
    // Save the lighting configuration to NVS for persistence
    esp_err_t save_err = led_save_config(lighting);
    if (save_err != ESP_OK) {
        ESP_LOGW(LED_TAG, "Failed to save lighting config to NVS: %s", esp_err_to_name(save_err));
        // Continue anyway - saving is not critical
    }
    
    // Allocate memory for lighting data to pass to the task
    lighting_data_t *lighting_param = (lighting_data_t *)malloc(sizeof(lighting_data_t));
    if (lighting_param == NULL) {
        ESP_LOGE(LED_TAG, "Failed to allocate memory for lighting task parameter");
        return ESP_FAIL;
    }
    
    // Copy lighting data
    memcpy(lighting_param, lighting, sizeof(lighting_data_t));
    
    TaskHandle_t new_task_handle = NULL;
    
    // Create the task
    BaseType_t ret = xTaskCreate(
        task_func,
        task_name,
        4096,  // Stack size
        lighting_param,
        5,     // Priority
        &new_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(LED_TAG, "Failed to create LED task: %s", task_name);
        free(lighting_param);
        return ESP_FAIL;
    }
    
    // Set the task handle under mutex protection
    if (xSemaphoreTake(led_task_mutex, portMAX_DELAY) == pdTRUE) {
        led_task_handle = new_task_handle;
        xSemaphoreGive(led_task_mutex);
    }
    
    ESP_LOGI(LED_TAG, "Started LED task: %s", task_name);
    return ESP_OK;
}

// LED solid color mode - displays static colors
void led_solid_task(void *params)
{
    lighting_data_t *lighting = (lighting_data_t *)params;
    uint8_t led_strip_pixels[LED_STRIP_LED_COUNT * 3]; // GRB format
    
    ESP_LOGI(LED_TAG, "LED solid color task started with %d colors", lighting->color_count);
    
    if (lighting->color_count == 0) {
        // Default to off if no colors specified
        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
        led_send_data(led_strip_pixels, sizeof(led_strip_pixels));
        free(lighting);
        vTaskDelete(NULL);
        return;
    }
    
    // Set all LEDs to the first color (or cycle through colors evenly)
    for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
        int color_idx = (i * lighting->color_count) / LED_STRIP_LED_COUNT;
        rgb_color_t color = lighting->colors[color_idx];
        
        // WS2812 uses GRB format
        led_strip_pixels[i * 3 + 0] = color.g;
        led_strip_pixels[i * 3 + 1] = color.r;
        led_strip_pixels[i * 3 + 2] = color.b;
    }
    
    led_send_data(led_strip_pixels, sizeof(led_strip_pixels));
    
    ESP_LOGI(LED_TAG, "Solid color applied, task exiting");
    free(lighting);
    vTaskDelete(NULL);
}

// LED breathing mode - pulsing effect
void led_breathing_task(void *params)
{
    lighting_data_t *lighting = (lighting_data_t *)params;
    uint8_t led_strip_pixels[LED_STRIP_LED_COUNT * 3]; // GRB format
    uint32_t brightness = 0;
    bool increasing = true;
    
    ESP_LOGI(LED_TAG, "LED breathing task started, speed=%d, colors=%d", 
             lighting->speed, lighting->color_count);
    
    if (lighting->color_count == 0) {
        free(lighting);
        vTaskDelete(NULL);
        return;
    }
    
    // Calculate delay based on speed (1-100, higher is faster)
    // Minimum 15ms to ensure WS2812 LEDs can handle the update rate
    // Range: 15ms (speed=100) to 510ms (speed=1)
    uint32_t delay_ms = 15 + ((100 - lighting->speed) * 5);
    
    // Adjust brightness step based on speed to maintain smooth animation
    uint32_t brightness_step = 1 + (lighting->speed / 50);  // 1-3 based on speed
    
    while (!led_task_should_stop) {
        // Update brightness
        if (increasing) {
            brightness += brightness_step;
            if (brightness >= 255) {
                brightness = 255;
                increasing = false;
            }
        } else {
            if (brightness <= brightness_step) {
                brightness = 0;
                increasing = true;
            } else {
                brightness -= brightness_step;
            }
        }
        
        // Apply brightness to all LEDs
        for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
            int color_idx = (i * lighting->color_count) / LED_STRIP_LED_COUNT;
            rgb_color_t color = lighting->colors[color_idx];
            
            uint8_t r = (color.r * brightness) / 255;
            uint8_t g = (color.g * brightness) / 255;
            uint8_t b = (color.b * brightness) / 255;
            
            // WS2812 uses GRB format
            led_strip_pixels[i * 3 + 0] = g;
            led_strip_pixels[i * 3 + 1] = r;
            led_strip_pixels[i * 3 + 2] = b;
        }
        
        led_send_data(led_strip_pixels, sizeof(led_strip_pixels));
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    ESP_LOGI(LED_TAG, "Breathing task exiting gracefully");
    free(lighting);
    vTaskDelete(NULL);
}

// LED marquee mode - scrolling colors
void led_marquee_task(void *params)
{
    lighting_data_t *lighting = (lighting_data_t *)params;
    uint8_t led_strip_pixels[LED_STRIP_LED_COUNT * 3]; // GRB format
    uint32_t offset = 0;
    
    ESP_LOGI(LED_TAG, "LED marquee task started, speed=%d, colors=%d", 
             lighting->speed, lighting->color_count);
    
    if (lighting->color_count == 0) {
        free(lighting);
        vTaskDelete(NULL);
        return;
    }
    
    // Calculate delay and step based on speed (1-100, higher is faster)
    // Quadratic curve: faster at low speeds, more gradual at high speeds
    // Range: 5ms (speed=100) to ~25ms (speed=1)
    uint32_t speed_diff = 100 - lighting->speed;
    uint32_t delay_ms = 5 + (speed_diff / 3);
    uint32_t step = 5;  // Constant step for consistent motion
    
    const int total_range = lighting->color_count * 1000;
    
    while (!led_task_should_stop) {
        // Generate color pattern
        for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
            uint32_t pos = (offset + (i * total_range / LED_STRIP_LED_COUNT)) % total_range;
            
            int color_idx = pos / 1000;
            int next_idx = (color_idx + 1) % lighting->color_count;
            uint32_t section_pos = pos % 1000;
            
            rgb_color_t color1 = lighting->colors[color_idx];
            rgb_color_t color2 = lighting->colors[next_idx];
            
            uint8_t r, g, b;
            
            // Blend between colors
            if (section_pos < 850) {
                r = color1.r;
                g = color1.g;
                b = color1.b;
            } else {
                uint32_t blend = ((section_pos - 850) * 255) / 150;
                r = (color1.r * (255 - blend) + color2.r * blend) / 255;
                g = (color1.g * (255 - blend) + color2.g * blend) / 255;
                b = (color1.b * (255 - blend) + color2.b * blend) / 255;
            }
            
            // WS2812 uses GRB format
            led_strip_pixels[i * 3 + 0] = g;
            led_strip_pixels[i * 3 + 1] = r;
            led_strip_pixels[i * 3 + 2] = b;
        }
        
        led_send_data(led_strip_pixels, sizeof(led_strip_pixels));
        
        offset = (offset + step) % total_range;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    ESP_LOGI(LED_TAG, "Marquee task exiting gracefully");
    free(lighting);
    vTaskDelete(NULL);
}

// LED chasing mode - running lights effect
void led_chasing_task(void *params)
{
    lighting_data_t *lighting = (lighting_data_t *)params;
    uint8_t led_strip_pixels[LED_STRIP_LED_COUNT * 3]; // GRB format
    uint32_t position = 0;
    
    ESP_LOGI(LED_TAG, "LED chasing task started, speed=%d, colors=%d", 
             lighting->speed, lighting->color_count);
    
    if (lighting->color_count == 0) {
        free(lighting);
        vTaskDelete(NULL);
        return;
    }
    
    // Calculate delay based on speed (1-100, higher is faster)
    // Faster overall: 10ms (speed=100) to 210ms (speed=1)
    uint32_t delay_ms = 10 + ((100 - lighting->speed) * 2);
    const int chase_length = 5; // Number of LEDs in the chase pattern
    
    while (!led_task_should_stop) {
        // Clear all LEDs
        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
        
        // Draw chase pattern
        for (int i = 0; i < chase_length; i++) {
            int led_idx = (position + i) % LED_STRIP_LED_COUNT;
            int color_idx = (position / LED_STRIP_LED_COUNT) % lighting->color_count;
            rgb_color_t color = lighting->colors[color_idx];
            
            // Fade effect: brightest at the head
            uint8_t brightness = 255 - (i * 255 / chase_length);
            uint8_t r = (color.r * brightness) / 255;
            uint8_t g = (color.g * brightness) / 255;
            uint8_t b = (color.b * brightness) / 255;
            
            // WS2812 uses GRB format
            led_strip_pixels[led_idx * 3 + 0] = g;
            led_strip_pixels[led_idx * 3 + 1] = r;
            led_strip_pixels[led_idx * 3 + 2] = b;
        }
        
        led_send_data(led_strip_pixels, sizeof(led_strip_pixels));
        
        position = (position + 1) % (LED_STRIP_LED_COUNT * lighting->color_count);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    ESP_LOGI(LED_TAG, "Chasing task exiting gracefully");
    free(lighting);
    vTaskDelete(NULL);
}

// LED rain mode - falling drops effect
void led_rain_task(void *params)
{
    lighting_data_t *lighting = (lighting_data_t *)params;
    uint8_t led_strip_pixels[LED_STRIP_LED_COUNT * 3]; // GRB format
    
    ESP_LOGI(LED_TAG, "LED rain task started, speed=%d, colors=%d", 
             lighting->speed, lighting->color_count);
    
    if (lighting->color_count == 0) {
        free(lighting);
        vTaskDelete(NULL);
        return;
    }
    
    // Calculate delay based on speed (1-100, higher is faster)
    // Faster overall: 15ms (speed=100) to 315ms (speed=1)
    uint32_t delay_ms = 15 + ((100 - lighting->speed) * 3);
    
    // Random drop positions and colors
    typedef struct {
        int position;
        int color_idx;
        bool active;
    } raindrop_t;
    
    raindrop_t drops[10];
    memset(drops, 0, sizeof(drops));
    
    uint32_t frame = 0;
    
    while (!led_task_should_stop) {
        // Clear all LEDs
        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
        
        // Spawn new drops randomly
        if (frame % 3 == 0) {
            for (int i = 0; i < 10; i++) {
                if (!drops[i].active && (rand() % 10) < 3) {
                    drops[i].position = 0;
                    drops[i].color_idx = rand() % lighting->color_count;
                    drops[i].active = true;
                    break;
                }
            }
        }
        
        // Update and draw drops
        for (int i = 0; i < 10; i++) {
            if (drops[i].active) {
                if (drops[i].position < LED_STRIP_LED_COUNT) {
                    rgb_color_t color = lighting->colors[drops[i].color_idx];
                    int led_idx = drops[i].position;
                    
                    // WS2812 uses GRB format
                    led_strip_pixels[led_idx * 3 + 0] = color.g;
                    led_strip_pixels[led_idx * 3 + 1] = color.r;
                    led_strip_pixels[led_idx * 3 + 2] = color.b;
                    
                    drops[i].position++;
                } else {
                    drops[i].active = false;
                }
            }
        }
        
        led_send_data(led_strip_pixels, sizeof(led_strip_pixels));
        
        frame++;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    ESP_LOGI(LED_TAG, "Rain task exiting gracefully");
    free(lighting);
    vTaskDelete(NULL);
}

// Convert HSV to RGB
static void hsv_to_rgb(uint32_t h, uint32_t s, uint32_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    h %= 360;
    uint32_t rgb_max = v * 255 / 100;
    uint32_t rgb_min = rgb_max * (100 - s) / 100;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}

esp_err_t init_leds(void) 
{
    ESP_LOGI(LED_TAG, "Initializing LED strip on GPIO %d with %d LEDs", LED_GPIO_0, LED_STRIP_LED_COUNT);
    
    // Create mutex for task handle synchronization
    if (led_task_mutex == NULL) {
        led_task_mutex = xSemaphoreCreateMutex();
        if (led_task_mutex == NULL) {
            ESP_LOGE(LED_TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Configure RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO_0,
        .mem_block_symbols = 64,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &led_chan);
    if (err) {
        ESP_LOGE(LED_TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(err));
        return err;
    }
    
    // Configure bytes encoder for WS2812
    // WS2812 timing: T0H=350ns, T1H=900ns, T0L=900ns, T1L=350ns (total 1.25us per bit)
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 0.35 * LED_STRIP_RMT_RES_HZ / 1000000, // 350ns
            .level1 = 0,
            .duration1 = 0.9 * LED_STRIP_RMT_RES_HZ / 1000000, // 900ns
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 0.9 * LED_STRIP_RMT_RES_HZ / 1000000, // 900ns
            .level1 = 0,
            .duration1 = 0.35 * LED_STRIP_RMT_RES_HZ / 1000000, // 350ns
        },
        .flags.msb_first = 1,
    };
    err = rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder);
    if (err) {
        ESP_LOGE(LED_TAG, "Failed to create RMT bytes encoder: %s", esp_err_to_name(err));
        return err;
    }
    
    // Enable RMT channel
    err = rmt_enable(led_chan);
    if (err) {
        ESP_LOGE(LED_TAG, "Failed to enable RMT channel: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(LED_TAG, "LED strip hardware initialized");
    
    // Try to load saved lighting configuration and apply it
    lighting_data_t saved_lighting;
    err = led_load_config(&saved_lighting);
    if (err == ESP_OK) {
        // Validate the loaded configuration
        if (saved_lighting.mode < 5 && saved_lighting.speed > 0 && saved_lighting.speed <= 100) {
            ESP_LOGI(LED_TAG, "Restoring saved lighting configuration");
            
            // Reset stop flag for the new task
            led_task_should_stop = false;
            
            // Task names for each lighting mode
            const char *task_names[] = {
                "led_solid", "led_breathing", "led_marquee", "led_chasing", "led_rain"
            };
            
            // Task functions array
            const lighting_task_func_t task_funcs[] = {
                led_solid_task, led_breathing_task, led_marquee_task, 
                led_chasing_task, led_rain_task
            };
            
            // Allocate and copy lighting data for the task
            lighting_data_t *lighting_param = (lighting_data_t *)malloc(sizeof(lighting_data_t));
            if (lighting_param != NULL) {
                memcpy(lighting_param, &saved_lighting, sizeof(lighting_data_t));
                
                BaseType_t ret = xTaskCreate(
                    task_funcs[saved_lighting.mode],
                    task_names[saved_lighting.mode],
                    4096,
                    lighting_param,
                    5,
                    &led_task_handle
                );
                
                if (ret != pdPASS) {
                    ESP_LOGE(LED_TAG, "Failed to restore saved lighting task");
                    free(lighting_param);
                } else {
                    ESP_LOGI(LED_TAG, "Restored lighting: %s", task_names[saved_lighting.mode]);
                }
            }
        } else {
            ESP_LOGW(LED_TAG, "Saved lighting config is invalid, skipping restore");
        }
    } else {
        ESP_LOGI(LED_TAG, "No saved lighting config, LED strip ready for commands");
    }
    
    return ESP_OK;
}
