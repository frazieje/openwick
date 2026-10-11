/*
 * Battery monitor: samples the divider on AIN4 and reports level and voltage.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BATTERY_H_
#define BATTERY_H_

#include <stdint.h>

typedef void (*battery_cb_t)(uint16_t millivolts, uint8_t percent);

int battery_init(void);

/* 0 when no plausible battery voltage has been measured. */
uint16_t battery_get_millivolts(void);
uint8_t battery_get_percent(void);

/* Called after each measurement. */
void battery_set_callback(battery_cb_t cb);

#endif /* BATTERY_H_ */
