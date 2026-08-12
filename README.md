# ESP8266 D1 Mini OLED RTC

Firmware for a Wemos/LOLIN D1 mini ESP8266 with an SSD1315 OLED shield and RTC shield.

## Features

- Centered date and time on a 0.66-inch SSD1315 64x48 OLED.
- DS3231 RTC support, with DS1307 and PCF8563 detection support.
- Serial console for reading and setting the RTC.
- Optional microSD card support with FatFs CSV logging.
- ESP8266_RTOS_SDK-based CMake project.

## Hardware

| Device | Details |
| --- | --- |
| Board | Wemos/LOLIN D1 mini, ESP8266 |
| OLED | SSD1315, 64x48, I2C address `0x3C` or `0x3D` |
| RTC | DS3231 at `0x68` |
| SD CS | GPIO15 / D8 |

### Pin map

| D1 mini | GPIO | Function |
| --- | ---: | --- |
| D1 | 5 | I2C SCL |
| D2 | 4 | I2C SDA |
| D5 | 14 | SD SCK |
| D6 | 12 | SD MISO |
| D7 | 13 | SD MOSI |
| D8 | 15 | SD CS |

## SDK

Modern ESP-IDF does not support the ESP8266. This project uses:

```text
ESP8266_RTOS_SDK v3.4
```

The SDK must be installed separately and exposed through `IDF_PATH`.

## Build and flash

From PowerShell:

```powershell
. .\export.ps1
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMxx` with the board's serial port.

Convenience scripts are also provided:

```powershell
.\build.ps1
.\flash.ps1 -Port COMxx
```

The firmware image is generated at:

```text
build\esp8266_oled_rtc_sd.bin
```

## Serial console

Use 115200 baud. Available commands:

```text
SETTIME yyyy-mm-dd hh:mm:ss
TIME
HELP
```

Example:

```text
SETTIME 2026-08-12 21:00:00
```

## Optional SD logging

SD support is disabled by default. To enable it, edit the local, ignored `sdkconfig` file:

```ini
CONFIG_APP_ENABLE_SD=y
```

Insert a FAT/FAT32 card before booting. Log data is written to:

```text
/sdcard/log.csv
```

## Project structure

```text
components/i2c_bus/   Shared I2C bus implementation
components/ssd1306/   SSD1315-compatible 64x48 display driver
components/rtc/       RTC detection and date/time API
components/sdcard/    Optional SD SPI and FatFs implementation
main/                  Application startup and display loop
project.md             Detailed living project document
```

## License

This project is licensed under the [MIT License](LICENSE).

The ESP8266_RTOS_SDK and other external dependencies retain their respective licenses.
