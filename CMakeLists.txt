# ESP8266_RTOS_SDK project for a Wemos D1 mini with:
#  - 0.96" SSD1306 OLED shield (I2C: SDA=GPIO4, SCL=GPIO5)
#  - "D1 mini RTC + SD" shield (RTC: DS3231@0x68 / PCF8563@0x51 on I2C,
#                               SD card: SPI CS=GPIO15, SCK=GPIO14, MOSI=GPIO13, MISO=GPIO12)
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp8266_oled_rtc_sd)