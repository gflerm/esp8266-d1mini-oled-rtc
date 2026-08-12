#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "lwip/apps/sntp.h"
#include "sdkconfig.h"
#include "i2c_bus.h"
#include "ssd1306.h"
#include "rtcdev.h"
#include "sd_card.h"

/* Wemos D1 mini pin map */
#define PIN_I2C_SDA      4   /* D2 */
#define PIN_I2C_SCL      5   /* D1 */
#define OLED_I2C_ADDR    0x3C

#define SD_BASE_PATH     "/sdcard"
#define LOG_FILE         SD_BASE_PATH "/log.csv"
#define RUN_EVERY_MS     1000
#define LOG_EVERY_MS     10000

static const char *TAG = "app";

static sd_card_t s_sd;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1

static EventGroupHandle_t s_wifi_events;
static int s_wifi_retries;

static bool set_system_time_from_rtc(void);

/* ------------------------------------------------------------------ */
/* Serial console: SETTIME / TIME / HELP                               */
/* ------------------------------------------------------------------ */

#define CONSOLE_RX_BUF 256
#define CMD_BUF_LEN    80

static char s_cmd_buf[CMD_BUF_LEN];
static int  s_cmd_len = 0;

static bool is_leap_year(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static bool valid_ymd(int y, int m, int d)
{
    if (y < 2000 || y > 2099 || m < 1 || m > 12) {
        return false;
    }
    static const int dim[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int maxd = dim[m - 1];
    if (m == 2 && is_leap_year(y)) {
        maxd = 29;
    }
    return d >= 1 && d <= maxd;
}

/* Sakamoto's algorithm; returns 0 = Sunday .. 6 = Saturday (matches rtc wday). */
static int day_of_week(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    y -= (m < 3);
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void console_handle_line(const char *line)
{
    if (strncasecmp(line, "SETTIME", 7) == 0) {
        int y, mo, d, h, mi, s;
        if (sscanf(line, "SETTIME %d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6 &&
            valid_ymd(y, mo, d) && h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59) {
            rtc_time_t t = {
                .year = y, .month = mo, .day = d,
                .wday = day_of_week(y, mo, d),
                .hour = h, .minute = mi, .second = s,
            };
            esp_err_t ret = rtc_set_time(&t);
            if (ret == ESP_OK) {
                printf("RTC time set to %04d-%02d-%02d %02d:%02d:%02d\n",
                       y, mo, d, h, mi, s);
                set_system_time_from_rtc(); /* resync the OS clock */
            } else {
                printf("RTC set failed: %s\n", esp_err_to_name(ret));
            }
        } else {
            printf("usage: SETTIME yyyy-mm-dd hh:mm:ss\n");
        }
    } else if (strncasecmp(line, "TIME", 4) == 0) {
        rtc_time_t t;
        if (rtc_get_time(&t) == ESP_OK) {
            printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                   t.year, t.month, t.day, t.hour, t.minute, t.second);
        } else {
            printf("no RTC detected\n");
        }
    } else if (strncasecmp(line, "HELP", 4) == 0) {
        printf("commands:\n"
               "  SETTIME yyyy-mm-dd hh:mm:ss\n"
               "  TIME\n"
               "  HELP\n");
    } else {
        printf("unknown command - type HELP\n");
    }
}

static void console_poll(void)
{
    uint8_t byte;
    while (uart_read_bytes(UART_NUM_0, &byte, 1, 0) > 0) {
        if (byte == '\r' || byte == '\n') {
            if (s_cmd_len > 0) {
                s_cmd_buf[s_cmd_len] = '\0';
                console_handle_line(s_cmd_buf);
                s_cmd_len = 0;
            }
        } else if (byte == '\b' || byte == 0x7F) {
            if (s_cmd_len > 0) {
                s_cmd_len--;
            }
        } else if (s_cmd_len < CMD_BUF_LEN - 1) {
            s_cmd_buf[s_cmd_len++] = (char)byte;
        }
    }
}

static void console_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, CONSOLE_RX_BUF, 0, 0, NULL, 0));
    printf("console ready - type HELP or SETTIME yyyy-mm-dd hh:mm:ss\n");
}

static bool set_system_time_from_rtc(void)
{
    rtc_time_t t;
    if (rtc_get_time(&t) != ESP_OK) {
        return false;
    }
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year  = t.year - 1900;
    tmv.tm_mon   = t.month - 1;
    tmv.tm_mday  = t.day;
    tmv.tm_hour  = t.hour;
    tmv.tm_min   = t.minute;
    tmv.tm_sec   = t.second;
    tmv.tm_isdst = -1;
    time_t ts = mktime(&tmv);
    if (ts == (time_t)-1) {
        return false;
    }
    struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    return true;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retries++ < CONFIG_APP_WIFI_MAX_RETRY) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Wi-Fi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retries = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_connect(void)
{
    if (CONFIG_APP_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi SSID is empty; configure it with idf.py menuconfig");
        return false;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    tcpip_adapter_init();
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(ret));
        return false;
    }

    s_wifi_events = xEventGroupCreate();
    s_wifi_retries = 0;

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    wifi_config_t config;
    memset(&config, 0, sizeof(config));
    strncpy((char *)config.sta.ssid, CONFIG_APP_WIFI_SSID, sizeof(config.sta.ssid) - 1);
    strncpy((char *)config.sta.password, CONFIG_APP_WIFI_PASSWORD, sizeof(config.sta.password) - 1);
    if (CONFIG_APP_WIFI_PASSWORD[0] != '\0') {
        config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
                                           WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                           pdFALSE, pdFALSE,
                                           30 * 1000 / portTICK_PERIOD_MS);
    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGW(TAG, "Wi-Fi connection failed");
        return false;
    }
    return true;
}

static void sync_time_from_ntp_task(void *arg)
{
    if (!wifi_connect()) {
        vTaskDelete(NULL);
        return;
    }

    /* South Africa Standard Time: UTC+2, no daylight-saving adjustment. */
    setenv("TZ", "SAST-2", 1);
    tzset();

    ESP_LOGI(TAG, "starting SNTP: pool.ntp.org");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    sntp_init();

    for (int retry = 0; retry < 30; retry++) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            rtc_time_t rtc = { 0 };
            time_t now = time(NULL);
            struct tm local;
            localtime_r(&now, &local);
            rtc.year = local.tm_year + 1900;
            rtc.month = local.tm_mon + 1;
            rtc.day = local.tm_mday;
            rtc.wday = local.tm_wday;
            rtc.hour = local.tm_hour;
            rtc.minute = local.tm_min;
            rtc.second = local.tm_sec;
            if (rtc_set_time(&rtc) == ESP_OK) {
                ESP_LOGI(TAG, "NTP time written to RTC: %04d-%02d-%02d %02d:%02d:%02d",
                         rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
            }
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGW(TAG, "NTP synchronization timed out");
    vTaskDelete(NULL);
}

#if CONFIG_APP_ENABLE_SD
static void log_to_sd(const rtc_time_t *t)
{
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) {
        ESP_LOGW(TAG, "cannot open %s", LOG_FILE);
        return;
    }
    float temp = 0.0f;
    bool has_temp = (rtc_get_temperature(&temp) == ESP_OK);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d,", t->year, t->month, t->day,
            t->hour, t->minute, t->second);
    if (has_temp) {
        fprintf(f, "%.1f\n", (double)temp);
    } else {
        fprintf(f, "\n");
    }
    fclose(f);
    ESP_LOGI(TAG, "logged line at %04d-%02d-%02d %02d:%02d:%02d",
             t->year, t->month, t->day, t->hour, t->minute, t->second);
}
#endif

static void render_status(const rtc_time_t *t)
{
    char line[24];
    ssd1306_clear();

    /* Center the date and time on the 64x48 panel. */
    ssd1306_draw_rect(0, 0, SSD1306_WIDTH - 1, SSD1306_HEIGHT - 1, true);

    snprintf(line, sizeof(line), "%04d-%02d-%02d", t->year, t->month, t->day);
    ssd1306_draw_string(2, 8, line, true);

    snprintf(line, sizeof(line), "%02d:%02d:%02d", t->hour, t->minute, t->second);
    ssd1306_draw_string(8, 24, line, true);

    ssd1306_refresh();
}

#if CONFIG_APP_ENABLE_SD
static void ensure_log_header(void)
{
    FILE *check = fopen(LOG_FILE, "r");
    if (check) {
        fclose(check);
        return;
    }
    FILE *f = fopen(LOG_FILE, "w");
    if (!f) {
        ESP_LOGW(TAG, "cannot create %s", LOG_FILE);
        return;
    }
    fprintf(f, "timestamp,temp_c\n");
    fclose(f);
}
#endif

static void app_task(void *arg)
{
#if CONFIG_APP_ENABLE_SD
    unsigned long last_log_ms = 0;
#endif

    while (1) {
        console_poll();

        rtc_time_t t = { 0 };
        if (rtc_get_time(&t) != ESP_OK) {
            t.year = 0;
        }

#if CONFIG_APP_ENABLE_SD
        unsigned long now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (s_sd.mounted && (now_ms - last_log_ms >= LOG_EVERY_MS)) {
            last_log_ms = now_ms;
            log_to_sd(&t);
        }
#endif

        render_status(&t);
        vTaskDelay(RUN_EVERY_MS / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot");

    console_init();

    ESP_ERROR_CHECK(i2c_bus_init(PIN_I2C_SDA, PIN_I2C_SCL, 300));

    if (ssd1306_init(OLED_I2C_ADDR) != ESP_OK) {
        ESP_LOGW(TAG, "OLED not detected, continuing without display");
    }

    rtc_type_t rtc_type = rtc_detect();
    if (rtc_type != RTC_TYPE_NONE) {
        if (set_system_time_from_rtc()) {
            time_t now = time(NULL);
            struct tm tm_now;
            localtime_r(&now, &tm_now);
            ESP_LOGI(TAG, "system time set from RTC: %04d-%02d-%02d %02d:%02d:%02d",
                     tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                     tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
        }
    }

#if CONFIG_APP_ENABLE_SD
    if (sd_card_mount(&s_sd, SD_BASE_PATH) == ESP_OK) {
        ensure_log_header();
        rtc_time_t t;
        if (rtc_get_time(&t) == ESP_OK) {
            log_to_sd(&t);
        }
    }
#endif

    xTaskCreate(app_task, "app_task", 4096, NULL, 10, NULL);
    xTaskCreate(sync_time_from_ntp_task, "ntp_sync", 4096, NULL, 8, NULL);
}
