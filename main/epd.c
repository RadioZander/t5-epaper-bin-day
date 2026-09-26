// Driver for the 2.13" SSD1675B e-paper panel on the TTGO T5 V2.3, written
// from the SSD1675B datasheet's command set
#include <string.h>
#include "epd.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd";

// The controller's memory is 128 pixels across the panel's short side (of
// which 122 are visible) by 250 rows along its long side
#define RAM_COLUMNS   128
#define RAM_ROW_BYTES (RAM_COLUMNS / 8)
#define RAM_ROWS      250

// How the landscape image lands in the panel's memory: the first visible
// column, and whether each direction runs backwards. Found by checking the
// test screen on the board: with USB at the bottom left, rows run backwards.
#define VISIBLE_COLUMN_OFFSET 0
#define REVERSE_ROWS          true
#define REVERSE_COLUMNS       false

#define BUSY_TIMEOUT_MS 10000

uint8_t epd_image[EPD_ROW_BYTES * EPD_HEIGHT];

// What's on screen, kept through deep sleep: a partial update needs it, and
// there's no need to redraw a screen that hasn't changed
RTC_DATA_ATTR static uint8_t s_shown[sizeof(epd_image)];
RTC_DATA_ATTR static bool s_shown_valid;
RTC_DATA_ATTR static bool s_shown_flipped;

static spi_device_handle_t s_spi;
static bool s_flipped;
static bool s_previous_loaded; // the panel has the image on screen, for a partial update

static void send(bool data, const uint8_t *bytes, size_t len)
{
    if (len == 0) {
        return;
    }
    gpio_set_level(BOARD_PIN_EPD_DC, data);
    spi_transaction_t t = {.length = len * 8, .tx_buffer = bytes};
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void command(uint8_t cmd, const uint8_t *data, size_t len)
{
    send(false, &cmd, 1);
    send(true, data, len);
}

#define COMMAND(cmd, ...) command(cmd, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static void wait_while_busy(void)
{
    int waited = 0;
    while (gpio_get_level(BOARD_PIN_EPD_BUSY) && waited < BUSY_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    if (waited >= BUSY_TIMEOUT_MS) {
        ESP_LOGW(TAG, "Panel still busy after %d ms", BUSY_TIMEOUT_MS);
    }
}

static void bus_init(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;

    gpio_config_t out = {
        .pin_bit_mask = (1ULL << BOARD_PIN_EPD_DC) | (1ULL << BOARD_PIN_EPD_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_config_t in = {.pin_bit_mask = 1ULL << BOARD_PIN_EPD_BUSY, .mode = GPIO_MODE_INPUT};
    ESP_ERROR_CHECK(gpio_config(&in));

    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_PIN_EPD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = BOARD_PIN_EPD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = RAM_ROW_BYTES * RAM_ROWS,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 4 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = BOARD_PIN_EPD_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev, &s_spi));
}

// Whether the pixel at a place in the panel's memory is black in `image`
static bool ram_pixel(const uint8_t *image, int column, int row)
{
    bool reverse_rows = REVERSE_ROWS != s_flipped;
    bool reverse_columns = REVERSE_COLUMNS != s_flipped;
    int x = reverse_rows ? RAM_ROWS - 1 - row : row;
    int y = column - VISIBLE_COLUMN_OFFSET;
    if (y < 0 || y >= EPD_HEIGHT) {
        return false;
    }
    if (reverse_columns) {
        y = EPD_HEIGHT - 1 - y;
    }
    return image[y * EPD_ROW_BYTES + x / 8] & (0x80 >> (x % 8));
}

static uint8_t ram[RAM_ROW_BYTES * RAM_ROWS];

// Converts an image into the panel's memory layout, which is written row by
// row along its long side, with a set bit meaning white
static void image_to_ram(const uint8_t *image)
{
    for (int row = 0; row < RAM_ROWS; row++) {
        for (int b = 0; b < RAM_ROW_BYTES; b++) {
            uint8_t byte = 0xFF;
            for (int bit = 0; bit < 8; bit++) {
                if (ram_pixel(image, b * 8 + bit, row)) {
                    byte &= ~(0x80 >> bit);
                }
            }
            ram[row * RAM_ROW_BYTES + b] = byte;
        }
    }
}

// Writes the image to one of the panel's two memories: 0x24 is the new
// image, and 0x26 the one on screen, which a partial update compares against
static void write_ram(uint8_t which)
{
    COMMAND(0x4E, 0x00);       // start at column 0
    COMMAND(0x4F, 0x00, 0x00); // and row 0
    command(which, ram, sizeof(ram));
}

static void refresh(uint8_t mode)
{
    COMMAND(0x22, mode);
    command(0x20, NULL, 0);
    wait_while_busy();
    // What's on screen now becomes the image to compare against next time
    write_ram(0x26);
    s_previous_loaded = true;
    memcpy(s_shown, epd_image, sizeof(s_shown));
    s_shown_valid = true;
    s_shown_flipped = s_flipped;
}

void epd_init(void)
{
    bus_init();

    // Hardware reset, which also wakes the panel from deep sleep
    gpio_set_level(BOARD_PIN_EPD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_PIN_EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_while_busy();

    command(0x12, NULL, 0); // software reset
    wait_while_busy();

    COMMAND(0x01, RAM_ROWS - 1, 0x00, 0x00); // driver output: 250 gate lines
    COMMAND(0x11, 0x03);                     // data entry: columns then rows, both increasing
    COMMAND(0x44, 0x00, RAM_ROW_BYTES - 1);  // RAM columns 0-127, in bytes
    COMMAND(0x45, 0x00, 0x00, RAM_ROWS - 1, 0x00); // RAM rows 0-249
    COMMAND(0x3C, 0x05);                     // border (the strip round the pixels) white
    COMMAND(0x18, 0x80);                     // use the built-in temperature sensor

    // Give the panel what's on screen to compare against, for partial updates
    s_previous_loaded = false;
    if (s_shown_valid && s_shown_flipped == s_flipped) {
        image_to_ram(s_shown);
        write_ram(0x26);
        s_previous_loaded = true;
    }
}

void epd_update(void)
{
    ESP_LOGI(TAG, "Full update%s", s_flipped ? ", flipped" : "");
    image_to_ram(epd_image);
    write_ram(0x24);
    refresh(0xF7); // full update, using the panel's own waveform
}

void epd_update_partial(void)
{
    // Without the image that's on screen, only a full update gets it right
    if (!s_previous_loaded) {
        epd_update();
        return;
    }
    ESP_LOGI(TAG, "Partial update%s", s_flipped ? ", flipped" : "");
    image_to_ram(epd_image);
    write_ram(0x24);
    refresh(0xFF); // "display mode 2", the controller's partial waveform
}

bool epd_image_changed(void)
{
    return !s_shown_valid || s_shown_flipped != s_flipped || memcmp(s_shown, epd_image, sizeof(s_shown)) != 0;
}

void epd_set_flipped(bool flipped)
{
    if (flipped != s_flipped) {
        // The image the panel has for comparing against is now the wrong way up
        s_previous_loaded = false;
    }
    s_flipped = flipped;
}

void epd_sleep(void)
{
    COMMAND(0x10, 0x01);
}
