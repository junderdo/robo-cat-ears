set(srcs "boot.c" "led.c" "servo.c" "servo_calibration.c" "animation.c" "ble.c" "controller.c" "store.c" "animation_store.c" "device_serial.c")

if(CONFIG_EXAMPLE_ENABLE_RF_TESTING_CONFIGURATION_COMMAND)
list(APPEND srcs
     "rf_testing_configuration_cmd.c")
endif()

idf_component_register(SRCS "custom_animation.c" "animation_mode.c" "${srcs}"
                    PRIV_REQUIRES bt mbedtls nvs_flash esp_driver_uart console espressif__servo driver
                    INCLUDE_DIRS ".")
