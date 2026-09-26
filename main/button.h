// The board's one button
#pragma once

#include <stdbool.h>

typedef enum {
    BUTTON_NONE,  // no press before the timeout
    BUTTON_SHORT, // pressed and released
    BUTTON_LONG,  // held for BUTTON_LONG_MS
} button_press_t;

#define BUTTON_LONG_MS 800

void button_init(void);

// Wait up to `timeout_ms` for a press and say what kind it was. A long press
// is reported as soon as it's held long enough, without waiting for the
// release; the next call waits for the release first. A press that's
// already under way when this is called counts.
button_press_t button_get(int timeout_ms);

bool button_pressed(void);

// Wait for the button to be let go, e.g. before sleeping, so it doesn't
// wake the board straight back up
void button_wait_release(void);
