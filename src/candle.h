/*
 * Candle engine: configuration model and the animation that drives the LEDs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CANDLE_H_
#define CANDLE_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

/* Flame channels 0..3 then the RGB core 4..6, in app.overlay order. */
#define CANDLE_CHANNELS 7

enum candle_mode {
	CANDLE_MODE_STEADY = 0,   /* no flicker, constant light */
	CANDLE_MODE_CALM,         /* gentle flicker, rare gusts */
	CANDLE_MODE_NORMAL,       /* a candle in a still room */
	CANDLE_MODE_WINDY,        /* fast, deep flicker, frequent gusts */
	CANDLE_MODE_EMBER,        /* dying fire: reds, slow pulsing, rare pops */
	CANDLE_MODE_STORM,        /* dim flicker with lightning flashes */
	CANDLE_MODE_COLOR_CYCLE,  /* RGB core cycles through the hues */
	CANDLE_MODE_BREATHE,      /* everything swells and fades together */
	CANDLE_MODE_COUNT
};

/* Everything a client can set. Persisted and exposed over GATT as-is. */
struct candle_config {
	uint8_t power;      /* 0 = off, 1 = on */
	uint8_t brightness; /* 0..100 % */
	uint8_t mode;       /* enum candle_mode */
	uint8_t speed;      /* 0..100, flicker tempo */
	uint8_t intensity;  /* 0..100, flicker depth */
	uint8_t rgb[3];     /* colour of the RGB core */
} __packed;

/* Bit mask of fields reported through the change callback. */
#define CANDLE_FIELD_POWER      BIT(0)
#define CANDLE_FIELD_BRIGHTNESS BIT(1)
#define CANDLE_FIELD_MODE       BIT(2)
#define CANDLE_FIELD_SPEED      BIT(3)
#define CANDLE_FIELD_INTENSITY  BIT(4)
#define CANDLE_FIELD_COLOR      BIT(5)

typedef void (*candle_change_cb_t)(uint32_t changed_fields);

/* Loads the saved configuration and starts the animation thread. */
int candle_init(void);

void candle_get_config(struct candle_config *out);

/* Setters return -EINVAL for out-of-range values. */
int candle_set_power(bool on);
int candle_set_brightness(uint8_t percent);
int candle_set_mode(uint8_t mode);
int candle_set_speed(uint8_t speed);
int candle_set_intensity(uint8_t intensity);
int candle_set_rgb(const uint8_t rgb[3]);
void candle_toggle_power(void);
void candle_next_mode(void);

/* Per-channel white balance for the RGB core, 0..100 %. Not persisted. */
void candle_set_rgb_balance(const uint8_t balance[3]);
void candle_get_rgb_balance(uint8_t balance[3]);

/* Called (from the context of whoever changed a value) after any change. */
void candle_set_change_callback(candle_change_cb_t cb);

/* Low battery: forces the light off until released. */
void candle_set_lockout(bool lockout);
bool candle_get_lockout(void);

/* Current perceptual output per channel, 0..255, for diagnostics. */
void candle_get_levels(uint8_t levels[CANDLE_CHANNELS]);

const char *candle_mode_name(uint8_t mode);
int candle_mode_from_name(const char *name);

#endif /* CANDLE_H_ */
