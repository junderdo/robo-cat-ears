set(srcs "boot.c" "led.c" "servo.c" "servo_calibration.c" "ble.c" "controller.c")

if(CONFIG_EXAMPLE_ENABLE_RF_TESTING_CONFIGURATION_COMMAND)
list(APPEND srcs
     "rf_testing_configuration_cmd.c")
endif()

idf_component_register(SRCS "${srcs}"
                    PRIV_REQUIRES bt nvs_flash esp_driver_uart console espressif__servo driver
                    INCLUDE_DIRS ".")
