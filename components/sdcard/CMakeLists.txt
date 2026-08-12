idf_component_register(SRCS "sd_card.c" "src/sd_ll.c" "src/sd_diskio.c"
                    INCLUDE_DIRS "include" "src"
                    REQUIRES fatfs esp8266 esp_common freertos)