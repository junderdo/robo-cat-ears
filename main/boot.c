/*
 * Description: Main boot and initialization function for robo cat ears
 * Author: Jeff Underdown (junderdo)
 * Copyright (C) 2026 Milk Lab Creations
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "boot.h"
#include "animation_store.h"
#include "ble.h"
#include "led.h"
#include "servo.h"
#include "animation.h"
#include "controller.h"

#define ROBO_CAT_EARS_TAG "ROBO_CAT_EARS_APP"

void app_main(void)
{
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Robo cat ears app starting");
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize the animation store's own NVS partition
    ESP_ERROR_CHECK(
        animation_store_init());

    // Initialize BLE
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Initializing BLE");
    ESP_ERROR_CHECK(
        init_ble());
    ESP_LOGI(ROBO_CAT_EARS_TAG, "BLE initialized");

#if CONFIG_IDF_TARGET_ESP32C3
    // ESP32-C3 is single-core: give the BLE stack time to complete its async
    // GATTS registration and start advertising before the app tasks compete
    // for CPU with NVS, servo, and LED initialization.
    vTaskDelay(pdMS_TO_TICKS(500));
#endif

    // Initialize controller (must be after BLE, before LEDs/servos)
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Initializing controller");
    ESP_ERROR_CHECK(
        controller_init());
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Controller initialized");

    // Initialize LEDs
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Initializing LEDs");
    ESP_ERROR_CHECK(
        init_leds());
    ESP_LOGI(ROBO_CAT_EARS_TAG, "LEDs initialized");

    // Initialize servos
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Initializing servos");
    ESP_ERROR_CHECK(
        init_servos());
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Servos initialized");
    do_animation(3);

    ESP_LOGI(ROBO_CAT_EARS_TAG, "Robo cat ears app initialized");
}
