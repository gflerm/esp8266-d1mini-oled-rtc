#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "i2c_bus.h"
#include "rtcdev.h"

#define DS_ADDR      0x68
#define PCF_ADDR     0x51

/* DS3231 / DS1307 register map */
#define DS_REG_SEC   0x00
#define DS_REG_MIN   0x01
#define DS_REG_HOUR  0x02
#define DS_REG_WDAY  0x03
#define DS_REG_DATE  0x04
#define DS_REG_MONTH 0x05
#define DS_REG_YEAR  0x06
#define DS_REG_STATUS 0x0F
#define DS_REG_TEMP_MSB 0x11

/* PCF8563 register map */
#define PCF_REG_CTRL1 0x00
#define PCF_REG_SEC   0x02

static const char *TAG = "rtc";

static rtc_type_t s_type = RTC_TYPE_NONE;

static uint8_t bcd2bin(uint8_t v)
{
    return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

static uint8_t bin2bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* ------------------------------------------------------------------ */
/* DS3231 / DS1307 (same time register layout, address 0x68)           */
/* ------------------------------------------------------------------ */

static esp_err_t ds_get_time(rtc_time_t *t)
{
    uint8_t buf[7];
    esp_err_t ret = i2c_bus_read_regs(DS_ADDR, DS_REG_SEC, buf, sizeof(buf));
    if (ret != ESP_OK) {
        return ret;
    }
    t->second = bcd2bin(buf[0] & 0x7F);
    t->minute = bcd2bin(buf[1] & 0x7F);
    t->hour   = bcd2bin(buf[2] & 0x3F);
    t->wday   = (buf[3] & 0x07) - 1;   /* chip: 1=Sunday */
    t->day    = bcd2bin(buf[4] & 0x3F);
    t->month  = bcd2bin(buf[5] & 0x1F);
    int cen   = (buf[5] >> 7) & 1;
    int yr    = bcd2bin(buf[6]);
    t->year   = 2000 + (cen ? 100 : 0) + yr;
    return ESP_OK;
}

static esp_err_t ds_set_time(const rtc_time_t *t)
{
    uint8_t buf[7];
    buf[0] = bin2bcd(t->second) & 0x7F;       /* clear CH: oscillator on */
    buf[1] = bin2bcd(t->minute);
    buf[2] = bin2bcd(t->hour);                /* 24h format */
    buf[3] = (t->wday % 7) + 1;
    buf[4] = bin2bcd(t->day);
    buf[5] = bin2bcd(t->month);
    buf[6] = bin2bcd(t->year % 100);

    esp_err_t ret = i2c_bus_write_reg(DS_ADDR, DS_REG_SEC, buf, sizeof(buf));
    if (ret != ESP_OK) {
        return ret;
    }
    /* clear the oscillator-stop flag so the "time invalid" bit goes away */
    uint8_t status = 0x00;
    return i2c_bus_write_reg(DS_ADDR, DS_REG_STATUS, &status, 1);
}

static esp_err_t ds_get_temp(float *out)
{
    uint8_t msb, lsb;
    esp_err_t ret = i2c_bus_read_regs(DS_ADDR, DS_REG_TEMP_MSB, &msb, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_bus_read_regs(DS_ADDR, DS_REG_TEMP_MSB + 1, &lsb, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    *out = (float)((int8_t)msb) + (float)((lsb >> 6) & 0x03) * 0.25f;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* PCF8563 (address 0x51)                                              */
/* ------------------------------------------------------------------ */

static esp_err_t pcf_get_time(rtc_time_t *t)
{
    uint8_t buf[7];
    esp_err_t ret = i2c_bus_read_regs(PCF_ADDR, PCF_REG_SEC, buf, sizeof(buf));
    if (ret != ESP_OK) {
        return ret;
    }
    t->second = bcd2bin(buf[0] & 0x7F);
    t->minute = bcd2bin(buf[1] & 0x7F);
    t->hour   = bcd2bin(buf[2] & 0x3F);
    t->day    = bcd2bin(buf[3] & 0x3F);
    t->wday   = buf[4] & 0x07;               /* chip: 0=Sunday (same mapping) */
    t->month  = bcd2bin(buf[5] & 0x1F);
    int cen   = (buf[5] >> 7) & 1;
    int yr    = bcd2bin(buf[6]);
    t->year   = 2000 + (cen ? 100 : 0) + yr;
    return ESP_OK;
}

static esp_err_t pcf_set_time(const rtc_time_t *t)
{
    uint8_t buf[8];
    buf[0] = 0x00;                           /* control1: STOP off */
    buf[1] = 0x00;                           /* control2 */
    buf[2] = bin2bcd(t->second) & 0x7F;
    buf[3] = bin2bcd(t->minute);
    buf[4] = bin2bcd(t->hour);
    buf[5] = bin2bcd(t->day);
    buf[6] = t->wday % 7;
    buf[7] = bin2bcd(t->month);
    esp_err_t ret = i2c_bus_write_reg(PCF_ADDR, PCF_REG_CTRL1, buf, 2);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_bus_write_reg(PCF_ADDR, PCF_REG_SEC, &buf[2], 6);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

rtc_type_t rtc_detect(void)
{
    s_type = RTC_TYPE_NONE;

    uint8_t status = 0xFF;
    if (i2c_bus_read_regs(DS_ADDR, DS_REG_STATUS, &status, 1) == ESP_OK) {
        s_type = (status != 0xFF) ? RTC_TYPE_DS3231 : RTC_TYPE_DS1307;
    } else if (i2c_bus_probe(PCF_ADDR) == ESP_OK) {
        s_type = RTC_TYPE_PCF8563;
    }

    if (s_type == RTC_TYPE_NONE) {
        ESP_LOGW(TAG, "no RTC found on the I2C bus");
    } else {
        ESP_LOGI(TAG, "detected %s", rtc_type_str(s_type));
    }
    return s_type;
}

const char *rtc_type_str(rtc_type_t type)
{
    switch (type) {
    case RTC_TYPE_DS3231:  return "DS3231";
    case RTC_TYPE_DS1307:  return "DS1307";
    case RTC_TYPE_PCF8563: return "PCF8563";
    default:               return "none";
    }
}

bool rtc_present(void)
{
    return s_type != RTC_TYPE_NONE;
}

rtc_type_t rtc_detected(void)
{
    return s_type;
}

esp_err_t rtc_get_time(rtc_time_t *t)
{
    if (!rtc_present()) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_type == RTC_TYPE_PCF8563) {
        return pcf_get_time(t);
    }
    return ds_get_time(t);
}

esp_err_t rtc_set_time(const rtc_time_t *t)
{
    if (!rtc_present()) {
        return ESP_ERR_NOT_FOUND;
    }
    if (s_type == RTC_TYPE_PCF8563) {
        return pcf_set_time(t);
    }
    return ds_set_time(t);
}

esp_err_t rtc_get_temperature(float *celsius)
{
    if (s_type != RTC_TYPE_DS3231) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ds_get_temp(celsius);
}