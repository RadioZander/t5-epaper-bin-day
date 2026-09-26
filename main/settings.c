#include "settings.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";
static const char *NVS_NAMESPACE = "settings";

void settings_load(bin_settings_t *s)
{
    *s = (bin_settings_t) {
        .reminder_from = 0,
        .flipped = false,
        .bins_out = 0,
    };

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return; // nothing saved yet
    }
    uint8_t v;
    if (nvs_get_u8(nvs, "reminder", &v) == ESP_OK && v <= 23) {
        s->reminder_from = v;
    }
    if (nvs_get_u8(nvs, "flipped", &v) == ESP_OK) {
        s->flipped = v;
    }
    uint32_t bins_out;
    if (nvs_get_u32(nvs, "bins_out", &bins_out) == ESP_OK) {
        s->bins_out = bins_out;
    }
    nvs_close(nvs);
}

void settings_save(const bin_settings_t *s)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(nvs, "reminder", s->reminder_from);
    nvs_set_u8(nvs, "flipped", s->flipped);
    nvs_set_u32(nvs, "bins_out", s->bins_out);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
    }
}
