#include <stdint.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_ll.h"

#define SD_BLOCK_SIZE 512

static const char *TAG = "sd_ll";

static sd_ll_pins_t s_pins = { -1, -1, -1, -1 };
static bool    s_ready  = false;
static bool    s_sdhc   = false;
static uint32_t s_sectors = 0;

/* Rough clock shaping: slow (~400 kHz) during card init, fast after. */
static inline void clock_delay(int slow)
{
    volatile int n = slow ? 26 : 2;
    while (n--) {
    }
}

/* Full-duplex byte. SD cards are simultaneously clocked, so every byte we
 * shift out (0xFF while reading) also shifts one back in on MISO. */
static uint8_t spi_transfer(uint8_t out, int slow)
{
    uint8_t in = 0;
    for (int bit = 7; bit >= 0; bit--) {
        gpio_set_level((gpio_num_t)s_pins.gpio_mosi, (out >> bit) & 1u);
        gpio_set_level((gpio_num_t)s_pins.gpio_sck, 1);
        clock_delay(slow);
        in = (uint8_t)((in << 1) | (uint8_t)(gpio_get_level((gpio_num_t)s_pins.gpio_miso) ? 1 : 0));
        gpio_set_level((gpio_num_t)s_pins.gpio_sck, 0);
    }
    return in;
}

static inline void cs_low(void)
{
    gpio_set_level((gpio_num_t)s_pins.gpio_cs, 0);
}

static inline void cs_high(void)
{
    gpio_set_level((gpio_num_t)s_pins.gpio_cs, 1);
}

/* Send the 6 byte command frame, then poll for R1. */
static uint8_t sd_send_cmd(uint8_t idx, uint32_t arg, uint8_t crc, int slow)
{
    uint8_t frame[6];
    frame[0] = (uint8_t)(0x40 | idx);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)arg;
    frame[5] = crc;
    for (int i = 0; i < 6; i++) {
        spi_transfer(frame[i], slow);
    }
    uint8_t r = 0xFF;
    for (int i = 0; i < 24 && (r & 0x80); i++) {
        r = spi_transfer(0xFF, slow);
    }
    return r;
}

static void read_extra(uint8_t *out, int n, int slow)
{
    for (int i = 0; i < n; i++) {
        out[i] = spi_transfer(0xFF, slow);
    }
}

/* Raise CS and clock the card to release the bus. */
static void cmd_end(int n)
{
    cs_high();
    for (int i = 0; i < n; i++) {
        spi_transfer(0xFF, 0);
    }
}

static bool valid_pins(void)
{
    return s_pins.gpio_cs >= 0 && s_pins.gpio_sck >= 0 &&
           s_pins.gpio_mosi >= 0 && s_pins.gpio_miso >= 0;
}

static esp_err_t read_csd(uint32_t *out_sectors, bool *out_sdhc)
{
    uint8_t r;
    cs_low();
    r = sd_send_cmd(9, 0, 0x01, 0); /* SEND_CSD */
    if (r != 0x00) {
        cmd_end(8);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t b;
    int i;
    for (i = 0; i < 32; i++) {
        b = spi_transfer(0xFF, 0);
        if (b != 0xFF) {
            break;
        }
    }
    if (b != 0xFE) {
        cmd_end(8);
        return ESP_ERR_TIMEOUT;
    }

    uint8_t csd[16];
    for (i = 0; i < 16; i++) {
        csd[i] = spi_transfer(0xFF, 0);
    }
    spi_transfer(0xFF, 0); /* crc high   */
    spi_transfer(0xFF, 0); /* crc low    */
    cmd_end(8);

    int ver  = csd[0] >> 6;
    uint32_t sectors = 0;
    if (ver == 0) { /* CSD v1.0 */
        int read_bl_len = csd[5] & 0x0F;
        int c_size      = (int)((csd[6] & 0x03) << 10) | (csd[7] << 2) | (csd[8] >> 6);
        int c_size_mult = (int)((csd[9] & 0x03) << 1) | (csd[10] >> 7);
        uint64_t blocks = (uint64_t)(c_size + 1) << (c_size_mult + 2);
        uint64_t bytes  = blocks << read_bl_len;
        sectors = (uint32_t)(bytes / SD_BLOCK_SIZE);
    } else if (ver == 1) { /* CSD v2.0 */
        uint32_t c_size = (uint32_t)(csd[6] & 0x3F) << 16 | (uint32_t)csd[7] << 8 | csd[8];
        sectors = (c_size + 1) * 1024u;
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }

    *out_sectors = sectors;
    *out_sdhc    = (ver >= 1);
    return ESP_OK;
}

esp_err_t sd_ll_init(void)
{
    s_ready  = false;
    s_sdhc   = false;
    s_sectors = 0;
    if (!valid_pins()) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t io;
    io.intr_type = GPIO_INTR_DISABLE;
    io.mode      = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (uint64_t)(1ULL << s_pins.gpio_cs) |
                      (uint64_t)(1ULL << s_pins.gpio_sck) |
                      (uint64_t)(1ULL << s_pins.gpio_mosi);
    io.pull_down_en = 0;
    io.pull_up_en   = 0;
    gpio_config(&io);

    io.mode         = GPIO_MODE_INPUT;
    io.pin_bit_mask = (uint64_t)(1ULL << s_pins.gpio_miso);
    io.pull_up_en   = 1;
    gpio_config(&io);

    cs_high();
    gpio_set_level((gpio_num_t)s_pins.gpio_sck, 0);
    gpio_set_level((gpio_num_t)s_pins.gpio_mosi, 1);

    /* power-on: at least 74 clock cycles with CS high */
    for (int i = 0; i < 80; i++) {
        spi_transfer(0xFF, 1);
    }

    /* GO_IDLE */
    esp_err_t err = ESP_ERR_TIMEOUT;
    for (int attempt = 0; attempt < 10; attempt++) {
        cs_low();
        uint8_t r = sd_send_cmd(0, 0, 0x95, 1);
        cmd_end(8);
        if (r == 0x01) {
            err = ESP_OK;
            break;
        }
        vTaskDelay(portTICK_PERIOD_MS * 3);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CMD0 failed, no card?");
        return err;
    }

    /* SEND_IF_COND: detect SD v2 */
    bool v2 = false;
    cs_low();
    uint8_t r = sd_send_cmd(8, 0x000001AA, 0x87, 1);
    if (r == 0x01) {
        uint8_t echo[4];
        read_extra(echo, 4, 1);
        if ((echo[2] == 0x01) && (echo[3] == 0xAA)) {
            v2 = true;
        } else {
            cmd_end(8);
            return ESP_ERR_INVALID_RESPONSE;
        }
    } else if (r != 0x05) { /* 0x05 = illegal command, plain v1 card */
        cmd_end(8);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Wait for the card to leave the idle state (ACMD41). */
    uint32_t arg_acmd41 = v2 ? 0x40000000u : 0x00000000u;
    int retry = 0;
    do {
        r = sd_send_cmd(55, 0, 0x01, 1); /* APP_CMD */
        if (r > 1) {
            cmd_end(8);
            return ESP_ERR_INVALID_RESPONSE;
        }
        r = sd_send_cmd(41, arg_acmd41, 0x01, 1); /* SD_SEND_OP_COND */
        if (r == 0x00) {
            break;
        }
        if (r != 0x01) {
            cmd_end(8);
            return ESP_ERR_INVALID_RESPONSE;
        }
        for (int i = 0; i < 8; i++) {
            spi_transfer(0xFF, 1);
        }
        retry++;
        if ((retry % 20) == 0) {
            vTaskDelay(portTICK_PERIOD_MS * 10);
        }
    } while (retry < 5000);
    if (r != 0x00) {
        ESP_LOGE(TAG, "ACMD41 timeout");
        cmd_end(8);
        return ESP_ERR_TIMEOUT;
    }
    cmd_end(8);

    /* READ_OCR: block address mode for SDHC/SDXC is set in OCR bit 30 */
    cs_low();
    r = sd_send_cmd(58, 0, 0x00, 0);
    if (r == 0x00) {
        uint8_t ocr[4];
        read_extra(ocr, 4, 0);
        s_sdhc = (ocr[0] & 0x40) != 0;
    } else {
        s_sdhc = v2;
    }
    cmd_end(8);

    /* SET_BLOCKLEN to 512 */
    cs_low();
    r = sd_send_cmd(16, SD_BLOCK_SIZE, 0x01, 0);
    cmd_end(8);
    if (r != 0x00) {
        ESP_LOGE(TAG, "CMD16 failed: 0x%02X", r);
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = read_csd(&s_sectors, &s_sdhc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read_csd failed: %d", err);
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "card ready, %u sectors (%.1f MB), %s",
             s_sectors, (float)s_sectors / 2.0f / 1024.0f,
             s_sdhc ? "SDHC/SDXC" : "SDSC");
    return ESP_OK;
}

void sd_ll_set_pins(const sd_ll_pins_t *pins)
{
    s_pins = *pins;
    s_ready = false;
}

bool sd_ll_ready(void)
{
    return s_ready;
}

uint32_t sd_ll_get_sector_count(void)
{
    return s_sectors;
}

bool sd_ll_is_sdhc(void)
{
    return s_sdhc;
}

esp_err_t sd_ll_read_block(uint32_t sector, uint8_t *dst)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t addr = s_sdhc ? sector : sector * SD_BLOCK_SIZE;

    cs_low();
    uint8_t r = sd_send_cmd(17, addr, 0x01, 0); /* READ_SINGLE_BLOCK */
    if (r != 0x00) {
        cmd_end(8);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t b;
    int i;
    for (i = 0; i < 32; i++) {
        b = spi_transfer(0xFF, 0);
        if (b != 0xFF) {
            break;
        }
    }
    if (b != 0xFE) {
        cmd_end(8);
        return ESP_ERR_TIMEOUT;
    }

    for (i = 0; i < SD_BLOCK_SIZE; i++) {
        dst[i] = spi_transfer(0xFF, 0);
    }
    spi_transfer(0xFF, 0); /* crc high */
    spi_transfer(0xFF, 0); /* crc low  */
    cmd_end(8);
    return ESP_OK;
}

esp_err_t sd_ll_write_block(uint32_t sector, const uint8_t *src)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t addr = s_sdhc ? sector : sector * SD_BLOCK_SIZE;

    cs_low();
    uint8_t r = sd_send_cmd(24, addr, 0x01, 0); /* WRITE_BLOCK */
    if (r != 0x00) {
        cmd_end(8);
        return ESP_ERR_INVALID_RESPONSE;
    }

    spi_transfer(0xFE, 0); /* data token */
    for (int i = 0; i < SD_BLOCK_SIZE; i++) {
        spi_transfer(src[i], 0);
    }
    spi_transfer(0xFF, 0); /* crc high */
    spi_transfer(0xFF, 0); /* crc low  */

    /* data status byte: bits 0..3 of 0bXXX00101 mean "accepted" */
    uint8_t resp = 0xFF;
    for (int i = 0; i < 32; i++) {
        resp = spi_transfer(0xFF, 0);
        if (resp != 0xFF) {
            break;
        }
    }
    if ((resp & 0x1F) != 0x05) {
        ESP_LOGE(TAG, "write data rejected: 0x%02X", resp);
        cmd_end(8);
        return ESP_FAIL;
    }

    /* wait for the card to stop signalling busy (MISO high) */
    int timeout = 0;
    while (spi_transfer(0xFF, 0) != 0xFF && timeout < 200000) {
        timeout++;
    }
    cmd_end(8);
    if (timeout >= 200000) {
        ESP_LOGE(TAG, "write busy timeout");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}