idf_component_register(SRCS "rtc.c"
                    INCLUDE_DIRS "include"
                    REQUIRES i2c_bus esp_common)