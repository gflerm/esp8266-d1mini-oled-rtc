#include <stdint.h>
#include <string.h>
#include "diskio_impl.h"
#include "ff.h"
#include "esp_log.h"
#include "sd_ll.h"

#define SD_DISK_SECTOR_SIZE 512

static const char *TAG = "sd_diskio";

static bool s_initialized = false;

static DSTATUS sd_disk_init(BYTE pdrv)
{
    esp_err_t err = sd_ll_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
        s_initialized = false;
        return STA_NOINIT;
    }
    s_initialized = true;
    return 0;
}

static DSTATUS sd_disk_status(BYTE pdrv)
{
    return sd_ll_ready() ? 0 : STA_NOINIT;
}

static DRESULT sd_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if (!sd_ll_ready()) {
        return RES_NOTRDY;
    }
    while (count--) {
        if (sd_ll_read_block(sector++, buff) != ESP_OK) {
            return RES_ERROR;
        }
        buff += SD_DISK_SECTOR_SIZE;
    }
    return RES_OK;
}

static DRESULT sd_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if (!sd_ll_ready()) {
        return RES_NOTRDY;
    }
    while (count--) {
        if (sd_ll_write_block(sector++, buff) != ESP_OK) {
            return RES_ERROR;
        }
        buff += SD_DISK_SECTOR_SIZE;
    }
    return RES_OK;
}

static DRESULT sd_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *((DWORD *)buff) = sd_ll_get_sector_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *((WORD *)buff) = SD_DISK_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *((DWORD *)buff) = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

static const ff_diskio_impl_t s_sd_impl = {
    .init   = sd_disk_init,
    .status = sd_disk_status,
    .read   = sd_disk_read,
    .write  = sd_disk_write,
    .ioctl  = sd_disk_ioctl,
};

void sd_diskio_register_for_pdrv(unsigned char pdrv)
{
    ff_diskio_register(pdrv, &s_sd_impl);
}