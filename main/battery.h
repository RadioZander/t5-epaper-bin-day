#pragma once

// The battery voltage in millivolts, or 0 if it can't be measured
int battery_millivolts(void);

// Roughly how full a LiPo battery at `mv` millivolts is, to the nearest 5%
int battery_percent(int mv);
