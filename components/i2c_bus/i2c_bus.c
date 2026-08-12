#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "i2c_bus.h"

#define I2C_ACK_CHECK_EN  1
#define I2C_ACK_VAL       0x0
#define I2C_LAST_NACK_VAL 0x2

static i2c_port_t s_port = I2C_NUM_0;

static esp_err_t i2c_bus_write_one(uint8_t dev_addr, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(dev_addr << 1) | I2C_MASTER_WRITE, I2C_ACK_CHECK_EN);
    i2c_master_write(cmd, (uint8_t *)data, len, I2C_ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(s_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_bus_write_two(uint8_t dev_addr, uint8_t reg, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(dev_addr << 1) | I2C_MASTER_WRITE, I2C_ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, I2C_ACK_CHECK_EN);
    if (len) {
        i2c_master_write(cmd, (uint8_t *)data, len, I2C_ACK_CHECK_EN);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(s_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t i2c_bus_init(int sda_gpio, int scl_gpio, uint32_t clk_stretch_ticks)
{
    i2c_config_t conf = {
        .mode            = I2C_MODE_MASTER,
        .sda_io_num      = (gpio_num_t)sda_gpio,
        .sda_pullup_en   = 1,
        .scl_io_num      = (gpio_num_t)scl_gpio,
        .scl_pullup_en   = 1,
        .clk_stretch_tick = clk_stretch_ticks,
    };
    esp_err_t ret = i2c_driver_install(s_port, conf.mode);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE && ret != ESP_FAIL) {
        return ret;
    }
    esp_err_t ret2 = i2c_param_config(s_port, &conf);
    return ret2 != ESP_OK ? ret2 : ESP_OK;
}

esp_err_t i2c_bus_write_raw(uint8_t dev_addr, const uint8_t *data, size_t len)
{
    return i2c_bus_write_one(dev_addr, data, len);
}

esp_err_t i2c_bus_write_reg(uint8_t dev_addr, uint8_t reg, const uint8_t *data, size_t len)
{
    return i2c_bus_write_two(dev_addr, reg, data, len);
}

esp_err_t i2c_bus_read_regs(uint8_t dev_addr, uint8_t reg, uint8_t *out, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(dev_addr << 1) | I2C_MASTER_WRITE, I2C_ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, I2C_ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(s_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (uint8_t)(dev_addr << 1) | I2C_MASTER_READ, I2C_ACK_CHECK_EN);
    if (len > 1) {
        i2c_master_read(cmd, out, len - 1, I2C_ACK_VAL);
    }
    i2c_master_read_byte(cmd, &out[len - 1], I2C_LAST_NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(s_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t i2c_bus_probe(uint8_t dev_addr)
{
    uint8_t data = 0;
    return i2c_bus_write_one(dev_addr, &data, 1);
}