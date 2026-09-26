// LilyGO TTGO T5 V2.3 with a 2.13" black and white e-paper panel
// (HINK-E0213A22 on the ribbon cable, SSD1675B controller)
#pragma once

// E-paper, on SPI
#define BOARD_PIN_EPD_MOSI 23
#define BOARD_PIN_EPD_SCLK 18
#define BOARD_PIN_EPD_CS   5
#define BOARD_PIN_EPD_DC   17
#define BOARD_PIN_EPD_RST  16
#define BOARD_PIN_EPD_BUSY 4 // high while the panel is busy

// The one user button, which reads low when pressed. GPIO39 is input-only,
// with a pull-up on the board.
#define BOARD_PIN_BUTTON 39
