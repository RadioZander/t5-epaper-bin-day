// Draws example screens with the firmware's own drawing code, for the
// README. Builds main.c on a PC, with tools/host standing in for ESP-IDF,
// and writes each screen as a PBM image. tools/screenshots.py builds and
// runs this, and turns the images into PNGs in docs/.
#include <stdlib.h>
#include "../main/main.c"

// ---- Stand-ins for the parts of the firmware that aren't drawing ----------

TickType_t xTaskGetTickCount(void) { return 0; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
const esp_app_desc_t *esp_app_get_description(void)
{
    static const esp_app_desc_t desc = {.date = __DATE__};
    return &desc;
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config) { (void)config; return ESP_OK; }
esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks) { (void)ticks; return ESP_OK; }
void esp_netif_sntp_deinit(void) {}
uint32_t esp_sleep_get_wakeup_causes(void) { return 0; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) { (void)us; return ESP_OK; }
esp_err_t esp_sleep_enable_ext0_wakeup(int pin, int level) { (void)pin; (void)level; return ESP_OK; }
void esp_deep_sleep_start(void) { exit(0); }
void esp_restart(void) { exit(0); }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }

int battery_millivolts(void) { return 0; }
int battery_percent(int mv) { (void)mv; return 80; } // what battery.c gives for 4000 mV
void button_init(void) {}
button_press_t button_get(int timeout_ms) { (void)timeout_ms; return BUTTON_NONE; }
bool button_pressed(void) { return false; }
void button_wait_release(void) {}
void config_load(device_config_t *c) { memset(c, 0, sizeof(*c)); }
bool config_save(const device_config_t *c) { (void)c; return true; }
bool config_complete(const device_config_t *c) { (void)c; return true; }
void settings_load(bin_settings_t *s) { memset(s, 0, sizeof(*s)); }
void settings_save(const bin_settings_t *s) { (void)s; }
int bin_calendar_fetch(const char *uprn, bin_calendar_t *cal) { (void)uprn; (void)cal; return -1; }
void bin_calendar_save(const bin_calendar_t *cal) { (void)cal; }
bool bin_calendar_load(bin_calendar_t *cal) { (void)cal; return false; }

uint8_t epd_image[EPD_ROW_BYTES * EPD_HEIGHT];
void epd_init(void) {}
void epd_update(void) {}
void epd_update_partial(void) {}
bool epd_image_changed(void) { return false; }
void epd_set_flipped(bool flipped) { (void)flipped; }
void epd_sleep(void) {}

void wifi_init(void) {}
void wifi_join(const char *ssid, const char *password, int retries) { (void)ssid; (void)password; (void)retries; }
void wifi_stop_joining(void) {}
void wifi_stop(void) {}
bool wifi_connected(void) { return false; }
int wifi_setup_clients(void) { return 0; }
void portal_start(const device_config_t *current) { (void)current; }
const char *portal_ssid(void) { return ""; }
const char *portal_password(void) { return ""; }
const char *portal_status(void) { return ""; }
bool portal_finished(void) { return false; }

// ---- The screens ----------------------------------------------------------

static const char *s_out_dir;

// epd_image has a set bit for black, as PBM does
static void save(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.pbm", s_out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P4\n%d %d\n", EPD_WIDTH, EPD_HEIGHT);
    fwrite(epd_image, 1, sizeof(epd_image), f);
    fclose(f);
    printf("%s\n", path);
}

static struct tm at(int month, int day, int hour)
{
    struct tm tm = {.tm_year = 2026 - 1900, .tm_mon = month - 1, .tm_mday = day, .tm_hour = hour, .tm_isdst = -1};
    mktime(&tm);
    return tm;
}

static void add(int month, int day, uint8_t bins)
{
    s_calendar.collections[s_calendar.count++] =
        (bin_collection_t){.year = 2026, .month = month, .day = day, .bins = bins};
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_DIR\n", argv[0]);
        return 1;
    }
    s_out_dir = argv[1];
    setenv("TZ", CONFIG_BIN_TIMEZONE, 1);
    tzset();

    // A made-up calendar, alternating weeks as in Horsham
    add(9, 29, BIN_REFUSE | BIN_FOOD);
    add(10, 6, BIN_RECYCLING | BIN_FOOD | BIN_GARDEN);
    add(10, 13, BIN_REFUSE | BIN_FOOD);
    add(10, 20, BIN_RECYCLING | BIN_FOOD | BIN_GARDEN);
    s_battery_mv = 4000;
    s_settings.reminder_from = 0;

    struct tm updated = at(9, 27, 15);
    s_calendar.updated = mktime(&updated);
    struct tm today = at(9, 27, 16);
    draw_main(&today, NULL);
    save("main");

    updated = at(10, 5, 15);
    s_calendar.updated = mktime(&updated);
    today = at(10, 5, 19);
    draw_main(&today, NULL);
    save("bins_out_tonight");

    s_settings.bins_out = bin_collection_date_key(&s_calendar.collections[1]);
    draw_main(&today, NULL);
    save("bins_are_out");
    s_settings.bins_out = 0;

    updated = at(10, 6, 5);
    s_calendar.updated = mktime(&updated);
    today = at(10, 6, 7);
    draw_main(&today, NULL);
    save("collection_today");

    draw_menu(ITEM_UPDATE);
    save("menu");
    return 0;
}
