/*
 * Description LED controller for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Company: Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0
 */

#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include <stdint.h>

#define LED_TAG "LED"

// Global LED strip variables
static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static TaskHandle_t led_task_handle = NULL;

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

// LED rainbow task
static void led_rainbow_task(void *pvParameters)
{
    uint8_t led_strip_pixels[LED_STRIP_LED_COUNT * 3]; // GRB format
    uint32_t offset = 0;
    
    // Highly saturated color palette: blue, hot pink, purple
    typedef struct {
        uint8_t r, g, b;
    } rgb_color_t;
    
    rgb_color_t palette[] = {
        {0, 150, 255},    // Pure saturated blue
        {255, 0, 180},    // Highly saturated hot pink
        {180, 0, 255}     // Highly saturated purple
    };
    const int palette_size = 3;
    
    ESP_LOGI(LED_TAG, "LED color scroll effect task started");
    
    while (1) {
        // Generate color pattern
        for (int i = 0; i < LED_STRIP_LED_COUNT; i++) {
            // Calculate position in the color cycle (0-2999 range)
            uint32_t pos = (offset + (i * 3000 / LED_STRIP_LED_COUNT)) % 3000;
            
            // Determine which color we're in
            int color_idx = pos / 1000;  // 0, 1, or 2
            int next_idx = (color_idx + 1) % palette_size;
            
            // Calculate position within this color section (0-999)
            uint32_t section_pos = pos % 1000;
            
            uint8_t r, g, b;
            
            // Only blend in the last 15% of each color section for sharper transitions
            if (section_pos < 850) {
                // Solid color - no blending
                r = palette[color_idx].r;
                g = palette[color_idx].g;
                b = palette[color_idx].b;
            } else {
                // Blend zone - quick transition to next color
                uint32_t blend = ((section_pos - 850) * 255) / 150;
                r = (palette[color_idx].r * (255 - blend) + palette[next_idx].r * blend) / 255;
                g = (palette[color_idx].g * (255 - blend) + palette[next_idx].g * blend) / 255;
                b = (palette[color_idx].b * (255 - blend) + palette[next_idx].b * blend) / 255;
            }
            
            // Dim to 20% brightness
            r = r / 5;
            g = g / 5;
            b = b / 5;
            
            // WS2812 uses GRB format
            led_strip_pixels[i * 3 + 0] = g;
            led_strip_pixels[i * 3 + 1] = r;
            led_strip_pixels[i * 3 + 2] = b;
        }
        
        // Send data to LED strip
        rmt_transmit_config_t tx_config = {
            .loop_count = 0,
        };
        
        ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        
        // Increment offset for scrolling effect
        offset = (offset + 50) % 3000;
        
        vTaskDelay(pdMS_TO_TICKS(50)); // Update every 50ms
    }
}

void init_leds(void) 
{
    ESP_LOGI(LED_TAG, "Initializing LED strip on GPIO %d with %d LEDs", LED_GPIO_0, LED_STRIP_LED_COUNT);
    
    // Configure RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO_0,
        .mem_block_symbols = 64,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));
    
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
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder));
    
    // Enable RMT channel
    ESP_ERROR_CHECK(rmt_enable(led_chan));
    
    // Create rainbow task
    xTaskCreate(led_rainbow_task, "led_rainbow_task", 2048, NULL, 5, &led_task_handle);
    
    ESP_LOGI(LED_TAG, "LED strip initialized with scrolling pastel effect");
}
