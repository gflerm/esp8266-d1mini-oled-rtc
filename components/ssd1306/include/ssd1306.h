#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ssd1306_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the SSD1306 (128x64) on the shared I2C bus.
 *
 * @param i2c_addr 7-bit I2C address (usually 0x3C)
 * @return ESP_OK on success
 */
esp_err_t ssd1306_init(uint8_t i2c_addr);

/**
 * @brief Fill the whole frame buffer.
 * @param color true = pixels on
 */
void ssd1306_fill(bool color);

/**
 * @brief Clear the whole frame buffer.
 */
void ssd1306_clear(void);

/**
 * @brief Set a single pixel in the frame buffer.
 */
void ssd1306_draw_pixel(int x, int y, bool color);

/**
 * @brief Get a single pixel from the frame buffer.
 */
bool ssd1306_get_pixel(int x, int y);

/**
 * @brief Draw one ASCII character in the built-in 5x7 font.
 *
 * @param x     left edge (0..123)
 * @param y     top edge (0..56)
 * @param ch    character
 * @param color true = pixels on
 */
void ssd1306_draw_char(int x, int y, char ch, bool color);

/**
 * @brief Draw a hollow rectangle outline.
 * @param x0,y0 top-left corner, x1,y1 bottom-right corner (inclusive)
 */
void ssd1306_draw_rect(int x0, int y0, int x1, int y1, bool color);

/**
 * @brief Draw a NUL-terminated ASCII string in the built-in 5x7 font.
 */
void ssd1306_draw_string(int x, int y, const char *str, bool color);

/**
 * @brief Push the frame buffer to the display GDDRAM.
 */
esp_err_t ssd1306_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_H */