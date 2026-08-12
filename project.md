# esp8266oledrtc — Project document

> Living document. Update it whenever hardware, wiring, build steps, or APIs change.

## Purpose

Firmware for a **Wemos/LOLIN D1 mini** (ESP8266 4 MB flash) that:

- reads the current date/time from the RTC,
- shows a centered date and time on the 0.66" SSD1315 OLED,
- optionally logs timestamped temperature readings to a microSD card as CSV.

A "headless clock + datalogger" reference project.

## Hardware

| Item                 | Part / interface            | Connection                                             |
| -------------------- | --------------------------- | ------------------------------------------------------ |
| MCU                  | Wemos D1 mini, ESP8266      | —                                                      |
| Display              | 0.66" 64×48 SSD1315, I2C (D1 mini OLED shield) | SDA → GPIO4 (D2), SCL → GPIO5 (D1), addr `0x3C` (or `0x3D`) |
| Clock & datalogging  | "D1 mini RTC + SD" shield   | —                                                      |
| · RTC                | DS3231 @ `0x68` / DS1307 @ `0x68` / PCF8563 @ `0x51` | shares the I2C bus (GPIO4/GPIO5) |
| · microSD            | SPI (bit-banged)            | CS → GPIO15 (D8), SCK → GPIO14 (D5), MOSI → GPIO13 (D7), MISO → GPIO12 (D6) |

### Pin map (D1 mini → GPIO)

| Shell label | GPIO |
| ----------- | ---- |
| D1 / SCL    | 5    |
| D2 / SDA    | 4    |
| D5 / SCK    | 14   |
| D6 / MISO   | 12   |
| D7 / MOSI   | 13   |
| D8 / CS     | 15   |

> `GPIO15` is a boot-strapping pin held **low during reset**; it is only configured as
> an output (SD card-select) *after* boot. This is standard for D1 mini SD shields.

## SDK & toolchain

**ESP-IDF v6.x does NOT support the ESP8266.** This project builds with Espressif's
official **ESP8266_RTOS_SDK v3.4** (`C:\esp\ESP8266_RTOS_SDK`).

Installed tooling (Windows):

| Tool                 | Location                                                            |
| -------------------- | ------------------------------------------------------------------- |
| Xtensa LX106 GCC     | `C:\Espressif\tools\tools\xtensa-lx106-elf\esp-2020r3-49-gd5524c1-8.4.0\xtensa-lx106-elf\bin` |
| CMake 3.13.4         | `C:\Espressif\tools\tools\cmake\3.13.4\bin`                         |
| Ninja 1.9.0          | `C:\Espressif\tools\tools\ninja\1.9.0`                              |
| mconf (menuconfig)   | `C:\Espressif\tools\tools\mconf\v4.6.0.0-idf-20190628`              |
| Python venv          | `C:\Espressif\tools\python\esp8266\Scripts\python.exe`              |

Notes:
- `idf.py` is invoked with the **esp8266 venv's** Python (`C:\Espressif\tools\python\esp8266`),
  not the ESP-IDF v6 venv.
- SDK git submodules are initialized (`lwip`, `mbedtls`, `json`, `mqtt`, `coap`). The nested
  `coap/ext/tinydtls` submodule has a dead URL and must **not** be updated recursively.

## Repository layout

```
CMakeLists.txt                  top-level ESP-IDF-style project file
sdkconfig.defaults              build defaults (4 MB flash, FRC1 time)
export.ps1                      dot-source for dev sessions: env + idf.py helper
build.ps1                       build only
flash.ps1                       build + flash + monitor (Port COM3 by default)
components/
  i2c_bus/                      shared I2C master (installs the bus once, used by OLED + RTC)
  ssd1306/                      SSD1315-compatible 64x48 driver, framebuffer + 5x7 font
  rtc/                          RTC detection + time/temp API (header: rtcdev.h!)
  sdcard/                       SD-over-SPI (bit-banged) + FatFs diskio binding
main/
  app_main.c                    application wiring, console, and display loop
```

## Components / APIs

### `i2c_bus` (shared I2C, `include/i2c_bus.h`)
- `i2c_bus_init(sda_gpio, scl_gpio, clk_stretch_ticks)` — idempotent (`i2c_driver_install`
  returning `ESP_FAIL` because already installed is treated as OK).
- `i2c_bus_write_raw / write_reg / read_regs / probe`.

### `ssd1306` (`include/ssd1306.h`)
> Panel geometry lives in `include/ssd1306_config.h` (defaults: 64×48 = the D1 mini
> OLED shield, **SSD1315** controller). For a 128×64 SSD1306 panel override
> `SSD1306_WIDTH=128` / `SSD1306_HEIGHT=64` as compile definitions.

- `ssd1306_init(i2c_addr)`, `ssd1306_clear/fill`, `ssd1306_draw_pixel/get_pixel`,
  `ssd1306_draw_rect`, `ssd1306_draw_char/draw_string`, `ssd1306_refresh()`.
- Uses a 384-byte framebuffer; `refresh()` uses the Adafruit-compatible 64x48 window
  (`0x21`, columns 32..95; `0x22` page range) and 32-byte I2C data chunks.

### `rtc` (`include/rtcdev.h`)
> Header is named **`rtcdev.h`**, not `rtc.h`, because `components/esp8266/include/driver/rtc.h`
> (the RTC-memory driver) shadows `rtc.h` on the include path.

- `rtc_detect()` — probes `0x68` (DS3231, or DS1307 if the status register reads `0xFF`)
  then `0x51` (PCF8563). Returns a `rtc_type_t`.
- `rtc_get_time / rtc_set_time(rtc_time_t *)`, `rtc_get_temperature(&celsius)` (DS3231 only),
  `rtc_present()`.
- System clock is synced from the RTC at boot so FAT file timestamps are correct.

### `sdcard` (`include/sd_card.h`)
> The SDK's FatFs SD glue (`esp_vfs_fat_sdmmc_mount`) is only compiled for **esp32**, so
> this component provides its own SPI + SD + FatFs diskio stack.

- Bit-banged full-duplex SPI (`src/sd_ll.c`): slow ~400 kHz clock during init,
  fast after; CMD0/CMD8/ACMD41/CMD16/CMD9(+CSD decode)/CMD17/CMD24.
- `src/sd_diskio.c` registers a `ff_diskio_impl_t` for a pdrv.
- `sd_card_mount(&card, "/sdcard")` → mount + register VFS (`esp_vfs_fat_register` + `f_mount`);
  `sd_card_unmount(&card)`.
- Default pins if the struct GPIO fields are `< 0`: CS=15, SCK=14, MOSI=13, MISO=12.

## Application (`main/app_main.c`)

Boot sequence:
1. `console_init()` — UART0 console for `SETTIME`/`TIME`/`HELP`.
2. `i2c_bus_init(4, 5, 300)`
3. `ssd1306_init(0x3C)` — display is optional; missing OLED is logged, not fatal.
4. `rtc_detect()`; if present, initialize system time from the RTC.
5. Optionally mount SD when `CONFIG_APP_ENABLE_SD=y`.
6. Start `app_task` (4 KB stack) — 1 s loop:
   - poll the serial console,
   - display centered date and time.

Optional CSV format (`/sdcard/log.csv`):
```
timestamp,temp_c
2026-08-12 09:30:01,24.3
```

## Serial console

Connect at 115200 baud (e.g. `idf.py -p COMx monitor`). Commands:

| Command                      | Effect                                   |
| ---------------------------- | ---------------------------------------- |
| `SETTIME yyyy-mm-dd hh:mm:ss`| Write date/time to the RTC, resync OS clock, compute weekday |
| `TIME`                       | Print the RTC's current date/time        |
| `HELP`                       | List commands                            |

The console polls UART0 in the 1 s app task (RX buffer 256 B, no TX buffer).

## SD logging

SD initialization and CSV logging are disabled by default, so an empty SD slot
does not produce warnings or display status text. To enable them, edit
`sdkconfig` and set:

```ini
CONFIG_APP_ENABLE_SD=y
```

Then rebuild and flash with a FAT/FAT32 card inserted.

## Build & flash

```powershell
. .\export.ps1        # load env once per session
idf.py build          # build only

# or one-shot:
.\build.ps1
.\flash.ps1 -Port COM3          # flash + monitor (default 115200 baud)
```

Equivalent manual command (after `export.ps1`):

```powershell
idf.py -p COM3 -b 460800 flash monitor
```

Flash layout: bootloader @ `0x0`, partition table @ `0x8000`, app @ `0x10000` —
binary `build\esp8266_oled_rtc_sd.bin`.

## Configuration (`sdkconfig.defaults`)

- `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` — D1 mini ships with 4 MB flash.
- `CONFIG_ESP8266_TIME_SYSCALL_USE_FRC1=y` — high-res timer backing `gettimeofday` /
  FAT timestamps.

Use `idf.py menuconfig` to inspect/adjust the generated `sdkconfig` (do not commit it).

## Known issues / decisions

- **RTC model:** `0x68` detection assumes DS3231 unless the status register reads `0xFF`,
  then DS1307. Time read/write is identical for both. Temperature is DS3231-only.
  If a shield uses another chip, extend `rtc.c` and `rtc_detect()`.
- **SD speed:** bit-banging caps sustained throughput (~1 MHz data clock). Fine for burst
  logging; not for streaming files at high rates.
- **SD is disabled by default:** this avoids warnings and logging when no card is inserted.
  Enable it with `CONFIG_APP_ENABLE_SD=y` in `sdkconfig`.
- **`rtc_init` symbol / header names:** deliberately avoided (`rtc_detect`, `rtcdev.h`).
- **Windows build prerequisites:** the build expects the toolchain/CMake/Ninja/mconf paths
  listed above under `C:\Espressif\...`; adjust `export.ps1` if tools live elsewhere.

## Changelog / todo

- [x] Initial scaffold + buildable firmware.
- [x] On-target verification: OLED init, DS3231 detect, serial console `SETTIME`/`TIME`.
- [x] Serial console to set the RTC time (`SETTIME yyyy-mm-dd hh:mm:ss`).
- [x] OLED geometry, column offset, 32-byte I2C transfers, and centered date/time verified on hardware.
- [ ] SD mount verified on hardware (disabled by default; requires a FAT/FAT32 card).
- [ ] Optional: FAT formatting of an unformatted card at mount (`format_if_mount_failed`).
