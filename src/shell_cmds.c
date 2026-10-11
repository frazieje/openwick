/*
 * "candle" shell commands on the USB console, for bench work without a phone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_power.h>

#include "battery.h"
#include "candle.h"
#include "candle_ble.h"

/* Adafruit bootloader: DFU_MAGIC_UF2_RESET */
#define UF2_BOOTLOADER_MAGIC 0x57

static int parse_u8(const struct shell *sh, const char *arg, unsigned int max, uint8_t *out)
{
	char *end;
	unsigned long v = strtoul(arg, &end, 0);

	if (*end != '\0' || v > max) {
		shell_error(sh, "expected a number 0..%u, got \"%s\"", max, arg);
		return -EINVAL;
	}
	*out = (uint8_t)v;
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct candle_config c;
	uint8_t lv[CANDLE_CHANNELS];
	uint8_t bal[3];

	candle_get_config(&c);
	candle_get_levels(lv);
	candle_get_rgb_balance(bal);

	shell_print(sh, "power      %s%s", c.power ? "on" : "off",
		    candle_get_lockout() ? " (locked out: low battery)" : "");
	shell_print(sh, "brightness %u %%", c.brightness);
	shell_print(sh, "mode       %u (%s)", c.mode, candle_mode_name(c.mode));
	shell_print(sh, "speed      %u", c.speed);
	shell_print(sh, "intensity  %u", c.intensity);
	shell_print(sh, "colour     %u,%u,%u  balance %u,%u,%u %%", c.rgb[0], c.rgb[1], c.rgb[2],
		    bal[0], bal[1], bal[2]);
	shell_print(sh, "levels     amber %u yellow %u red %u cluster %u | rgb %u,%u,%u", lv[0],
		    lv[1], lv[2], lv[3], lv[4], lv[5], lv[6]);
	if (battery_get_millivolts()) {
		shell_print(sh, "battery    %u mV (%u %%)", battery_get_millivolts(),
			    battery_get_percent());
	} else {
		shell_print(sh, "battery    not connected");
	}
	shell_print(sh, "bluetooth  %s", candle_ble_is_connected() ? "connected" : "advertising");
	return 0;
}

static int cmd_on(const struct shell *sh, size_t argc, char **argv)
{
	return candle_set_power(true);
}

static int cmd_off(const struct shell *sh, size_t argc, char **argv)
{
	return candle_set_power(false);
}

static int cmd_toggle(const struct shell *sh, size_t argc, char **argv)
{
	candle_toggle_power();
	return 0;
}

static int cmd_bright(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t v;

	return parse_u8(sh, argv[1], 100, &v) ? -EINVAL : candle_set_brightness(v);
}

static int cmd_mode(const struct shell *sh, size_t argc, char **argv)
{
	int mode = candle_mode_from_name(argv[1]);
	uint8_t v;

	if (mode < 0) {
		if (parse_u8(sh, argv[1], CANDLE_MODE_COUNT - 1, &v)) {
			return -EINVAL;
		}
		mode = v;
	}
	return candle_set_mode((uint8_t)mode);
}

static int cmd_speed(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t v;

	return parse_u8(sh, argv[1], 100, &v) ? -EINVAL : candle_set_speed(v);
}

static int cmd_intensity(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t v;

	return parse_u8(sh, argv[1], 100, &v) ? -EINVAL : candle_set_intensity(v);
}

static int cmd_rgb(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t rgb[3];

	for (int i = 0; i < 3; i++) {
		if (parse_u8(sh, argv[1 + i], 255, &rgb[i])) {
			return -EINVAL;
		}
	}
	return candle_set_rgb(rgb);
}

static int cmd_balance(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t bal[3];

	for (int i = 0; i < 3; i++) {
		if (parse_u8(sh, argv[1 + i], 100, &bal[i])) {
			return -EINVAL;
		}
	}
	candle_set_rgb_balance(bal);
	return 0;
}

static int cmd_modes(const struct shell *sh, size_t argc, char **argv)
{
	for (int i = 0; i < CANDLE_MODE_COUNT; i++) {
		shell_print(sh, "%d  %s", i, candle_mode_name(i));
	}
	return 0;
}

static int cmd_dfu(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Rebooting into the UF2 bootloader; the NRF52BOOT drive will appear.");
	k_sleep(K_MSEC(200));
	NRF_POWER->GPREGRET = UF2_BOOTLOADER_MAGIC;
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_candle,
	SHELL_CMD_ARG(status, NULL, "Show settings, output levels and battery", cmd_status, 1, 0),
	SHELL_CMD_ARG(on, NULL, "Light on", cmd_on, 1, 0),
	SHELL_CMD_ARG(off, NULL, "Light off", cmd_off, 1, 0),
	SHELL_CMD_ARG(toggle, NULL, "Toggle the light", cmd_toggle, 1, 0),
	SHELL_CMD_ARG(bright, NULL, "<0-100> brightness %", cmd_bright, 2, 0),
	SHELL_CMD_ARG(mode, NULL, "<0-7|name> flicker mode (see modes)", cmd_mode, 2, 0),
	SHELL_CMD_ARG(speed, NULL, "<0-100> flicker speed", cmd_speed, 2, 0),
	SHELL_CMD_ARG(intensity, NULL, "<0-100> flicker depth", cmd_intensity, 2, 0),
	SHELL_CMD_ARG(rgb, NULL, "<r> <g> <b> core colour, 0-255 each", cmd_rgb, 4, 0),
	SHELL_CMD_ARG(balance, NULL, "<r> <g> <b> RGB white balance %, not saved", cmd_balance, 4, 0),
	SHELL_CMD_ARG(modes, NULL, "List modes", cmd_modes, 1, 0),
	SHELL_CMD_ARG(dfu, NULL, "Reboot into the UF2 bootloader", cmd_dfu, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(candle, &sub_candle, "Pumpkin candle control", NULL);
