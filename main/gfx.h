// Drawing into the e-paper image: rectangles and text
#pragma once

#include <stdbool.h>
#include <stdint.h>

void gfx_clear(void);
void gfx_pixel(int x, int y, bool black);
void gfx_fill_rect(int x, int y, int w, int h, bool black);
void gfx_rect(int x, int y, int w, int h, bool black); // outline, 1 pixel thick

// Text in the 5x7 font, `scale` times the size: 6 * scale pixels a character
void gfx_text(int x, int y, const char *text, int scale, bool black);
void gfx_text_centered(int y, const char *text, int scale, bool black);
int gfx_text_width(const char *text, int scale);

// A bitmap font made by tools/make_font.py. Each glyph's pixels are stored
// row by row, 8 to a byte (MSB first), placed against the pen position on
// the baseline.
typedef struct {
    uint16_t offset; // into the bitmap
    uint8_t width, height;
    uint8_t advance; // how far the pen moves on
    int8_t x_offset, y_offset; // top left corner, from the pen
} gfx_glyph_t;

typedef struct {
    const uint8_t *bitmap;
    const gfx_glyph_t *glyphs;
    uint8_t first, last; // characters covered
    uint8_t cap_height;  // pixels
    uint8_t descent;     // pixels below the baseline
} gfx_font_t;

// DejaVu Sans Bold, named by the height of their capitals
extern const gfx_font_t font_bold_12; // headlines, menus
extern const gfx_font_t font_bold_20; // titles

// Text in a bitmap font, with `y` the top of the capitals
void gfx_text_font(int x, int y, const char *text, const gfx_font_t *font, bool black);
void gfx_text_font_centered(int y, const char *text, const gfx_font_t *font, bool black);
int gfx_text_font_width(const char *text, const gfx_font_t *font);
