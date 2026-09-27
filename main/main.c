// Bin day reminder for the LilyGO TTGO T5 V2.3 e-paper board.
//
// The board spends nearly all its time in deep sleep, with the e-paper
// holding the picture. It wakes shortly after midnight, when the reminder
// starts the day before a collection, and at least every 12 hours, to
// download the calendar, correct the clock and redraw if anything changed.
// It also wakes when the button is pressed: a short press marks the bins as
// out, and a long press opens the menu.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "battery.h"
#include "bin_calendar.h"
#include "board.h"
#include "button.h"
#include "config.h"
#include "epd.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "nvs_flash.h"
#include "portal.h"
#include "settings.h"
#include "wifi.h"

static const char *TAG = "bin_day";

#define REFRESH_INTERVAL_S   (12 * 60 * 60) // longest sleep between downloads
#define RETRY_INTERVAL_S     (60 * 60)      // after a failed download
#define JOIN_TIMEOUT_MS      (2 * 60 * 1000) // at power-on, before offering setup
#define WAKE_JOIN_TIMEOUT_MS 20000           // on a scheduled wake
#define TIME_SYNC_TIMEOUT_MS 15000
#define MENU_TIMEOUT_MS      15000
#define PORTAL_IDLE_MS       (10 * 60 * 1000) // setup gives up if nobody joins
#define PORTAL_RESTART_MS    3000             // time to read the result before restarting
#define FULL_REFRESH_EVERY   20 // partial updates between full ones, to clear any ghosting
#define LOW_BATTERY_MV       3400
#define PREVIEW_SCENES       4

// Kept through deep sleep
RTC_DATA_ATTR static bool s_time_valid; // the clock has been set since power-on
RTC_DATA_ATTR static bool s_fetch_failed;
RTC_DATA_ATTR static time_t s_last_attempt;
RTC_DATA_ATTR static int s_partial_updates;

static bin_settings_t s_settings;
static device_config_t s_config;
static bin_calendar_t s_calendar;
static bool s_wifi_started;
static bool s_time_synced_now; // this time awake
static int s_battery_mv;       // measured on waking, before WiFi loads the battery

// Bins in black and white: a fill pattern and the name underneath
typedef enum {
    FILL_SOLID,
    FILL_HATCHED,
    FILL_DOTTED,
    FILL_OUTLINE,
} fill_t;

static const struct {
    uint8_t bin;
    const char *label;
    fill_t fill;
    bool caddy; // a small food caddy rather than a wheelie bin
} bin_styles[] = {
    {BIN_REFUSE, "Refuse", FILL_SOLID, false},
    {BIN_RECYCLING, "Recycling", FILL_HATCHED, false},
    {BIN_FOOD, "Food", FILL_OUTLINE, true},
    {BIN_GARDEN, "Garden", FILL_DOTTED, false},
    {BIN_OTHER, "Other", FILL_OUTLINE, false},
};
#define NUM_BIN_STYLES (sizeof(bin_styles) / sizeof(bin_styles[0]))

// Where things stand with the next collection, for a given day
typedef struct {
    const bin_collection_t *next; // NULL if there are none
    int days;                     // until the next collection
    bool reminder;                // time to put the bins out, or collection day
    bool out;                     // the bins have been marked as out
} bin_status_t;

static bin_status_t get_status(const struct tm *today)
{
    bin_status_t st = {.next = bin_calendar_next(&s_calendar, today)};
    if (!st.next) {
        return st;
    }
    st.days = bin_collection_days_until(st.next, today);
    st.reminder = st.days == 0 || (st.days == 1 && today->tm_hour >= s_settings.reminder_from);
    st.out = st.days <= 1 && s_settings.bins_out == bin_collection_date_key(st.next);
    return st;
}

static void today_now(struct tm *today)
{
    time_t now = time(NULL);
    localtime_r(&now, today);
}

// Shows epd_image, if it has changed: partly, which is quick and doesn't
// flash, unless `full` is asked for or it's time for a full one
static void show(bool full)
{
    if (!epd_image_changed()) {
        return;
    }
    if (full || s_partial_updates >= FULL_REFRESH_EVERY) {
        epd_update();
        s_partial_updates = 0;
    } else {
        epd_update_partial();
        s_partial_updates++;
    }
}

// ---- Drawing -------------------------------------------------------------

// "Thu 1 Oct"
static void format_date(char *buf, size_t len, const bin_collection_t *c)
{
    struct tm date = {.tm_year = c->year - 1900, .tm_mon = c->month - 1, .tm_mday = c->day,
                      .tm_hour = 12, .tm_isdst = -1};
    mktime(&date); // fills in the weekday
    char weekday[8], month[8];
    strftime(weekday, sizeof(weekday), "%a", &date);
    strftime(month, sizeof(month), "%b", &date);
    snprintf(buf, len, "%s %d %s", weekday, c->day, month);
}

// "Refuse, Food"
static void format_bins(char *buf, size_t len, uint8_t bins)
{
    buf[0] = '\0';
    for (size_t i = 0; i < NUM_BIN_STYLES; i++) {
        if (bins & bin_styles[i].bin) {
            if (buf[0]) {
                strlcat(buf, ", ", len);
            }
            strlcat(buf, bin_styles[i].label, len);
        }
    }
}

static void fill_area(int x, int y, int w, int h, fill_t fill)
{
    gfx_rect(x, y, w, h, true);
    for (int py = y + 1; py < y + h - 1; py++) {
        for (int px = x + 1; px < x + w - 1; px++) {
            bool black = fill == FILL_SOLID ||
                         (fill == FILL_HATCHED && (px + py) % 4 == 0) ||
                         (fill == FILL_DOTTED && px % 3 == 0 && py % 3 == 0);
            gfx_pixel(px, py, black);
        }
    }
}

// A wheelie bin (or food caddy) `h` pixels tall, centred on `cx`
static void draw_bin(int cx, int top, int h, int style)
{
    if (bin_styles[style].caddy) {
        // Smaller, sitting on the same ground line, and no wheels
        int ch = h * 3 / 5;
        int w = ch;
        top += h - ch;
        gfx_fill_rect(cx - w / 2 - 2, top, w + 4, ch / 5, true);
        fill_area(cx - w / 2, top + ch / 5 + 1, w, ch - ch / 5 - 1, bin_styles[style].fill);
        return;
    }
    int w = h * 2 / 3;
    int lid_h = h / 8;
    int wheel = h / 7;
    gfx_fill_rect(cx - w / 2 - h / 16, top, w + h / 8, lid_h, true);
    fill_area(cx - w / 2, top + lid_h + 1, w, h - lid_h - 1 - wheel / 2, bin_styles[style].fill);
    // Wheels, with a white edge so they show against a black bin
    for (int side = 0; side < 2; side++) {
        int x = side ? cx + w / 2 - wheel : cx - w / 2;
        gfx_fill_rect(x - 1, top + h - wheel - 1, wheel + 2, wheel + 1, false);
        gfx_fill_rect(x, top + h - wheel, wheel, wheel, true);
    }
}

// The charge and a battery with a segment for each quarter, ending at
// `right` in the status line, white on black when low
#define BATTERY_ICON_W 17 // body and terminal
static void draw_battery(int right, int percent, bool low)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    int w = gfx_text_width(buf, 1) + 2 + BATTERY_ICON_W;
    int x = right - w;
    bool ink = !low;
    if (low) {
        gfx_fill_rect(x - 2, 0, w + 4, 9, true);
    }
    gfx_text(x, 1, buf, 1, ink);
    x = right - BATTERY_ICON_W;
    gfx_fill_rect(x, 1, 15, 1, ink); // body, 15x7
    gfx_fill_rect(x, 7, 15, 1, ink);
    gfx_fill_rect(x, 1, 1, 7, ink);
    gfx_fill_rect(x + 14, 1, 1, 7, ink);
    gfx_fill_rect(x + 15, 3, 2, 3, ink); // terminal
    int segments = (percent + 24) / 25; // a quarter part used still shows
    for (int i = 0; i < segments; i++) {
        gfx_fill_rect(x + 2 + i * 3, 3, 2, 3, ink);
    }
}

// The status line along the top: today's date, then the preview label or the
// last update in the middle, and the battery on the right like on a phone
static void draw_status_line(const struct tm *today, const char *preview)
{
    char buf[40];
    strftime(buf, sizeof(buf), "%a %d %b", today);
    gfx_text(2, 1, buf, 1, true);

    if (s_battery_mv) {
        draw_battery(EPD_WIDTH - 2, battery_percent(s_battery_mv), s_battery_mv < LOW_BATTERY_MV);
    }

    if (preview) {
        snprintf(buf, sizeof(buf), " %s ", preview);
        int w = gfx_text_width(buf, 1);
        gfx_fill_rect((EPD_WIDTH - w) / 2, 0, w, 9, true);
        gfx_text((EPD_WIDTH - w) / 2, 1, buf, 1, false);
    } else {
        if (s_fetch_failed) {
            snprintf(buf, sizeof(buf), "Update failed");
        } else if (s_calendar.updated) {
            time_t updated = s_calendar.updated;
            struct tm tm;
            localtime_r(&updated, &tm);
            strftime(buf, sizeof(buf), "Updated %H:%M", &tm);
        } else {
            buf[0] = '\0';
        }
        gfx_text_centered(1, buf, 1, true);
    }
    gfx_fill_rect(0, 10, EPD_WIDTH, 1, true);
}

// The main screen: the next collection and its bins. `preview` labels an
// example screen from the Preview menu item.
static void draw_main(const struct tm *today, const char *preview)
{
    char headline[32], subline[40], buf[64], date[16];
    gfx_clear();
    draw_status_line(today, preview);

    bin_status_t st = get_status(today);
    const bin_collection_t *next = st.next;
    if (!next) {
        gfx_text_centered(30, "No collections", 2, true);
        gfx_text_centered(56, s_calendar.updated ? "None in the calendar" : "Waiting for the calendar", 1, true);
        return;
    }

    format_date(date, sizeof(date), next);
    bool highlight = false; // white on black, when something needs doing
    if (st.out) {
        snprintf(headline, sizeof(headline), "Bins are out");
        if (st.days == 0) {
            snprintf(subline, sizeof(subline), "Collection today");
        } else {
            snprintf(subline, sizeof(subline), "For %s", date);
        }
    } else if (st.days == 0) {
        snprintf(headline, sizeof(headline), "Collection today");
        snprintf(subline, sizeof(subline), "%s", date);
        highlight = true;
    } else if (st.days == 1 && st.reminder) {
        snprintf(headline, sizeof(headline), "Bins out tonight");
        snprintf(subline, sizeof(subline), "For %s", date);
        highlight = true;
    } else if (st.days == 1) {
        snprintf(headline, sizeof(headline), "Tomorrow");
        snprintf(subline, sizeof(subline), "%s", date);
    } else {
        snprintf(headline, sizeof(headline), "%s", date);
        snprintf(subline, sizeof(subline), "In %d days", st.days);
    }
    if (next->changed) {
        strlcat(subline, " (changed)", sizeof(subline));
    }
    if (highlight) {
        gfx_fill_rect(0, 12, EPD_WIDTH, 19, true);
    }
    gfx_text_centered(15, headline, 2, !highlight);
    gfx_text_centered(33, subline, 1, true);

    // One bin per slot across the screen, with its name underneath
    int count = 0;
    for (size_t i = 0; i < NUM_BIN_STYLES; i++) {
        count += (next->bins & bin_styles[i].bin) != 0;
    }
    int slot = EPD_WIDTH / count;
    int x = 0;
    for (size_t i = 0; i < NUM_BIN_STYLES; i++) {
        if (!(next->bins & bin_styles[i].bin)) {
            continue;
        }
        int cx = x + slot / 2;
        draw_bin(cx, 44, 44, i);
        gfx_text(cx - gfx_text_width(bin_styles[i].label, 1) / 2, 91, bin_styles[i].label, 1, true);
        x += slot;
    }

    // Along the bottom: how to mark the bins as out while they need doing,
    // otherwise the collection after this one
    const bin_collection_t *after = next + 1;
    if (st.reminder && !st.out) {
        gfx_text_centered(112, "Press the button when they're out", 1, true);
    } else if (after < s_calendar.collections + s_calendar.count) {
        char bins[40];
        format_date(date, sizeof(date), after);
        format_bins(bins, sizeof(bins), after->bins);
        snprintf(buf, sizeof(buf), "Then %s: %s", date, bins);
        gfx_text_centered(112, buf, 1, true);
    }
}

static void draw_message(const char *title, const char *line1, const char *line2)
{
    gfx_clear();
    gfx_text_centered(20, title, 3, true);
    gfx_text_centered(58, line1, 1, true);
    gfx_text_centered(72, line2, 1, true);
}

// ---- Going online --------------------------------------------------------

static void run_portal(const char *reason) __attribute__((noreturn));

static void start_wifi(void)
{
    if (!s_wifi_started) {
        wifi_init();
        s_wifi_started = true;
    }
}

// Joins the saved network. At power-on, a long press while waiting starts
// setup instead.
static bool connect_wifi(int timeout_ms, bool offer_setup)
{
    start_wifi();
    wifi_join(s_config.ssid, s_config.password, -1);
    TickType_t start = xTaskGetTickCount();
    while (!wifi_connected()) {
        if (xTaskGetTickCount() - start > pdMS_TO_TICKS(timeout_ms)) {
            wifi_stop_joining();
            return false;
        }
        if (offer_setup) {
            if (button_get(250) == BUTTON_LONG) {
                run_portal("Setup");
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
    return true;
}

static void sync_time(void)
{
    if (s_time_synced_now) {
        return;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_BIN_NTP_SERVER);
    esp_netif_sntp_init(&config);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(TIME_SYNC_TIMEOUT_MS)) == ESP_OK) {
        s_time_valid = true;
        s_time_synced_now = true;
        ESP_LOGI(TAG, "Time synchronised from %s", CONFIG_BIN_NTP_SERVER);
    } else {
        ESP_LOGW(TAG, "Time sync failed");
    }
    esp_netif_sntp_deinit();
}

// Joins WiFi, sets the clock and downloads the calendar. Returns false if
// any of that failed.
static bool online_update(bool power_on)
{
    bool ok = false;
    if (connect_wifi(power_on ? JOIN_TIMEOUT_MS : WAKE_JOIN_TIMEOUT_MS, power_on)) {
        sync_time();
        if (s_time_valid && bin_calendar_fetch(s_config.uprn, &s_calendar) == 0) {
            bin_calendar_save(&s_calendar);
            ok = true;
        }
    } else if (power_on) {
        char reason[48];
        snprintf(reason, sizeof(reason), "Can't join %s", s_config.ssid);
        run_portal(reason);
    }
    s_last_attempt = time(NULL);
    s_fetch_failed = !ok;
    return ok;
}

// ---- Setup portal --------------------------------------------------------

static void draw_portal(const char *reason, const char *status, bool can_cancel)
{
    char buf[40];
    gfx_clear();
    gfx_fill_rect(0, 0, EPD_WIDTH, 20, true);
    gfx_text(4, 3, "Setup", 2, false);
    gfx_text(EPD_WIDTH - 4 - gfx_text_width(reason, 1), 7, reason, 1, false);
    if (!portal_ssid()[0]) {
        gfx_text_centered(50, "Starting...", 2, true);
        return;
    }
    gfx_text(4, 25, "On your phone, join the WiFi network", 1, true);
    gfx_text(4, 36, portal_ssid(), 2, true);
    snprintf(buf, sizeof(buf), "Password %s", portal_password());
    gfx_text(4, 56, buf, 2, true);
    gfx_text(4, 77, "The setup page opens by itself,", 1, true);
    gfx_text(4, 86, "or go to http://" PORTAL_ADDRESS, 1, true);
    gfx_text(4, 100, status, 1, true);
    if (can_cancel) {
        gfx_text(4, 112, "Hold the button to cancel", 1, true);
    }
}

// Runs the setup portal until the page saves or cancels, then restarts.
// When there's a setup to go back to, a long press cancels, and it restarts
// (to try the saved network again) if nobody joins for PORTAL_IDLE_MS.
static void run_portal(const char *reason)
{
    ESP_LOGI(TAG, "Setup: %s", reason);
    bool can_cancel = config_complete(&s_config);
    draw_portal(reason, "", can_cancel);
    show(true);
    start_wifi();
    portal_start(&s_config);

    char shown[48];
    strlcpy(shown, portal_status(), sizeof(shown));
    draw_portal(reason, shown, can_cancel);
    show(true);

    TickType_t last_joined = xTaskGetTickCount();
    TickType_t finished_at = 0;
    while (true) {
        TickType_t now = xTaskGetTickCount();
        const char *status = portal_status();
        if (strcmp(status, shown) != 0) {
            strlcpy(shown, status, sizeof(shown));
            draw_portal(reason, shown, can_cancel);
            show(false);
        }
        if (wifi_setup_clients() > 0) {
            last_joined = now;
        }
        if (portal_finished() && !finished_at) {
            finished_at = now;
        }
        if (finished_at && now - finished_at > pdMS_TO_TICKS(PORTAL_RESTART_MS)) {
            esp_restart();
        }
        if (can_cancel && now - last_joined > pdMS_TO_TICKS(PORTAL_IDLE_MS)) {
            ESP_LOGI(TAG, "Nobody joined the setup network, restarting");
            esp_restart();
        }
        if (button_get(500) == BUTTON_LONG && can_cancel) {
            ESP_LOGI(TAG, "Setup cancelled");
            esp_restart();
        }
    }
}

// ---- Sleep ---------------------------------------------------------------

// When to wake next: shortly after midnight (the day count changes), when
// the reminder starts, and at least every REFRESH_INTERVAL_S
static time_t next_wake(time_t now)
{
    if (!s_time_valid) {
        return now + RETRY_INTERVAL_S;
    }
    struct tm today;
    localtime_r(&now, &today);

    struct tm midnight = {.tm_year = today.tm_year, .tm_mon = today.tm_mon, .tm_mday = today.tm_mday + 1,
                          .tm_min = 2, .tm_isdst = -1};
    time_t next = mktime(&midnight);

    bin_status_t st = get_status(&today);
    if (st.next && st.days == 1 && !st.reminder) {
        struct tm reminder = {.tm_year = today.tm_year, .tm_mon = today.tm_mon, .tm_mday = today.tm_mday,
                              .tm_hour = s_settings.reminder_from, .tm_isdst = -1};
        time_t at = mktime(&reminder);
        if (at > now && at < next) {
            next = at;
        }
    }

    time_t refresh = now + (s_fetch_failed ? RETRY_INTERVAL_S : REFRESH_INTERVAL_S);
    return refresh < next ? refresh : next;
}

static void go_to_sleep(void) __attribute__((noreturn));

static void go_to_sleep(void)
{
    time_t now = time(NULL);
    time_t wake = next_wake(now);
    struct tm tm;
    localtime_r(&wake, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%a %d %b %H:%M", &tm);
    ESP_LOGI(TAG, "Sleeping until %s", buf);

    button_wait_release();
    if (s_wifi_started) {
        wifi_stop();
    }
    epd_sleep();
    esp_sleep_enable_timer_wakeup((uint64_t)(wake - now) * 1000000);
    esp_sleep_enable_ext0_wakeup(BOARD_PIN_BUTTON, 0);
    esp_deep_sleep_start();
}

// ---- Menu ----------------------------------------------------------------

// Long press for the next item (closing the menu after the last), short
// press to select. Closes by itself after MENU_TIMEOUT_MS.
typedef enum {
    ITEM_UPDATE,
    ITEM_INFO,
    ITEM_REMINDER,
    ITEM_SCREEN,
    ITEM_PREVIEW,
    ITEM_SETUP,
    NUM_ITEMS,
} menu_item_t;

static const char *item_names[NUM_ITEMS] = {"Update", "Info", "Reminder", "Screen", "Preview", "Setup"};

// Reminder choices: all day, or from an hour in the afternoon or evening
static const int reminder_hours[] = {0, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22};
#define NUM_REMINDER_HOURS (int)(sizeof(reminder_hours) / sizeof(reminder_hours[0]))

static const char *s_update_result;

static void draw_info(void)
{
    char lines[8][44];
    int n = 0;
    struct tm tm;

    int mv = s_battery_mv;
    if (mv) {
        snprintf(lines[n++], sizeof(lines[0]), "Battery %d.%02d V, about %d%%", mv / 1000, mv % 1000 / 10,
                 battery_percent(mv));
    } else {
        snprintf(lines[n++], sizeof(lines[0]), "Battery not measured");
    }
    if (s_calendar.updated) {
        time_t updated = s_calendar.updated;
        localtime_r(&updated, &tm);
        strftime(lines[n++], sizeof(lines[0]), "Updated %a %d %b %H:%M", &tm);
    } else {
        snprintf(lines[n++], sizeof(lines[0]), "Never updated");
    }
    if (s_last_attempt) {
        snprintf(lines[n++], sizeof(lines[0]), "Last update %s", s_fetch_failed ? "failed" : "OK");
    }
    time_t wake = next_wake(time(NULL));
    localtime_r(&wake, &tm);
    strftime(lines[n++], sizeof(lines[0]), "Next update %a %H:%M", &tm);
    snprintf(lines[n++], sizeof(lines[0]), "%d collections saved", s_calendar.count);
    snprintf(lines[n++], sizeof(lines[0]), "WiFi %s", s_config.ssid);
    snprintf(lines[n++], sizeof(lines[0]), "Built %s", esp_app_get_description()->date);
    for (int i = 0; i < n; i++) {
        gfx_text(4, 25 + i * 10, lines[i], 1, true);
    }
}

static void draw_menu(menu_item_t item)
{
    char value[32], buf[16];
    const char *action = "Press to change";
    gfx_clear();
    gfx_fill_rect(0, 0, EPD_WIDTH, 20, true);
    gfx_text(4, 3, item_names[item], 2, false);
    snprintf(buf, sizeof(buf), "%d/%d", item + 1, NUM_ITEMS);
    gfx_text(EPD_WIDTH - 4 - gfx_text_width(buf, 1), 7, buf, 1, false);

    if (item == ITEM_INFO) {
        draw_info();
        gfx_text(4, 112, "Hold: next item", 1, true);
        return;
    }
    switch (item) {
    case ITEM_UPDATE:
        snprintf(value, sizeof(value), "%s", s_update_result ? s_update_result : "Download now");
        action = "Press to update";
        break;
    case ITEM_REMINDER:
        if (s_settings.reminder_from == 0) {
            snprintf(value, sizeof(value), "All day");
        } else {
            snprintf(value, sizeof(value), "From %02d:00", s_settings.reminder_from);
        }
        break;
    case ITEM_SCREEN:
        snprintf(value, sizeof(value), "%s", s_settings.flipped ? "Flipped" : "Normal");
        break;
    case ITEM_PREVIEW:
        snprintf(value, sizeof(value), "Example screens");
        action = "Press to see them";
        break;
    case ITEM_SETUP:
        snprintf(value, sizeof(value), "WiFi and address");
        action = "Press to start setup";
        break;
    default:
        value[0] = '\0';
        break;
    }
    gfx_text_centered(40, value, 2, true);
    gfx_text_centered(64, action, 1, true);
    gfx_text(4, 100, "Hold: next item", 1, true);
    gfx_text(4, 112, "Closes by itself after 15 s", 1, true);
}

// The preview scenes: the evening before, then the day of, each of the
// next two collections
static bool preview_date(int scene, struct tm *date)
{
    struct tm today;
    today_now(&today);
    const bin_collection_t *next = bin_calendar_next(&s_calendar, &today);
    if (!next) {
        return false;
    }
    const bin_collection_t *c = next + scene / 2;
    if (c >= s_calendar.collections + s_calendar.count) {
        c = next;
    }
    *date = (struct tm){.tm_year = c->year - 1900, .tm_mon = c->month - 1,
                        .tm_mday = c->day - (scene % 2 == 0 ? 1 : 0), .tm_hour = 20, .tm_isdst = -1};
    mktime(date); // normalises the day before the 1st
    return true;
}

static void run_menu(void)
{
    ESP_LOGI(TAG, "Menu opened");
    menu_item_t item = ITEM_UPDATE;
    int preview_scene = -1;
    s_update_result = NULL;
    draw_menu(item);
    show(false);

    while (true) {
        button_press_t press = button_get(MENU_TIMEOUT_MS);
        if (press == BUTTON_NONE) {
            break;
        }
        if (press == BUTTON_LONG) {
            item++;
            if (item == NUM_ITEMS) {
                break;
            }
            preview_scene = -1;
            s_update_result = NULL;
            draw_menu(item);
            show(false);
            continue;
        }

        switch (item) {
        case ITEM_UPDATE:
            s_update_result = "Updating...";
            draw_menu(item);
            show(false);
            s_update_result = online_update(false) ? "Done" : "Failed";
            break;
        case ITEM_REMINDER: {
            int i = 0;
            while (i < NUM_REMINDER_HOURS - 1 && reminder_hours[i] != s_settings.reminder_from) {
                i++;
            }
            s_settings.reminder_from = reminder_hours[(i + 1) % NUM_REMINDER_HOURS];
            break;
        }
        case ITEM_SCREEN:
            s_settings.flipped = !s_settings.flipped;
            epd_set_flipped(s_settings.flipped);
            ESP_LOGI(TAG, "Screen %s", s_settings.flipped ? "flipped" : "normal");
            break;
        case ITEM_PREVIEW: {
            struct tm date;
            preview_scene = (preview_scene + 1) % PREVIEW_SCENES;
            if (preview_date(preview_scene, &date)) {
                char tag[16];
                snprintf(tag, sizeof(tag), "PREVIEW %d/%d", preview_scene + 1, PREVIEW_SCENES);
                draw_main(&date, tag);
                show(false);
                continue;
            }
            break;
        }
        case ITEM_SETUP:
            settings_save(&s_settings);
            run_portal("Setup");
        default:
            break;
        }
        draw_menu(item);
        show(false);
    }
    settings_save(&s_settings);
    ESP_LOGI(TAG, "Menu closed");
}

// ---- Waking up -----------------------------------------------------------

static void handle_button_wake(void)
{
    // The press that woke the board may already be over by now
    button_press_t press = button_get(500);
    bool full = false;
    if (press == BUTTON_LONG) {
        run_menu();
        full = true; // clear any ghosting from the menu
    } else {
        struct tm today;
        today_now(&today);
        bin_status_t st = get_status(&today);
        if (st.next && st.days <= 1) {
            // Mark the bins as out, or not out if pressed by mistake
            s_settings.bins_out = st.out ? 0 : bin_collection_date_key(st.next);
            settings_save(&s_settings);
            ESP_LOGI(TAG, "Bins %s", st.out ? "not out" : "out");
        }
    }
    struct tm today;
    today_now(&today);
    draw_main(&today, NULL);
    show(full);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    settings_load(&s_settings);
    config_load(&s_config);
    bin_calendar_load(&s_calendar);
    button_init();
    s_battery_mv = battery_millivolts();
    ESP_LOGI(TAG, "Battery %d mV", s_battery_mv);
    setenv("TZ", CONFIG_BIN_TIMEZONE, 1);
    tzset();
    epd_set_flipped(s_settings.flipped);
    epd_init();

    if (!config_complete(&s_config)) {
        run_portal("First-time setup");
    }

    uint32_t causes = esp_sleep_get_wakeup_causes();
    struct tm today;
    if ((causes & (1 << ESP_SLEEP_WAKEUP_EXT0)) && s_time_valid) {
        ESP_LOGI(TAG, "Woken by the button");
        handle_button_wake();
    } else if ((causes & (1 << ESP_SLEEP_WAKEUP_TIMER)) && s_time_valid) {
        ESP_LOGI(TAG, "Woken for an update");
        online_update(false);
        today_now(&today);
        draw_main(&today, NULL);
        show(true);
    } else {
        // Power-on or reset
        char line[64];
        snprintf(line, sizeof(line), "Connecting to %s...", s_config.ssid);
        draw_message("Bin Day", line, "Hold the button for setup");
        show(true);
        online_update(true);
        today_now(&today);
        draw_main(&today, NULL);
        show(true);
    }
    go_to_sleep();
}
