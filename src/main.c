/*
 * OpenWick: a Bluetooth-controlled flickering pumpkin candle.
 *
 *  - candle.c      animation engine driving seven PWM channels
 *  - candle_ble.c  advertising and the Candle GATT service
 *  - battery.c     LiPo monitoring and low-battery lockout
 *  - shell_cmds.c  "candle" commands on the USB console
 *
 * This file wires them together and handles the button and the status LED:
 * short press toggles the light, a long press steps to the next mode. The
 * blue LED gives one short blink every two seconds while advertising and two
 * blinks while a phone is connected.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "battery.h"
#include "candle.h"
#include "candle_ble.h"

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

#define LED_TICK_MS        50
#define LED_PERIOD_TICKS   40 /* 2 s */
#define BUTTON_DEBOUNCE_MS 30
#define LONG_PRESS_MS      1500

static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static struct gpio_callback button_cb_data;
static int64_t last_edge_ms;
static int64_t press_ms;
static bool long_press_fired;

/* ---- Button --------------------------------------------------------------- */

static void short_press_work_handler(struct k_work *work)
{
	LOG_INF("Button: toggle");
	candle_toggle_power();
}
static K_WORK_DEFINE(short_press_work, short_press_work_handler);

static void long_press_work_handler(struct k_work *work)
{
	long_press_fired = true;
	LOG_INF("Button: next mode");
	candle_next_mode();
}
static K_WORK_DELAYABLE_DEFINE(long_press_work, long_press_work_handler);

static void button_edge(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int64_t now = k_uptime_get();
	bool pressed = gpio_pin_get_dt(&button) > 0;

	if (now - last_edge_ms < BUTTON_DEBOUNCE_MS) {
		return;
	}
	last_edge_ms = now;

	if (pressed) {
		press_ms = now;
		long_press_fired = false;
		k_work_reschedule(&long_press_work, K_MSEC(LONG_PRESS_MS));
	} else {
		k_work_cancel_delayable(&long_press_work);
		if (!long_press_fired && now - press_ms >= BUTTON_DEBOUNCE_MS) {
			k_work_submit(&short_press_work);
		}
	}
}

/* ---- Main ----------------------------------------------------------------- */

int main(void)
{
	uint32_t tick = 0;
	int err;

	LOG_INF("OpenWick starting");

	if (!gpio_is_ready_dt(&status_led) || !gpio_is_ready_dt(&button)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}
	gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	gpio_init_callback(&button_cb_data, button_edge, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	err = candle_init();
	if (err) {
		return err;
	}
	err = battery_init();
	if (err) {
		LOG_WRN("Battery monitor disabled (err %d)", err);
	}
	err = candle_ble_init();
	if (err) {
		return err;
	}

	while (1) {
		unsigned int phase;
		bool on;

		k_sleep(K_MSEC(LED_TICK_MS));
		phase = tick++ % LED_PERIOD_TICKS;
		on = phase == 0 || (phase == 3 && candle_ble_is_connected());
		gpio_pin_set_dt(&status_led, on);
	}
	return 0;
}
