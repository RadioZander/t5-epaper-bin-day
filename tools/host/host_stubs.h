// Just enough of ESP-IDF for main.c's drawing code to compile on a PC, for
// tools/screenshots.c. Nothing here runs: the screenshots only call the
// drawing functions.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef int esp_err_t;
#define ESP_OK                         0
#define ESP_ERR_NVS_NO_FREE_PAGES      1
#define ESP_ERR_NVS_NEW_VERSION_FOUND  2
#define ESP_ERROR_CHECK(x)             (void)(x)

#define RTC_DATA_ATTR

#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))

typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);

typedef struct {
    const char *date;
} esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);

typedef struct {
    const char *server;
} esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(s) {.server = (s)}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks);
void esp_netif_sntp_deinit(void);

#define ESP_SLEEP_WAKEUP_EXT0  2
#define ESP_SLEEP_WAKEUP_TIMER 4
uint32_t esp_sleep_get_wakeup_causes(void);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us);
esp_err_t esp_sleep_enable_ext0_wakeup(int pin, int level);
void esp_deep_sleep_start(void) __attribute__((noreturn));
void esp_restart(void) __attribute__((noreturn));

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);

#define CONFIG_BIN_NTP_SERVER "pool.ntp.org"
#define CONFIG_BIN_TIMEZONE   "GMT0BST,M3.5.0/1,M10.5.0"

typedef struct {
    uint8_t ssid[33];
    int8_t rssi;
} wifi_ap_record_t;

// ESP-IDF's C library has these; older glibc doesn't
#include <string.h>
static inline size_t host_strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}
static inline size_t host_strlcat(char *dst, const char *src, size_t size)
{
    size_t used = strnlen(dst, size);
    return used + host_strlcpy(dst + used, src, size - used);
}
#define strlcpy host_strlcpy
#define strlcat host_strlcat
