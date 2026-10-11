/*
 * Battery monitor.
 *
 * Reads the 1 M / 1 M divider every ten seconds, converts to a LiPo state of
 * charge, and locks the candle out below the cutoff voltage so a cell is never
 * run flat. An unwired divider reads near zero and is reported as unknown.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include "battery.h"
#include "candle.h"

LOG_MODULE_REGISTER(battery, CONFIG_LOG_DEFAULT_LEVEL);

#define SAMPLE_PERIOD   K_SECONDS(10)
#define FIRST_SAMPLE    K_SECONDS(2)
#define SAMPLES_PER_READ 4

#define PLAUSIBLE_MIN_MV 2500 /* below this the divider is not connected */
#define CUTOFF_MV        3300 /* lock the light out */
#define RELEASE_MV       3550 /* ...until the cell recovers or is charged */
#define CUTOFF_READINGS  3

static const struct adc_dt_spec vbatt = ADC_DT_SPEC_GET(DT_NODELABEL(vbatt));
static const uint32_t full_ohms = DT_PROP(DT_NODELABEL(vbatt), full_ohms);
static const uint32_t output_ohms = DT_PROP(DT_NODELABEL(vbatt), output_ohms);

static uint16_t last_mv;
static uint8_t last_percent;
static battery_cb_t callback;
static int low_readings;
static bool warned_unwired;

static const struct {
	uint16_t mv;
	uint8_t percent;
} curve[] = {
	{ 4200, 100 }, { 4100, 92 }, { 4000, 82 }, { 3900, 68 }, { 3800, 50 }, { 3750, 40 },
	{ 3700, 30 }, { 3650, 20 }, { 3600, 12 }, { 3500, 5 }, { 3400, 2 }, { 3300, 0 },
};

static uint8_t mv_to_percent(uint16_t mv)
{
	if (mv >= curve[0].mv) {
		return 100;
	}
	for (size_t i = 1; i < ARRAY_SIZE(curve); i++) {
		if (mv >= curve[i].mv) {
			uint16_t span = curve[i - 1].mv - curve[i].mv;
			uint16_t pct_span = curve[i - 1].percent - curve[i].percent;

			return curve[i].percent + (mv - curve[i].mv) * pct_span / span;
		}
	}
	return 0;
}

static int read_millivolts(int32_t *out)
{
	int16_t raw;
	struct adc_sequence seq = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int32_t sum = 0;
	int err;

	adc_sequence_init_dt(&vbatt, &seq);
	for (int i = 0; i < SAMPLES_PER_READ; i++) {
		int32_t mv;

		err = adc_read_dt(&vbatt, &seq);
		if (err) {
			return err;
		}
		mv = MAX(raw, 0);
		err = adc_raw_to_millivolts_dt(&vbatt, &mv);
		if (err) {
			return err;
		}
		sum += mv;
	}
	*out = (sum / SAMPLES_PER_READ) * (int32_t)full_ohms / (int32_t)output_ohms;
	return 0;
}

static void sample_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	int32_t mv;
	int err;

	err = read_millivolts(&mv);
	if (err) {
		LOG_WRN("ADC read failed (err %d)", err);
	} else if (mv < PLAUSIBLE_MIN_MV) {
		if (!warned_unwired) {
			LOG_INF("Battery divider reads %d mV: treating as not connected", mv);
			warned_unwired = true;
		}
		last_mv = 0;
	} else {
		warned_unwired = false;
		last_mv = (uint16_t)mv;
		last_percent = mv_to_percent(last_mv);
		LOG_DBG("Battery %u mV (%u %%)", last_mv, last_percent);

		if (last_mv < CUTOFF_MV) {
			if (++low_readings >= CUTOFF_READINGS && !candle_get_lockout()) {
				LOG_WRN("Battery low (%u mV): light off", last_mv);
				candle_set_lockout(true);
			}
		} else {
			low_readings = 0;
			if (last_mv > RELEASE_MV && candle_get_lockout()) {
				LOG_INF("Battery recovered (%u mV): light released", last_mv);
				candle_set_lockout(false);
			}
		}
		if (callback) {
			callback(last_mv, last_percent);
		}
	}

	k_work_reschedule(dwork, SAMPLE_PERIOD);
}
static K_WORK_DELAYABLE_DEFINE(sample_work, sample_work_handler);

uint16_t battery_get_millivolts(void)
{
	return last_mv;
}

uint8_t battery_get_percent(void)
{
	return last_mv ? last_percent : 0;
}

void battery_set_callback(battery_cb_t cb)
{
	callback = cb;
}

int battery_init(void)
{
	int err;

	if (!adc_is_ready_dt(&vbatt)) {
		LOG_ERR("ADC not ready");
		return -ENODEV;
	}
	err = adc_channel_setup_dt(&vbatt);
	if (err) {
		LOG_ERR("ADC channel setup failed (err %d)", err);
		return err;
	}
	k_work_schedule(&sample_work, FIRST_SAMPLE);
	return 0;
}
