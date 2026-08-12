#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default pins used by the "D1 mini RTC + SD" shield. */
#define SD_DEFAULT_GPIO_CS   15
#define SD_DEFAULT_GPIO_SCK  14
#define SD_DEFAULT_GPIO_MOSI 13
#define SD_DEFAULT_GPIO_MISO 12

#define SD_BLOCK_SIZE 512

typedef struct {
    int gpio_cs;
    int gpio_sck;
    int gpio_mosi;
    int gpio_miso;
    uint32_t sector_count; /* total number of 512-byte sectors */
    bool sdhc;             /* block addressed card (SDHC/SDXC) */
    bool mounted;
    unsigned char pdrv;    /* FatFs physical drive number */
    char base_path[16];
} sd_card_t;

/**
 * @brief Mount the SD card as a FAT filesystem on the shared SPI pins.
 *
 * GPIO15 can only be used as CS/SPI *after* boot (it is held low during
 * reset). After a successful mount the card is available below base_path,
 * e.g. "/sdcard".
 *
 * @param card      caller-allocated card handle; gpio_* fields are copied,
 *                  defaults are applied when a GPIO field is < 0
 * @param base_path VFS mount point, e.g. "/sdcard"
 * @return ESP_OK on success
 */
esp_err_t sd_card_mount(sd_card_t *card, const char *base_path);

/**
 * @brief Unmount and de-initialise the card.
 */
esp_err_t sd_card_unmount(sd_card_t *card);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_H */