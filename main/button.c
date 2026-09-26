// The button is polled rather than using interrupts: the ESP32 can see
// false interrupts on GPIO39 while WiFi starts up
#include "button.h"
#include "board.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POLL_MS     10
#define DEBOUNCE_MS 30

static bool s_wait_release;

void button_init(void)
{
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << BOARD_PIN_BUTTON, .mode = GPIO_MODE_INPUT};
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

bool button_pressed(void)
{
    return gpio_get_level(BOARD_PIN_BUTTON) == 0;
}

// Waits up to `timeout_ms` for the button to settle in the `pressed` state.
// Returns the time taken, or -1 on timeout.
static int wait_for(bool pressed, int timeout_ms)
{
    int waited = 0;
    int steady = 0;
    while (steady < DEBOUNCE_MS) {
        if (timeout_ms >= 0 && waited >= timeout_ms) {
            return -1;
        }
        steady = button_pressed() == pressed ? steady + POLL_MS : 0;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        waited += POLL_MS;
    }
    return waited;
}

void button_wait_release(void)
{
    wait_for(false, 10000);
    s_wait_release = false;
}

button_press_t button_get(int timeout_ms)
{
    int waited = 0;
    if (s_wait_release) {
        waited = wait_for(false, timeout_ms);
        if (waited < 0) {
            return BUTTON_NONE;
        }
        s_wait_release = false;
    }
    if (wait_for(true, timeout_ms - waited) < 0) {
        return BUTTON_NONE;
    }
    // Pressed: wait for the release, or for it to count as a long press
    int held = DEBOUNCE_MS;
    int released = wait_for(false, BUTTON_LONG_MS - held);
    if (released >= 0) {
        return BUTTON_SHORT;
    }
    s_wait_release = true;
    return BUTTON_LONG;
}
