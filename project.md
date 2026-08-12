# ESP8266 OLED & RTC D1 Mini

> Living project document. Update this file when hardware, wiring, build steps, or APIs change.

This project is licensed under the MIT License. See [`LICENSE`](LICENSE).

## Overview

Firmware for a Wemos/LOLIN D1 mini ESP8266 with:

- a 0.66-inch SSD1315 64x48 OLED shield,
- a DS3231 RTC,
- an optional D1 mini RTC + SD shield.

The current default display shows a centered date and time. SD initialization and CSV logging are optional and disabled by default.

## Hardware

| Device | Details | Connection |
| --- | --- | --- |
| MCU | Wemos/LOLIN D1 mini, ESP8266, 4 MB flash | - |
| OLED | SSD1315, 64x48, I2C address `0x3C` or `0x3D` | SDA GPIO4/D2, SCL GPIO5/D1 |
| RTC | DS3231 at I2C address `0x68` | Shared I2C bus |
| RTC alternatives | DS1307 at `0x68`, PCF8563 at `0x51` | Supported by the RTC component |
| microSD | SPI, optional | CS GPIO15/D8, SCK GPIO14/D5, MOSI GPIO13/D7, MISO GPIO12/D6 |

### Pin map

| D1 mini pin | GPIO | Function |
| --- | ---: | --- |
| D1 | 5 | I2C SCL |
| D2 | 4 | I2C SDA |
| D5 | 14 | SD SCK |
| D6 | 12 | SD MISO |
| D7 | 13 | SD MOSI |
| D8 | 15 | SD CS |

`GPIO15` is a boot-strapping pin held low during reset. It is configured as SD chip-select only after boot.

## SDK

Modern ESP-IDF does not support the ESP8266. This project uses Espressif's official:

```text
ESP8266_RTOS_SDK v3.4
C:\esp\ESP8266_RTOS_SDK
```

The project uses the Xtensa LX106 toolchain, CMake, Ninja, and the SDK's `idf.py` build system.

## Repository layout

```text
CMakeLists.txt
sdkconfig.defaults
export.ps1
build.ps1
flash.ps1
project.md

components/
  i2c_bus/       Shared I2C master implementation
  ssd1306/       SSD1315-compatible 64x48 display driver and 5x7 font
  rtc/           RTC detection, read, write, and temperature API
  sdcard/        Optional bit-banged SD SPI and FatFs diskio implementation

main/
  app_main.c
  Kconfig.projbuild
```

## Components

### I2C bus

Header: `components/i2c_bus/include/i2c_bus.h`

- Initializes I2C0 on GPIO4/GPIO5.
- Supports raw writes, register writes, register reads, and device probing.
- Shared by the OLED and RTC components.

### OLED

Header: `components/ssd1306/include/ssd1306.h`

Although the component retains the historical `ssd1306` name, it is configured for the SSD1315 64x48 panel.

- 64x48 framebuffer: 384 bytes.
- SSD1315/Adafruit-compatible GDDRAM window: columns 32 through 95.
- I2C data is sent in 32-byte chunks.
- Includes pixel, rectangle, character, string, clear, fill, and refresh APIs.
- Uses a 5x7 ASCII font.

Panel geometry and column offset are defined in:

```text
components/ssd1306/include/ssd1306_config.h
```

### RTC

Header: `components/rtc/include/rtcdev.h`

- `rtc_detect()` probes supported RTC addresses.
- `rtc_get_time()` reads the current date/time.
- `rtc_set_time()` writes the date/time and calculates weekday externally.
- `rtc_get_temperature()` supports DS3231 temperature readings.

The header is named `rtcdev.h` because the SDK already contains `driver/rtc.h`.

### SD card

Header: `components/sdcard/include/sd_card.h`

The ESP8266 SDK does not provide the ESP32 SDMMC/FatFs glue, so this component implements SD SPI protocol support directly and registers a FatFs diskio driver.

SD logging is disabled by default. Enable it in `sdkconfig`:

```ini
CONFIG_APP_ENABLE_SD=y
```

When enabled, the card is mounted at `/sdcard` and readings are written to `/sdcard/log.csv`.

## Application behavior

At startup the application:

1. Initializes the UART0 console at 115200 baud.
2. Initializes the shared I2C bus.
3. Initializes the SSD1315 OLED at address `0x3C`.
4. Detects the RTC and initializes the system clock from it.
5. Mounts the SD card only when `CONFIG_APP_ENABLE_SD=y`.
6. Starts the display task.

The OLED displays:

- centered date on the first line,
- centered time on the second line,
- a border around the 64x48 display.

## Serial console

Use a serial monitor at 115200 baud. Commands:

| Command | Description |
| --- | --- |
| `SETTIME yyyy-mm-dd hh:mm:ss` | Set the RTC and resynchronize the system clock |
| `TIME` | Print the current RTC time |
| `HELP` | Print the available commands |

## Optional SD logging

Enable SD support by setting:

```ini
CONFIG_APP_ENABLE_SD=y
```

Then insert a FAT/FAT32 card before booting. The CSV format is:

```text
timestamp,temp_c
2026-08-12 09:30:01,24.3
```

## Build and flash

From PowerShell:

```powershell
. .\export.ps1
idf.py build
idf.py -p COMx flash monitor
```

Alternatively:

```powershell
.\build.ps1
.\flash.ps1 -Port COMx
```

Replace `COMxx` with the actual serial port.

Firmware image:

```text
build\esp8266_oled_rtc_sd.bin
```

Flash layout:

```text
Bootloader:       0x0000
Partition table:  0x8000
Application:      0x10000
```

## Configuration defaults

`sdkconfig.defaults` contains:

```ini
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESP8266_TIME_SYSCALL_USE_FRC1=y
```

The generated `sdkconfig` file is local configuration and should not be committed.

## Verified status

- Project builds successfully with ESP8266_RTOS_SDK v3.4.
- Firmware flashed successfully to the D1 mini.
- SSD1315 64x48 OLED geometry and GDDRAM offset verified.
- Centered date/time display verified.
- DS3231 detected and time-setting console verified.
- Optional SD logging remains untested because no card was available.
