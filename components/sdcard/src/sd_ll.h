#ifndef SD_LL_H
#define SD_LL_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Low-level SD card in SPI mode, implemented with bit-banged GPIO.
 *
 * ESP8266_RTOS_SDK does not ship the SDMMC/SPI protocol stack for the ESP8266
 * target (the fatfs sdmmc glue is only built for esp32), so the card is driven
 * directly here: a handful of commands over SPI and a FatFs diskio binding in
 * sd_diskio.c.
 */

typedef struct {
    int gpio_cs;
    int gpio_sck;
    int gpio_mosi;
    int gpio_miso;
} sd_ll_pins_t;

/**
 * @brief Register this component's FatFs diskio driver for a physical drive.
 * @param pdrv drive number obtained from ff_diskio_get_drive()
 */
void sd_diskio_register_for_pdrv(unsigned char pdrv);

void sd_ll_set_pins(const sd_ll_pins_t *pins);

/**
 * @brief Initialise SPI mode on a card (CMD0/CMD8/ACMD41/CMD16, reads CSD).
 *
 * Slow (~400 kHz) clock during init, fast clock afterwards.
 *
 * @return ESP_OK on success
 */
esp_err_t sd_ll_init(void);

bool sd_ll_ready(void);

/**
 * @brief Total number of 512-byte sectors.
 */
uint32_t sd_ll_get_sector_count(void);

bool sd_ll_is_sdhc(void);

/**
 * @brief Read one sector.
 * @param sector sector number (block address for SDHC cards)
 */
esp_err_t sd_ll_read_block(uint32_t sector, uint8_t *dst);

/**
 * @brief Write one sector.
 */
esp_err_t sd_ll_write_block(uint32_t sector, const uint8_t *src);

#ifdef __cplusplus
}
#endif

#endif /* SD_LL_H */