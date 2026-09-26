// Driver for the 2.13" SSD1675B e-paper panel, used in landscape
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EPD_WIDTH  250
#define EPD_HEIGHT 122

// The image, 1 bit per pixel, row by row, EPD_ROW_BYTES bytes per row with
// the leftmost pixel in the top bit. A set bit is black.
#define EPD_ROW_BYTES ((EPD_WIDTH + 7) / 8)
extern uint8_t epd_image[EPD_ROW_BYTES * EPD_HEIGHT];

// Wake the panel (after power-up or epd_sleep) and get it ready to draw
void epd_init(void);

// Show epd_image with a full refresh: the screen flashes black and white
// for about 4 seconds and ends up with no ghosting
void epd_update(void);

// Show epd_image with a partial refresh: only the pixels that changed, with
// no flashing. Faster, but leaves slight ghosting, so every so often use a
// full update.
void epd_update_partial(void);

// Whether epd_image differs from what's on screen (which is remembered
// through deep sleep)
bool epd_image_changed(void);

// Rotate the picture 180 degrees, for standing the board the other way up.
// Applies from the next update, which is then always a full one.
void epd_set_flipped(bool flipped);

// Put the panel into deep sleep. It keeps showing the image.
void epd_sleep(void);
