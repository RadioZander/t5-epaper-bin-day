// Drawing into the e-paper image: rectangles and text
#pragma once

#include <stdbool.h>

void gfx_clear(void);
void gfx_pixel(int x, int y, bool black);
void gfx_fill_rect(int x, int y, int w, int h, bool black);
void gfx_rect(int x, int y, int w, int h, bool black); // outline, 1 pixel thick

// Text in the 5x7 font, `scale` times the size: 6 * scale pixels a character
void gfx_text(int x, int y, const char *text, int scale, bool black);
void gfx_text_centered(int y, const char *text, int scale, bool black);
int gfx_text_width(const char *text, int scale);
