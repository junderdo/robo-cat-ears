/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"
#include "iot_servo.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "string.h"
#include "math.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "boot.h"
#include "esp_gatt_common_api.h"
#include "esp_timer.h"
#if (CONFIG_EXAMPLE_ENABLE_RF_TESTING_CONFIGURATION_COMMAND)
#include "rf_testing_configuration_cmd.h"
#endif // CONFIG_EXAMPLE_ENABLE_RF_TESTING_CONFIGURATION_COMMAND
#define ROBO_CAT_EARS_TAG "ROBO_CAT_EARS_APP"
#define GATTS_TABLE_TAG "GATTS_SPP_SERVICE"

// Servo configuration
#define SERVO_MIN_PULSEWIDTH_US 500  // Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH_US 2500  // Maximum pulse width in microsecond
#define SERVO_MAX_ANGLE         180    // Maximum angle (0-180 degrees)
#define SERVO_FREQ              50     // 50Hz for standard servos
#define SERVO_PULSE_GPIO_0        2      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_1        3      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_2        4      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_3        5      // GPIO connects to the PWM signal line

// LED configuration
#define LED_GPIO_0 6 // GPIO connects to the LED strip
#define LED_STRIP_LED_COUNT 31 // Number of LEDs in the strip
#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000) // 10MHz resolution

// Global LED strip variables
static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static TaskHandle_t led_task_handle = NULL;

// SPP configuration
#define SPP_PROFILE_NUM 1
#define SPP_PROFILE_APP_IDX 0
#define ESP_SPP_APP_ID 0x56
#define SAMPLE_DEVICE_NAME "ROBO_CAT_EARS" // The Device Name Characteristics in GAP
#define SPP_SVC_INST_ID 0

/// SPP Service
static const uint16_t spp_service_uuid = 0xABF0;
/// Characteristic UUID
#define ESP_GATT_UUID_SPP_DATA_RECEIVE 0xABF1
#define ESP_GATT_UUID_SPP_DATA_NOTIFY 0xABF2
#define ESP_GATT_UUID_SPP_COMMAND_RECEIVE 0xABF3
#define ESP_GATT_UUID_SPP_COMMAND_NOTIFY 0xABF4

#define SPP_GATT_MTU_SIZE (512)

#ifdef SUPPORT_HEARTBEAT
#define ESP_GATT_UUID_SPP_HEARTBEAT 0xABF5
#endif

#define BLUETOOTH_TASK_PINNED_TO_CORE (0)

static const uint8_t spp_adv_data[23] = {
    /* Flags */
    0x02, 0x01, 0x06,
    /* Complete List of 16-bit Service Class UUIDs */
    0x03, 0x03, 0xF0, 0xAB,
    /* Complete Local Name in advertising */
    0x0F, 0x09, 'R', 'O', 'B', 'O', '_', 'C', 'A', 'T', '_', 'E', 'A', 'R', 'S'};

static uint16_t spp_mtu_size = SPP_GATT_MTU_SIZE;
static uint16_t spp_conn_id = 0xffff;
static esp_gatt_if_t spp_gatts_if = 0xff;
QueueHandle_t spp_uart_queue = NULL;
static QueueHandle_t cmd_cmd_queue = NULL;

#ifdef SUPPORT_HEARTBEAT
static QueueHandle_t cmd_heartbeat_queue = NULL;
static uint8_t heartbeat_s[9] = {'E', 's', 'p', 'r', 'e', 's', 's', 'i', 'f'};
static bool enable_heart_ntf = false;
static uint8_t heartbeat_count_num = 0;
#endif

static bool enable_data_ntf = false;
static bool is_connected = false;
static esp_bd_addr_t spp_remote_bda = {
    0x0,
};

static uint16_t spp_handle_table[SPP_IDX_NB];

static esp_ble_adv_params_t spp_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

typedef struct spp_receive_data_node
{
    int32_t len;
    uint8_t *node_buff;
    struct spp_receive_data_node *next_node;
} spp_receive_data_node_t;

static spp_receive_data_node_t *temp_spp_recv_data_node_p1 = NULL;
static spp_receive_data_node_t *temp_spp_recv_data_node_p2 = NULL;

typedef struct spp_receive_data_buff
{
    int32_t node_num;
    int32_t buff_size;
    spp_receive_data_node_t *first_node;
} spp_receive_data_buff_t;

static spp_receive_data_buff_t SppRecvDataBuff = {
    .node_num = 0,
    .buff_size = 0,
    .first_node = NULL};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst spp_profile_tab[SPP_PROFILE_NUM] = {
    [SPP_PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE, /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

/*
 *  SPP PROFILE ATTRIBUTES
 ****************************************************************************************
 */

#define CHAR_DECLARATION_SIZE (sizeof(uint8_t))
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_READ;
#ifdef CONFIG_EXAMPLE_SPP_THROUGHPUT
static const uint8_t spp_data_notity_char_prop = char_prop_read_notify;
#else
static const uint8_t spp_data_notity_char_prop = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_INDICATE;
#endif

#ifdef SUPPORT_HEARTBEAT
static const uint8_t char_prop_read_write_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
#endif

/// SPP Service - data receive characteristic, read&write without response
static const uint16_t spp_data_receive_uuid = ESP_GATT_UUID_SPP_DATA_RECEIVE;
static const uint8_t spp_data_receive_val[20] = {0x00};

/// SPP Service - data notify characteristic, notify&read
static const uint16_t spp_data_notify_uuid = ESP_GATT_UUID_SPP_DATA_NOTIFY;
static const uint8_t spp_data_notify_val[20] = {0x00};
static const uint8_t spp_data_notify_ccc[2] = {0x00, 0x00};

/// SPP Service - command characteristic, read&write without response
static const uint16_t spp_command_uuid = ESP_GATT_UUID_SPP_COMMAND_RECEIVE;
static const uint8_t spp_command_val[10] = {0x00};

/// SPP Service - status characteristic, notify&read
static const uint16_t spp_status_uuid = ESP_GATT_UUID_SPP_COMMAND_NOTIFY;
static const uint8_t spp_status_val[10] = {0x00};
static const uint8_t spp_status_ccc[2] = {0x00, 0x00};

#ifdef SUPPORT_HEARTBEAT
/// SPP Server - Heart beat characteristic, notify&write&read
static const uint16_t spp_heart_beat_uuid = ESP_GATT_UUID_SPP_HEARTBEAT;
static const uint8_t spp_heart_beat_val[2] = {0x00, 0x00};
static const uint8_t spp_heart_beat_ccc[2] = {0x00, 0x00};
#endif

/// Full HRS Database Description - Used to add attributes into the database
static const esp_gatts_attr_db_t spp_gatt_db[SPP_IDX_NB] =
    {
        // SPP -  Service Declaration
        [SPP_IDX_SVC] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, sizeof(spp_service_uuid), sizeof(spp_service_uuid), (uint8_t *)&spp_service_uuid}},

        // SPP -  data receive characteristic Declaration
        [SPP_IDX_SPP_DATA_RECV_CHAR] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},

        // SPP -  data receive characteristic Value
        [SPP_IDX_SPP_DATA_RECV_VAL] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_data_receive_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, SPP_DATA_MAX_LEN, sizeof(spp_data_receive_val), (uint8_t *)spp_data_receive_val}},

        // SPP -  data notify characteristic Declaration
        [SPP_IDX_SPP_DATA_NOTIFY_CHAR] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&spp_data_notity_char_prop}},

        // SPP -  data notify characteristic Value
        [SPP_IDX_SPP_DATA_NTY_VAL] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_data_notify_uuid, ESP_GATT_PERM_READ, SPP_DATA_MAX_LEN, sizeof(spp_data_notify_val), (uint8_t *)spp_data_notify_val}},

        // SPP -  data notify characteristic - Client Characteristic Configuration Descriptor
        [SPP_IDX_SPP_DATA_NTF_CFG] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(spp_data_notify_ccc), (uint8_t *)spp_data_notify_ccc}},

        // SPP -  command characteristic Declaration
        [SPP_IDX_SPP_COMMAND_CHAR] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}},

        // SPP -  command characteristic Value
        [SPP_IDX_SPP_COMMAND_VAL] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_command_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, SPP_CMD_MAX_LEN, sizeof(spp_command_val), (uint8_t *)spp_command_val}},

        // SPP -  status characteristic Declaration
        [SPP_IDX_SPP_STATUS_CHAR] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

        // SPP -  status characteristic Value
        [SPP_IDX_SPP_STATUS_VAL] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_status_uuid, ESP_GATT_PERM_READ, SPP_STATUS_MAX_LEN, sizeof(spp_status_val), (uint8_t *)spp_status_val}},

        // SPP -  status characteristic - Client Characteristic Configuration Descriptor
        [SPP_IDX_SPP_STATUS_CFG] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(spp_status_ccc), (uint8_t *)spp_status_ccc}},

#ifdef SUPPORT_HEARTBEAT
        // SPP -  Heart beat characteristic Declaration
        [SPP_IDX_SPP_HEARTBEAT_CHAR] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},

        // SPP -  Heart beat characteristic Value
        [SPP_IDX_SPP_HEARTBEAT_VAL] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_heart_beat_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(spp_heart_beat_val), sizeof(spp_heart_beat_val), (uint8_t *)spp_heart_beat_val}},

        // SPP -  Heart beat characteristic - Client Characteristic Configuration Descriptor
        [SPP_IDX_SPP_HEARTBEAT_CFG] =
            {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(spp_data_notify_ccc), (uint8_t *)spp_heart_beat_ccc}},
#endif
};

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
    
    ESP_LOGI(ROBO_CAT_EARS_TAG, "LED color scroll effect task started");
    
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
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Initializing LED strip on GPIO %d with %d LEDs", LED_GPIO_0, LED_STRIP_LED_COUNT);
    
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
    
    ESP_LOGI(ROBO_CAT_EARS_TAG, "LED strip initialized with scrolling pastel effect");
}

void reset_servos(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Resetting servos to center position (90 degrees)");
    for (int i = 0; i < 4; i++)
    {
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, i, 90.0));
    }
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Servos reset to center position");
}

void init_servos(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Initializing 4 servos on GPIO 2-5");
    
    // Configure 4 servos
    servo_config_t servo_cfg = {
        .max_angle = SERVO_MAX_ANGLE,
        .min_width_us = SERVO_MIN_PULSEWIDTH_US,
        .max_width_us = SERVO_MAX_PULSEWIDTH_US,
        .freq = SERVO_FREQ,
        .timer_number = LEDC_TIMER_0,
        .channels = {
            .servo_pin = {SERVO_PULSE_GPIO_0, SERVO_PULSE_GPIO_1, SERVO_PULSE_GPIO_2, SERVO_PULSE_GPIO_3},
            .ch = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3},
        },
        .channel_number = 4,
    };
    
    // Initialize servos
    ESP_ERROR_CHECK(iot_servo_init(LEDC_LOW_SPEED_MODE, &servo_cfg));
    ESP_LOGI(ROBO_CAT_EARS_TAG, "All 4 servos initialized at center position (90 degrees)");
    // Reset servos to center position
    reset_servos();
}

void do_animation_1(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Starting animation 1: Happy wiggle");
    
    // Start with ears up and perked
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 80));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 100));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90));
    vTaskDelay(200 / portTICK_PERIOD_MS);
    
    // Quick back and forth movements with rotation
    for (int i = 0; i < 2; i++) {
        // Move ears back with slight outward rotation
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 85));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 95));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 75));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 105));
        vTaskDelay(200 / portTICK_PERIOD_MS);
        
        // Move ears forward with inward rotation
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 135));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 45));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 105));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 75));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    // Big happy wiggle
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 145));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 35));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 110));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 70));
    vTaskDelay(300 / portTICK_PERIOD_MS);
    
    // Return to center with alternating movement
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 80));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90));
    vTaskDelay(150 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 100));
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90));
    vTaskDelay(150 / portTICK_PERIOD_MS);
    
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Animation 1 complete");
    reset_servos();
}

void do_animation_2(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Starting animation 2: Sad");
    for (int i = 0; i < 3; i++) {
        // move ears forward/down
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 145));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 35));
        // rotate outward
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 30));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 150));
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Animation 2 complete");
    reset_servos();
}

void do_animation_3(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Starting animation 3: Playful bounce");
    // Very rapid playful movements - extended version
    for (int i = 0; i < 8; i++) {
        // Quick bouncy positions
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 80 + (i % 4) * 12));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 100 - (i % 4) * 12));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90 + ((i % 2) * 25 - 12)));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90 + ((i % 2) * 25 - 12)));
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Medium bounces
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 95));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 85));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 105));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 75));
        vTaskDelay(180 / portTICK_PERIOD_MS);
        
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 125));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 55));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 75));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 105));
        vTaskDelay(180 / portTICK_PERIOD_MS);
    }
    
    // Final rapid flutter
    for (int i = 0; i < 4; i++) {
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 85 + (i % 2) * 20));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 95 - (i % 2) * 20));
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Animation 3 complete");
    reset_servos();
}

void do_animation_4(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Starting animation 4: Curious tilt");
    // Alternate ears - one up, one down
    for (int i = 0; i < 3; i++) {
        // Left ear down, right ear up
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 130)); // Left ear down
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 80)); // Right ear up
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 100)); // Slight tilt
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 80)); // Slight tilt
        vTaskDelay(600 / portTICK_PERIOD_MS);
        
        // Right ear down, left ear up
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 85)); // Left ear up
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 60)); // Right ear down
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 80)); // Slight tilt
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 100)); // Slight tilt
        vTaskDelay(600 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Animation 4 complete");
    reset_servos();
}

void do_animation_5(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Starting animation 5: Listening/Radar");
    // Ears rotate side to side like listening/scanning
    for (int i = 0; i < 2; i++) {
        // Rotate both ears outward
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 60)); // Right ear rotate out
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 120)); // Left ear rotate out
        vTaskDelay(400 / portTICK_PERIOD_MS);
        
        // Rotate both ears inward
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 120)); // Right ear rotate in
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 60)); // Left ear rotate in
        vTaskDelay(400 / portTICK_PERIOD_MS);
        
        // Back to center
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    // Independent rotation - one ear tracks left, other right
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 60)); // Right ear left
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 60)); // Left ear left
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 120)); // Right ear right
    ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 120)); // Left ear right
    vTaskDelay(500 / portTICK_PERIOD_MS);
    
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Animation 5 complete");
    reset_servos();
}

void do_animation_6(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Starting animation 6: Excited twitch");
    // Rapid excited movements
    for (int i = 0; i < 6; i++) {
        // Quick twitch positions
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 80 + (i % 3) * 15));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 100 - (i % 3) * 15));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 90 + ((i % 2) * 20 - 10)));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 90 + ((i % 2) * 20 - 10)));
        vTaskDelay(120 / portTICK_PERIOD_MS);
    }
    
    // Big excited wiggle
    for (int i = 0; i < 2; i++) {
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 100));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 80));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 110));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 70));
        vTaskDelay(200 / portTICK_PERIOD_MS);
        
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 1, 130));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 3, 50));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, 70));
        ESP_ERROR_CHECK(iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 2, 110));
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Animation 6 complete");
    reset_servos();
}

static uint8_t find_char_and_desr_index(uint16_t handle)
{
    uint8_t error = 0xff;

    for (int i = 0; i < SPP_IDX_NB; i++)
    {
        if (handle == spp_handle_table[i])
        {
            return i;
        }
    }

    return error;
}

static bool store_wr_buffer(esp_ble_gatts_cb_param_t *p_data)
{
    temp_spp_recv_data_node_p1 = (spp_receive_data_node_t *)malloc(sizeof(spp_receive_data_node_t));

    if (temp_spp_recv_data_node_p1 == NULL)
    {
        ESP_LOGI(GATTS_TABLE_TAG, "malloc error %s %d", __func__, __LINE__);
        return false;
    }
    if (temp_spp_recv_data_node_p2 != NULL)
    {
        temp_spp_recv_data_node_p2->next_node = temp_spp_recv_data_node_p1;
    }
    temp_spp_recv_data_node_p1->len = p_data->write.len;
    SppRecvDataBuff.buff_size += p_data->write.len;
    temp_spp_recv_data_node_p1->next_node = NULL;
    temp_spp_recv_data_node_p1->node_buff = (uint8_t *)malloc(p_data->write.len);
    temp_spp_recv_data_node_p2 = temp_spp_recv_data_node_p1;
    if (temp_spp_recv_data_node_p1->node_buff == NULL)
    {
        ESP_LOGI(GATTS_TABLE_TAG, "malloc error %s %d\n", __func__, __LINE__);
        temp_spp_recv_data_node_p1->len = 0;
    }
    else
    {
        memcpy(temp_spp_recv_data_node_p1->node_buff, p_data->write.value, p_data->write.len);
    }

    if (SppRecvDataBuff.node_num == 0)
    {
        SppRecvDataBuff.first_node = temp_spp_recv_data_node_p1;
        SppRecvDataBuff.node_num++;
    }
    else
    {
        SppRecvDataBuff.node_num++;
    }

    return true;
}

static void free_write_buffer(void)
{
    temp_spp_recv_data_node_p1 = SppRecvDataBuff.first_node;

    while (temp_spp_recv_data_node_p1 != NULL)
    {
        temp_spp_recv_data_node_p2 = temp_spp_recv_data_node_p1->next_node;
        if (temp_spp_recv_data_node_p1->node_buff)
        {
            free(temp_spp_recv_data_node_p1->node_buff);
        }
        free(temp_spp_recv_data_node_p1);
        temp_spp_recv_data_node_p1 = temp_spp_recv_data_node_p2;
    }

    SppRecvDataBuff.node_num = 0;
    SppRecvDataBuff.buff_size = 0;
    SppRecvDataBuff.first_node = NULL;
}

static void print_write_buffer(void)
{
    temp_spp_recv_data_node_p1 = SppRecvDataBuff.first_node;

    while (temp_spp_recv_data_node_p1 != NULL)
    {
        uart_write_bytes(UART_NUM_0, (char *)(temp_spp_recv_data_node_p1->node_buff), temp_spp_recv_data_node_p1->len);
        temp_spp_recv_data_node_p1 = temp_spp_recv_data_node_p1->next_node;
    }
}

void uart_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t total_num = 0;
    uint8_t current_num = 0;

    for (;;)
    {
        // Waiting for UART event.
        if (xQueueReceive(spp_uart_queue, (void *)&event, (TickType_t)portMAX_DELAY))
        {
            switch (event.type)
            {
            // Event of UART receiving data
            case UART_DATA:
                ESP_LOGI(GATTS_TABLE_TAG, "uart[%d] data, size: %d", UART_NUM_0, event.size);
                if ((event.size) && (is_connected))
                {
                    uint8_t *temp = NULL;
                    uint8_t *ntf_value_p = NULL;
#ifdef SUPPORT_HEARTBEAT
                    if (!enable_heart_ntf)
                    {
                        ESP_LOGE(GATTS_TABLE_TAG, "%s do not enable heartbeat Notify", __func__);
                        break;
                    }
#endif
                    if (!enable_data_ntf)
                    {
                        ESP_LOGE(GATTS_TABLE_TAG, "%s do not enable data Notify", __func__);
                        break;
                    }
                    temp = (uint8_t *)malloc(sizeof(uint8_t) * event.size);
                    if (temp == NULL)
                    {
                        ESP_LOGE(GATTS_TABLE_TAG, "%s malloc.1 failed", __func__);
                        break;
                    }
                    uart_read_bytes(UART_NUM_0, temp, event.size, portMAX_DELAY);
                    if (event.size <= (spp_mtu_size - 3))
                    {
#ifdef CONFIG_EXAMPLE_ENABLE_RF_EMC_TEST_MODE
                        ESP_LOG_BUFFER_HEX("TX", temp, event.size);
#endif
#ifdef CONFIG_EXAMPLE_SPP_THROUGHPUT
                        if (esp_ble_get_cur_sendable_packets_num(spp_conn_id) > 0)
                        {
                            esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id, spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL], event.size, temp, false);
                        }
                        else
                        {
                            // Add the vTaskDelay to prevent this task from consuming the CPU all the time, causing low-priority tasks to not be executed at all.
                            vTaskDelay(10 / portTICK_PERIOD_MS);
                        }
#else
                        esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id, spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL], event.size, temp, true);
#endif
                    }
                    else if (event.size > (spp_mtu_size - 3))
                    {
                        if ((event.size % (spp_mtu_size - 7)) == 0)
                        {
                            total_num = event.size / (spp_mtu_size - 7);
                        }
                        else
                        {
                            total_num = event.size / (spp_mtu_size - 7) + 1;
                        }
                        current_num = 1;
                        ntf_value_p = (uint8_t *)malloc((spp_mtu_size - 3) * sizeof(uint8_t));
                        if (ntf_value_p == NULL)
                        {
                            ESP_LOGE(GATTS_TABLE_TAG, "%s malloc.2 failed", __func__);
                            free(temp);
                            break;
                        }
                        while (current_num <= total_num)
                        {
                            if (current_num < total_num)
                            {
                                ntf_value_p[0] = '#';
                                ntf_value_p[1] = '#';
                                ntf_value_p[2] = total_num;
                                ntf_value_p[3] = current_num;
                                memcpy(ntf_value_p + 4, temp + (current_num - 1) * (spp_mtu_size - 7), (spp_mtu_size - 7));
                                esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id, spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL], (spp_mtu_size - 3), ntf_value_p, false);
                            }
                            else if (current_num == total_num)
                            {
                                ntf_value_p[0] = '#';
                                ntf_value_p[1] = '#';
                                ntf_value_p[2] = total_num;
                                ntf_value_p[3] = current_num;
                                memcpy(ntf_value_p + 4, temp + (current_num - 1) * (spp_mtu_size - 7), (event.size - (current_num - 1) * (spp_mtu_size - 7)));
                                esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id, spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL], (event.size - (current_num - 1) * (spp_mtu_size - 7) + 4), ntf_value_p, false);
                            }
                            vTaskDelay(20 / portTICK_PERIOD_MS);
                            current_num++;
                        }
                        free(ntf_value_p);
                    }
                    free(temp);
                }
                break;
            default:
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

static void spp_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_RTS,
        .rx_flow_ctrl_thresh = 124,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install UART driver, and get the queue.
    uart_driver_install(UART_NUM_0, 4096, 8192, 10, &spp_uart_queue, 0);
    // Set UART parameters
    uart_param_config(UART_NUM_0, &uart_config);
    // Set UART pins
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    xTaskCreate(uart_task, "uTask", 4096, (void *)UART_NUM_0, 8, NULL);
}

#ifdef SUPPORT_HEARTBEAT
void spp_heartbeat_task(void *arg)
{
    uint16_t cmd_id;

    for (;;)
    {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        if (xQueueReceive(cmd_heartbeat_queue, &cmd_id, portMAX_DELAY))
        {
            while (1)
            {
                heartbeat_count_num++;
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                if ((heartbeat_count_num > 3) && (is_connected))
                {
                    esp_ble_gap_disconnect(spp_remote_bda);
                }
                if (is_connected && enable_heart_ntf)
                {
                    esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id, spp_handle_table[SPP_IDX_SPP_HEARTBEAT_VAL], sizeof(heartbeat_s), heartbeat_s, false);
                }
                else if (!is_connected)
                {
                    break;
                }
            }
        }
    }
    vTaskDelete(NULL);
}
#endif

void spp_cmd_task(void *arg)
{
    uint8_t *cmd_id;

    for (;;)
    {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        if (xQueueReceive(cmd_cmd_queue, &cmd_id, portMAX_DELAY))
        {
            ESP_LOG_BUFFER_CHAR(GATTS_TABLE_TAG, (char *)(cmd_id), strlen((char *)cmd_id));
            free(cmd_id);
        }
    }
    vTaskDelete(NULL);
}

static void spp_task_init(void)
{
#ifdef CONFIG_EXAMPLE_ENABLE_RF_TESTING_CONFIGURATION_COMMAND
    rf_testing_configuration_command_enable();
#else
    spp_uart_init();
#endif // CONFIG_EXAMPLE_ENABLE_RF_TESTING_CONFIGURATION_COMMAND

#ifdef SUPPORT_HEARTBEAT
    cmd_heartbeat_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(spp_heartbeat_task, "spp_heartbeat_task", 2048, NULL, 10, NULL);
#endif

    cmd_cmd_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(spp_cmd_task, "spp_cmd_task", 4096, NULL, 10, NULL);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&spp_adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        // advertising start complete event to indicate advertising start successfully or failed
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "Advertising start failed, status %d", param->adv_start_cmpl.status);
            break;
        }
        ESP_LOGI(GATTS_TABLE_TAG, "Advertising start successfully");
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "Advertising stop failed, status %d", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTS_TABLE_TAG, "Advertising stop successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "Connection params update, status %d, conn_int %d, latency %d, timeout %d",
                 param->update_conn_params.status,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    esp_ble_gatts_cb_param_t *p_data = (esp_ble_gatts_cb_param_t *)param;
    uint8_t res = 0xff;

    switch (event)
    {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "GATT server register, status %d, app_id %d, gatts_if %d", param->reg.status, param->reg.app_id, gatts_if);
        esp_ble_gap_set_device_name(SAMPLE_DEVICE_NAME);
        esp_ble_gap_config_adv_data_raw((uint8_t *)spp_adv_data, sizeof(spp_adv_data));
        esp_ble_gatts_create_attr_tab(spp_gatt_db, gatts_if, SPP_IDX_NB, SPP_SVC_INST_ID);
        break;
    case ESP_GATTS_READ_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "Characteristic read");
        break;
    case ESP_GATTS_WRITE_EVT:
    {
        ESP_LOGI(GATTS_TABLE_TAG, "Characteristic write, conn_id %d, handle %d", param->write.conn_id, param->write.handle);
        ESP_LOGI(GATTS_TABLE_TAG, "Data: %.*s", param->write.len, param->write.value);

        if (strcmp((char *)param->write.value, "DA1") == 0)
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Doing Animation 1");
            do_animation_1();
        }
        else if (strcmp((char *)param->write.value, "DA2") == 0)
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Doing Animation 2");
            do_animation_2();
        }
        else if (strcmp((char *)param->write.value, "DA3") == 0)
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Doing Animation 3");
            do_animation_3();
        }
        else if (strcmp((char *)param->write.value, "DA4") == 0)
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Doing Animation 4");
            do_animation_4();
        }
        else if (strcmp((char *)param->write.value, "DA5") == 0)
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Doing Animation 5");
            do_animation_5();
        }
        else if (strcmp((char *)param->write.value, "DA6") == 0)
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Doing Animation 6");
            do_animation_6();
        }

        // TODO: enable larger data input using prepared writes, and handle them in execute write event
        //     	    res = find_char_and_desr_index(p_data->write.handle);
        //             if (p_data->write.is_prep == false) {
        //                 if (res == SPP_IDX_SPP_COMMAND_VAL) {
        //                     uint8_t * spp_cmd_buff = NULL;
        //                     spp_cmd_buff = (uint8_t *)malloc((spp_mtu_size - 3) * sizeof(uint8_t));
        //                     if(spp_cmd_buff == NULL){
        //                         ESP_LOGE(GATTS_TABLE_TAG, "%s malloc failed", __func__);
        //                         break;
        //                     }
        //                     memset(spp_cmd_buff, 0x0, (spp_mtu_size - 3));
        //                     memcpy(spp_cmd_buff, p_data->write.value, p_data->write.len);
        //                     xQueueSend(cmd_cmd_queue, &spp_cmd_buff, 10/portTICK_PERIOD_MS);
        //                 } else if (res == SPP_IDX_SPP_DATA_NTF_CFG) {
        //                     if ((p_data->write.len == 2) && (p_data->write.value[0] == 0x01) && (p_data->write.value[1] == 0x00)) {
        //                         ESP_LOGI(GATTS_TABLE_TAG, "SPP data notification enable");
        //                         enable_data_ntf = true;
        //                     } else if ((p_data->write.len == 2) && (p_data->write.value[0] == 0x02) && (p_data->write.value[1] == 0x00)) {
        //                         ESP_LOGI(GATTS_TABLE_TAG, "SPP data indication enable");
        //                         enable_data_ntf = true;
        //                     } else if ((p_data->write.len == 2) && (p_data->write.value[0] == 0x00) && (p_data->write.value[1] == 0x00)) {
        //                         ESP_LOGI(GATTS_TABLE_TAG, "SPP data notification/indication disable");
        //                         enable_data_ntf = false;
        //                     }
        //                 } else if (res == SPP_IDX_SPP_STATUS_CFG) {
        //                     if ((p_data->write.len == 2) && (p_data->write.value[0] == 0x01) && (p_data->write.value[1] == 0x00)) {
        //                         ESP_LOGI(GATTS_TABLE_TAG, "SPP status notification enable");
        //                     } else if ((p_data->write.len == 2) && (p_data->write.value[0] == 0x00) && (p_data->write.value[1] == 0x00)) {
        //                         ESP_LOGI(GATTS_TABLE_TAG, "SPP status notification disable");
        //                     }
        //                 }
        // #ifdef SUPPORT_HEARTBEAT
        //                 else if (res == SPP_IDX_SPP_HEARTBEAT_CFG) {
        //                     if ((p_data->write.len == 2) && (p_data->write.value[0] == 0x01) && (p_data->write.value[1] == 0x00)) {
        //                         ESP_LOGI(GATTS_TABLE_TAG, "SPP heartbeat notification enable");
        //                         enable_heart_ntf = true;
        //                     } else if ((p_data->write.len == 2) && (p_data->write.value[0] == 0x00) && (p_data->write.value[1] == 0x00)) {
        //                         ESP_LOGI(GATTS_TABLE_TAG, "SPP heartbeat notification disable");
        //                         enable_heart_ntf = false;
        //                     }
        //                 } else if (res == SPP_IDX_SPP_HEARTBEAT_VAL) {
        //                     if ((p_data->write.len == sizeof(heartbeat_s)) && (memcmp(heartbeat_s, p_data->write.value, sizeof(heartbeat_s)) == 0)) {
        //                         heartbeat_count_num = 0;
        //                     }
        //                 }
        // #endif
        //                 else if (res == SPP_IDX_SPP_DATA_RECV_VAL) {
        // #ifdef CONFIG_EXAMPLE_ENABLE_RF_EMC_TEST_MODE
        //                     ESP_LOG_BUFFER_HEX("RX", p_data->write.value, p_data->write.len);
        // #else
        //                     uart_write_bytes(UART_NUM_0, (char *)(p_data->write.value), p_data->write.len);
        // #endif
        //                 } else {
        //                     //TODO:
        //                 }
        //             } else if ((p_data->write.is_prep == true) && (res == SPP_IDX_SPP_DATA_RECV_VAL)) {
        //                 store_wr_buffer(p_data);
        //             }
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
    {
        ESP_LOGI(GATTS_TABLE_TAG, "Execute write");
        if (p_data->exec_write.exec_write_flag)
        {
            print_write_buffer();
            free_write_buffer();
        }
        break;
    }
    case ESP_GATTS_RESPONSE_EVT:
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "MTU exchange, MTU %d", param->mtu.mtu);
        spp_mtu_size = p_data->mtu.mtu;
        break;
    case ESP_GATTS_CONF_EVT:
        if (param->conf.status)
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Confirm received, status %d, handle %d", param->conf.status, param->conf.handle);
        }
        break;
    case ESP_GATTS_UNREG_EVT:
        break;
    case ESP_GATTS_DELETE_EVT:
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "Service start, status %d, service_handle %d",
                 param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "Connected, conn_id %u, remote " ESP_BD_ADDR_STR "",
                 param->connect.conn_id, ESP_BD_ADDR_HEX(param->connect.remote_bda));
        spp_conn_id = p_data->connect.conn_id;
        spp_gatts_if = gatts_if;
        is_connected = true;
        memcpy(&spp_remote_bda, &p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
#ifdef SUPPORT_HEARTBEAT
        uint16_t cmd = 0;
        xQueueSend(cmd_heartbeat_queue, &cmd, 10 / portTICK_PERIOD_MS);
#endif
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "Disconnected, remote " ESP_BD_ADDR_STR ", reason 0x%02x",
                 ESP_BD_ADDR_HEX(param->disconnect.remote_bda), param->disconnect.reason);
        spp_mtu_size = 23;
        is_connected = false;
        enable_data_ntf = false;
#ifdef SUPPORT_HEARTBEAT
        enable_heart_ntf = false;
        heartbeat_count_num = 0;
#endif
        esp_ble_gap_start_advertising(&spp_adv_params);
        break;
    case ESP_GATTS_OPEN_EVT:
        break;
    case ESP_GATTS_CANCEL_OPEN_EVT:
        break;
    case ESP_GATTS_CLOSE_EVT:
        break;
    case ESP_GATTS_LISTEN_EVT:
        break;
    case ESP_GATTS_CONGEST_EVT:
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
    {
        ESP_LOGI(GATTS_TABLE_TAG, "The number handle %x", param->add_attr_tab.num_handle);
        if (param->add_attr_tab.status != ESP_GATT_OK)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "Create attribute table failed, error code 0x%x", param->add_attr_tab.status);
        }
        else if (param->add_attr_tab.num_handle != SPP_IDX_NB)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "Create attribute table abnormally, num_handle (%d) doesn't equal to HRS_IDX_NB(%d)", param->add_attr_tab.num_handle, SPP_IDX_NB);
        }
        else
        {
            memcpy(spp_handle_table, param->add_attr_tab.handles, sizeof(spp_handle_table));
            esp_ble_gatts_start_service(spp_handle_table[SPP_IDX_SVC]);
        }
        break;
    }
    default:
        break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            spp_profile_tab[SPP_PROFILE_APP_IDX].gatts_if = gatts_if;
        }
        else
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Reg app failed, app_id %04x, status %d", param->reg.app_id, param->reg.status);
            return;
        }
    }

    do
    {
        int idx;
        for (idx = 0; idx < SPP_PROFILE_NUM; idx++)
        {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                gatts_if == spp_profile_tab[idx].gatts_if)
            {
                if (spp_profile_tab[idx].gatts_cb)
                {
                    spp_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

void app_main(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Robo cat ears app starting");
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    spp_task_init();

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(GATTS_TABLE_TAG, "%s init bluetooth", __func__);

    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&cfg);
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_app_register(ESP_SPP_APP_ID);

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(SPP_GATT_MTU_SIZE);
    if (local_mtu_ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }

    ESP_LOGI(ROBO_CAT_EARS_TAG, "Initializing LEDS");
    init_leds();
    ESP_LOGI(ROBO_CAT_EARS_TAG, "LEDS initialized");

    ESP_LOGI(ROBO_CAT_EARS_TAG, "Initializing servos");
    init_servos();
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Servos initialized");
    do_animation_1();

    ESP_LOGI(ROBO_CAT_EARS_TAG, "Robo cat ears app initialized");

    return;
}
