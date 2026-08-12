idf_component_register(SRCS "app_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES i2c_bus ssd1306 rtc sdcard pthread nvs_flash)
