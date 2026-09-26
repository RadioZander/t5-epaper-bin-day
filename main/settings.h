#pragma once

#include <stdbool.h>
#include <stdint.h>

// User settings, changed from the on-device menu and kept in NVS
typedef struct {
    int reminder_from; // hour the day before a collection to start the reminder, 0 = all day
    bool flipped;      // screen rotated 180 degrees
    uint32_t bins_out; // date (yyyymmdd) of the collection whose bins are out, 0 = none
} bin_settings_t;

// Load saved settings, falling back to defaults for anything missing
void settings_load(bin_settings_t *s);
void settings_save(const bin_settings_t *s);
