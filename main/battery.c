// The battery voltage, through a divide-by-two resistor divider on GPIO35.
// Not every T5 version has the divider: without it, the reading is near 0.
#include "battery.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#define BATTERY_CHANNEL ADC_CHANNEL_7 // GPIO35
#define SAMPLES         16

int battery_millivolts(void)
{
    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&unit, &adc) != ESP_OK) {
        return 0;
    }
    adc_oneshot_chan_cfg_t chan = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    adc_oneshot_config_channel(adc, BATTERY_CHANNEL, &chan);

    adc_cali_handle_t cali = NULL;
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_create_scheme_line_fitting(&cali_cfg, &cali);

    int total = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw = 0, mv = 0;
        adc_oneshot_read(adc, BATTERY_CHANNEL, &raw);
        if (cali && adc_cali_raw_to_voltage(cali, raw, &mv) == ESP_OK) {
            total += mv;
        } else {
            total += raw * 3300 / 4095;
        }
    }
    if (cali) {
        adc_cali_delete_scheme_line_fitting(cali);
    }
    adc_oneshot_del_unit(adc);

    int mv = total / SAMPLES * 2;
    return mv > 1000 ? mv : 0;
}

// A LiPo cell's resting voltage against its charge, from typical discharge
// curves. Only a guide: the voltage also depends on the load and temperature.
static const struct {
    int mv;
    int percent;
} charge_curve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, {3950, 70},
    {3910, 65},  {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45}, {3800, 40}, {3790, 35},
    {3770, 30},  {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},  {3270, 0},
};

int battery_percent(int mv)
{
    const int n = sizeof(charge_curve) / sizeof(charge_curve[0]);
    if (mv >= charge_curve[0].mv) {
        return 100;
    }
    for (int i = 1; i < n; i++) {
        if (mv >= charge_curve[i].mv) {
            int span = charge_curve[i - 1].mv - charge_curve[i].mv;
            int percent = charge_curve[i].percent +
                          (mv - charge_curve[i].mv) * (charge_curve[i - 1].percent - charge_curve[i].percent) / span;
            return (percent + 2) / 5 * 5; // to the nearest 5%, as it's only a guide
        }
    }
    return 0;
}
