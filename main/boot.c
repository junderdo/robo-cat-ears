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

    // Initialize BLE
    ESP_LOGI(ROBO_CAT_EARS_TAG, "Initializing BLE");
    ESP_ERROR_CHECK(
        init_ble());
    ESP_LOGI(ROBO_CAT_EARS_TAG, "BLE initialized");

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
