#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure and start the I2C master driver on I2C_NUM_0.
 *
 * safe to call more than once: subsequent calls re-apply the pin/clock
 * configuration without re-installing the driver.
 *
 * @param sda_gpio SDA GPIO number
 * @param scl_gpio SCL GPIO number
 * @param clk_stretch_ticks clock-stretch timeout in ticks
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_init(int sda_gpio, int scl_gpio, uint32_t clk_stretch_ticks);

/**
 * @brief Write bytes to a device without a leading register byte.
 *
 * @param dev_addr 7-bit I2C address
 * @param data     bytes to send
 * @param len      number of bytes
 * @return ESP_OK if the device ACKed the address, error otherwise
 */
esp_err_t i2c_bus_write_raw(uint8_t dev_addr, const uint8_t *data, size_t len);

/**
 * @brief Write a device register followed by bytes.
 */
esp_err_t i2c_bus_write_reg(uint8_t dev_addr, uint8_t reg, const uint8_t *data, size_t len);

/**
 * @brief Read a device register followed by bytes.
 */
esp_err_t i2c_bus_read_regs(uint8_t dev_addr, uint8_t reg, uint8_t *out, size_t len);

/**
 * @brief Probe whether a device ACKs its address (test write of one byte).
 */
esp_err_t i2c_bus_probe(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* I2C_BUS_H */