#ifndef RTC_DEV_H
#define RTC_DEV_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RTC_TYPE_NONE = 0,
    RTC_TYPE_DS3231,   /* I2C 0x68, +temperature sensor */
    RTC_TYPE_DS1307,   /* I2C 0x68, DS3231-compatible time layout */
    RTC_TYPE_PCF8563,  /* I2C 0x51 */
} rtc_type_t;

typedef struct {
    int year;    /* full year, e.g. 2026 */
    int month;   /* 1..12 */
    int day;     /* 1..31 */
    int wday;    /* 0 = Sunday, 6 = Saturday */
    int hour;    /* 0..23 */
    int minute;  /* 0..59 */
    int second;  /* 0..59 */
} rtc_time_t;

/**
 * @brief Probe the I2C bus for a supported RTC.
 *
 * 0x68 is treated as DS3231 (unless its status register reads 0xFF, which
 * indicates a DS1307); 0x51 is a PCF8563. Requires the shared I2C bus to be
 * up (call i2c_bus_init() first).
 */
rtc_type_t rtc_detect(void);

/**
 * @brief Human readable name of the detected chip.
 */
const char *rtc_type_str(rtc_type_t type);

/**
 * @brief Type of the chip detected by the last rtc_detect() call.
 */
rtc_type_t rtc_detected(void);

/**
 * @brief Whether an RTC has been detected.
 */
bool rtc_present(void);

/**
 * @brief Read the current date/time from the RTC.
 */
esp_err_t rtc_get_time(rtc_time_t *t);

/**
 * @brief Write date/time to the RTC and start/keep the oscillator running.
 */
esp_err_t rtc_set_time(const rtc_time_t *t);

/**
 * @brief Read the RTC temperature in degrees Celsius (DS3231 only).
 *
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED otherwise
 */
esp_err_t rtc_get_temperature(float *celsius);

#ifdef __cplusplus
}
#endif

#endif /* RTC_DEV_H */