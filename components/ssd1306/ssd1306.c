#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "ssd1306.h"
#include "font5x7.h"

#define SSD1306_I2C_BUS_DELAY_TICKS (1000 / portTICK_PERIOD_MS)

#define SSD1306_CTRL_DATA  0x40
#define SSD1306_CTRL_CMD   0x00

static const char *TAG = "ssd1306";

static uint8_t s_i2c_addr = 0x3C;
static uint8_t s_framebuf[SSD1306_WIDTH * SSD1306_PAGES];
static bool     s_initialized = false;

static void set_pixel(int x, int y, bool color)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }
    uint8_t *page = &s_framebuf[(y >> 3) * SSD1306_WIDTH + x];
    uint8_t  bit  = (uint8_t)(1u << (y & 7));
    if (color) {
        *page |= bit;
    } else {
        *page &= (uint8_t)~bit;
    }
}

static esp_err_t write_cmd_block(const uint8_t *cmd, size_t len)
{
    uint8_t buf[64];
    if (len > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = SSD1306_CTRL_CMD;
    memcpy(&buf[1], cmd, len);
    return i2c_bus_write_raw(s_i2c_addr, buf, len + 1);
}

static esp_err_t write_cmd(uint8_t cmd)
{
    return write_cmd_block(&cmd, 1);
}

esp_err_t ssd1306_init(uint8_t i2c_addr)
{
    s_i2c_addr = i2c_addr;

    const uint8_t init_seq[] = {
        0xAE,                  /* display off */
        0xD5, 0x80,            /* clock divide ratio / oscillator freq */
        0xA8, (SSD1306_HEIGHT - 1), /* multiplex ratio (panel rows - 1) */
        0xD3, 0x00,            /* display offset */
        0x40,                  /* start line 0 */
        0x8D, 0x14,            /* charge pump on */
        0x20, 0x00,            /* memory addressing mode = horizontal */
        0xA1,                  /* segment remap (column N <-> 0) */
        0xC8,                  /* COM scan direction reversed */
        0xDA, 0x12,            /* COM pins hardware config */
        0x81, 0xCF,            /* contrast */
        0xD9, 0x22,            /* pre-charge period (panel datasheet value) */
        0xDB, 0x00,            /* VCOMH deselect level (panel datasheet value) */
        0x2E,                  /* deactivate scroll */
        0xA4,                  /* resume from RAM content */
        0xA6,                  /* normal (not inverted) display */
        0xAF,                  /* display on */
    };
    esp_err_t ret = write_cmd_block(init_seq, sizeof(init_seq));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    memset(s_framebuf, 0, sizeof(s_framebuf));
    s_initialized = true;
    ESP_LOGI(TAG, "initialised 0x%02X %dx%d", s_i2c_addr, SSD1306_WIDTH, SSD1306_HEIGHT);
    return ssd1306_refresh();
}

void ssd1306_fill(bool color)
{
    memset(s_framebuf, color ? 0xFF : 0x00, sizeof(s_framebuf));
}

void ssd1306_clear(void)
{
    memset(s_framebuf, 0, sizeof(s_framebuf));
}

void ssd1306_draw_pixel(int x, int y, bool color)
{
    if (!s_initialized) {
        return;
    }
    set_pixel(x, y, color);
}

bool ssd1306_get_pixel(int x, int y)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return false;
    }
    return (s_framebuf[(y >> 3) * SSD1306_WIDTH + x] >> (y & 7)) & 1u;
}

void ssd1306_draw_char(int x, int y, char ch, bool color)
{
    if (ch < 0x20 || ch > 0x7E) {
        ch = ' ';
    }
    int i = (ch - 0x20) * FONT5X7_WIDTH;
    for (int col = 0; col < FONT5X7_WIDTH; col++) {
        uint8_t data = font5x7[i + col];
        for (int row = 0; row < FONT5X7_HEIGHT; row++) {
            if (data & (1u << row)) {
                set_pixel(x + col, y + row, color);
            }
        }
    }
}

void ssd1306_draw_rect(int x0, int y0, int x1, int y1, bool color)
{
    if (x1 < x0 || y1 < y0) {
        return;
    }
    for (int x = x0; x <= x1; x++) {
        set_pixel(x, y0, color);
        set_pixel(x, y1, color);
    }
    for (int y = y0 + 1; y < y1; y++) {
        set_pixel(x0, y, color);
        set_pixel(x1, y, color);
    }
}

void ssd1306_draw_string(int x, int y, const char *str, bool color)
{
    int cx = x;
    while (*str) {
        if (cx > SSD1306_WIDTH - FONT5X7_WIDTH) {
            break;
        }
        ssd1306_draw_char(cx, y, *str, color);
        cx += FONT5X7_ADVANCE;
        str++;
    }
}

esp_err_t ssd1306_refresh(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Match Adafruit_SSD1306's 64x48 path: the visible area occupies
     * GDDRAM columns 32..95, and the I2C data is sent in <=32-byte chunks. */
    const uint8_t window[] = {
        0x22, 0x00, 0xFF,                         /* page start/end */
        0x21, SSD1306_COLUMN_OFFSET,
        SSD1306_COLUMN_OFFSET + SSD1306_WIDTH - 1, /* column 32..95 */
    };
    esp_err_t ret = write_cmd_block(window, sizeof(window));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t data[33];
    data[0] = SSD1306_CTRL_DATA;
    for (size_t offset = 0; offset < sizeof(s_framebuf); offset += 32) {
        size_t len = sizeof(s_framebuf) - offset;
        if (len > 32) {
            len = 32;
        }
        memcpy(&data[1], &s_framebuf[offset], len);
        ret = i2c_bus_write_raw(s_i2c_addr, data, len + 1);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "refresh failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    return ESP_OK;
}
