#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "diskio_impl.h"
#include "sd_ll.h"
#include "sd_card.h"

static const char *TAG = "sdcard";

esp_err_t sd_card_mount(sd_card_t *card, const char *base_path)
{
    if (!card) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(card, 0, sizeof(*card));

    card->gpio_cs   = SD_DEFAULT_GPIO_CS;
    card->gpio_sck  = SD_DEFAULT_GPIO_SCK;
    card->gpio_mosi = SD_DEFAULT_GPIO_MOSI;
    card->gpio_miso = SD_DEFAULT_GPIO_MISO;

    /* GPIO15 is used as the card chip-select. It is held low by the hardware
     * during boot, so configure it as a plain output *after* startup. */
    sd_ll_pins_t pins = {
        .gpio_cs   = card->gpio_cs,
        .gpio_sck  = card->gpio_sck,
        .gpio_mosi = card->gpio_mosi,
        .gpio_miso = card->gpio_miso,
    };
    sd_ll_set_pins(&pins);

    esp_err_t err = sd_ll_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "card init failed: %s", esp_err_to_name(err));
        return err;
    }

    BYTE pdrv;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK) {
        ESP_LOGE(TAG, "no free FatFs drive slot");
        return ESP_ERR_NO_MEM;
    }
    char drv[3] = { (char)('0' + pdrv), ':', 0 };

    sd_diskio_register_for_pdrv(pdrv);

    FATFS *fs = NULL;
    err = esp_vfs_fat_register(base_path, drv, 4, &fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_register failed: %s", esp_err_to_name(err));
        ff_diskio_unregister(pdrv);
        return err;
    }

    FRESULT fr = f_mount(fs, drv, 1);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "f_mount failed: %d", (int)fr);
        esp_vfs_fat_unregister_path(base_path);
        ff_diskio_unregister(pdrv);
        return ESP_FAIL;
    }

    card->pdrv    = pdrv;
    strncpy(card->base_path, base_path, sizeof(card->base_path) - 1);
    card->sector_count = sd_ll_get_sector_count();
    card->sdhc         = sd_ll_is_sdhc();
    card->mounted      = true;

    ESP_LOGI(TAG, "mounted at %s, %u sectors, %s",
             card->base_path, card->sector_count,
             card->sdhc ? "SDHC/SDXC" : "SDSC");
    return ESP_OK;
}

esp_err_t sd_card_unmount(sd_card_t *card)
{
    if (!card || !card->mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    char drv[3] = { (char)('0' + card->pdrv), ':', 0 };
    f_mount(NULL, drv, 0);
    esp_vfs_fat_unregister_path(card->base_path);

    card->mounted = false;
    return ESP_OK;
}