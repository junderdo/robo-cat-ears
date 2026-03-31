set(srcs "boot.c" "ble.c" "led.c" "servo.c")

if(CONFIG_EXAMPLE_ENABLE_RF_TESTING_CONFIGURATION_COMMAND)
list(APPEND srcs
     "rf_tesing_configuration_cmd.c")
endif()

idf_component_register(SRCS "${srcs}"
                    PRIV_REQUIRES bt nvs_flash esp_driver_uart console espressif__servo driver
                    INCLUDE_DIRS ".")
