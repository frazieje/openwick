/*
 * Candle engine.
 *
 * A 50 Hz animation thread turns the configuration into seven PWM duty
 * cycles. Flames are modelled as independent noisy levels that drift toward
 * random targets, modulated by a slow "wind" process and occasional gusts;
 * the RGB core follows the same model in its own colour. Outputs are gamma
 * corrected so the low end dims smoothly.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "candle.h"

LOG_MODULE_REGISTER(candle, CONFIG_LOG_DEFAULT_LEVEL);

#define TICK_MS 20
#define TICK_HZ (1000 / TICK_MS)

#define SAVE_DELAY K_SECONDS(2)

#define TWO_PI_F 6.28318530718f

enum { CH_AMBER, CH_YELLOW, CH_RED, CH_CLUSTER, CH_R, CH_G, CH_B, CH_COUNT };
BUILD_ASSERT(CH_COUNT == CANDLE_CHANNELS);

static const struct pwm_dt_spec channels[CH_COUNT] = {
	[CH_AMBER] = PWM_DT_SPEC_GET(DT_NODELABEL(flame0)),
	[CH_YELLOW] = PWM_DT_SPEC_GET(DT_NODELABEL(flame1)),
	[CH_RED] = PWM_DT_SPEC_GET(DT_NODELABEL(flame2)),
	[CH_CLUSTER] = PWM_DT_SPEC_GET(DT_NODELABEL(flame3)),
	[CH_R] = PWM_DT_SPEC_GET(DT_NODELABEL(rgb_r)),
	[CH_G] = PWM_DT_SPEC_GET(DT_NODELABEL(rgb_g)),
	[CH_B] = PWM_DT_SPEC_GET(DT_NODELABEL(rgb_b)),
};

static const char *const mode_names[CANDLE_MODE_COUNT] = {
	[CANDLE_MODE_STEADY] = "steady",
	[CANDLE_MODE_CALM] = "calm",
	[CANDLE_MODE_NORMAL] = "normal",
	[CANDLE_MODE_WINDY] = "windy",
	[CANDLE_MODE_EMBER] = "ember",
	[CANDLE_MODE_STORM] = "storm",
	[CANDLE_MODE_COLOR_CYCLE] = "colorcycle",
	[CANDLE_MODE_BREATHE] = "breathe",
};

/* ---- Configuration -------------------------------------------------------- */

static struct candle_config cfg = {
	.power = 1,
	.brightness = 80,
	.mode = CANDLE_MODE_NORMAL,
	.speed = 50,
	.intensity = 50,
	.rgb = { 255, 96, 8 },
};
static struct k_spinlock cfg_lock;
static candle_change_cb_t change_cb;
static atomic_t lockout;
static uint8_t rgb_balance[3] = { 100, 100, 100 };
static uint8_t levels[CH_COUNT];

static void save_work_handler(struct k_work *work)
{
	struct candle_config snapshot;
	int err;

	candle_get_config(&snapshot);
	err = settings_save_one("candle/cfg", &snapshot, sizeof(snapshot));
	if (err) {
		LOG_WRN("Saving settings failed (err %d)", err);
	} else {
		LOG_DBG("Settings saved");
	}
}
static K_WORK_DELAYABLE_DEFINE(save_work, save_work_handler);

static void changed(uint32_t fields)
{
	if (change_cb) {
		change_cb(fields);
	}
	k_work_reschedule(&save_work, SAVE_DELAY);
}

static bool config_valid(const struct candle_config *c)
{
	return c->power <= 1 && c->brightness <= 100 && c->mode < CANDLE_MODE_COUNT &&
	       c->speed <= 100 && c->intensity <= 100;
}

static int candle_settings_set(const char *name, size_t len, settings_read_cb read_cb,
			       void *cb_arg)
{
	const char *next;

	if (settings_name_steq(name, "cfg", &next) && !next) {
		struct candle_config loaded;
		ssize_t rc;

		if (len != sizeof(loaded)) {
			LOG_WRN("Ignoring saved config of size %u", (unsigned int)len);
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &loaded, sizeof(loaded));
		if (rc < 0) {
			return (int)rc;
		}
		if (!config_valid(&loaded)) {
			LOG_WRN("Ignoring invalid saved config");
			return -EINVAL;
		}
		k_spinlock_key_t key = k_spin_lock(&cfg_lock);

		cfg = loaded;
		k_spin_unlock(&cfg_lock, key);
		return 0;
	}
	return -ENOENT;
}
SETTINGS_STATIC_HANDLER_DEFINE(candle, "candle", NULL, candle_settings_set, NULL, NULL);

void candle_get_config(struct candle_config *out)
{
	k_spinlock_key_t key = k_spin_lock(&cfg_lock);

	*out = cfg;
	k_spin_unlock(&cfg_lock, key);
}

/* Updates one byte of the configuration; returns true if it changed. */
static bool set_u8(uint8_t *field, uint8_t value)
{
	k_spinlock_key_t key = k_spin_lock(&cfg_lock);
	bool differs = *field != value;

	*field = value;
	k_spin_unlock(&cfg_lock, key);
	return differs;
}

int candle_set_power(bool on)
{
	if (set_u8(&cfg.power, on ? 1 : 0)) {
		LOG_INF("Power %s", on ? "on" : "off");
		changed(CANDLE_FIELD_POWER);
	}
	return 0;
}

int candle_set_brightness(uint8_t percent)
{
	if (percent > 100) {
		return -EINVAL;
	}
	if (set_u8(&cfg.brightness, percent)) {
		LOG_INF("Brightness %u %%", percent);
		changed(CANDLE_FIELD_BRIGHTNESS);
	}
	return 0;
}

int candle_set_mode(uint8_t mode)
{
	if (mode >= CANDLE_MODE_COUNT) {
		return -EINVAL;
	}
	if (set_u8(&cfg.mode, mode)) {
		LOG_INF("Mode %s", mode_names[mode]);
		changed(CANDLE_FIELD_MODE);
	}
	return 0;
}

int candle_set_speed(uint8_t speed)
{
	if (speed > 100) {
		return -EINVAL;
	}
	if (set_u8(&cfg.speed, speed)) {
		LOG_INF("Speed %u", speed);
		changed(CANDLE_FIELD_SPEED);
	}
	return 0;
}

int candle_set_intensity(uint8_t intensity)
{
	if (intensity > 100) {
		return -EINVAL;
	}
	if (set_u8(&cfg.intensity, intensity)) {
		LOG_INF("Intensity %u", intensity);
		changed(CANDLE_FIELD_INTENSITY);
	}
	return 0;
}

int candle_set_rgb(const uint8_t rgb[3])
{
	k_spinlock_key_t key = k_spin_lock(&cfg_lock);
	bool differs = memcmp(cfg.rgb, rgb, 3) != 0;

	memcpy(cfg.rgb, rgb, 3);
	k_spin_unlock(&cfg_lock, key);
	if (differs) {
		LOG_INF("Colour %u,%u,%u", rgb[0], rgb[1], rgb[2]);
		changed(CANDLE_FIELD_COLOR);
	}
	return 0;
}

void candle_toggle_power(void)
{
	struct candle_config c;

	candle_get_config(&c);
	candle_set_power(!c.power);
}

void candle_next_mode(void)
{
	struct candle_config c;

	candle_get_config(&c);
	candle_set_mode((c.mode + 1) % CANDLE_MODE_COUNT);
}

void candle_set_rgb_balance(const uint8_t balance[3])
{
	for (int i = 0; i < 3; i++) {
		rgb_balance[i] = MIN(balance[i], 100);
	}
}

void candle_get_rgb_balance(uint8_t balance[3])
{
	memcpy(balance, rgb_balance, 3);
}

void candle_set_change_callback(candle_change_cb_t cb)
{
	change_cb = cb;
}

void candle_set_lockout(bool on)
{
	atomic_set(&lockout, on);
}

bool candle_get_lockout(void)
{
	return atomic_get(&lockout);
}

void candle_get_levels(uint8_t out[CANDLE_CHANNELS])
{
	memcpy(out, levels, CH_COUNT);
}

const char *candle_mode_name(uint8_t mode)
{
	return mode < CANDLE_MODE_COUNT ? mode_names[mode] : "?";
}

int candle_mode_from_name(const char *name)
{
	for (int i = 0; i < CANDLE_MODE_COUNT; i++) {
		if (strcmp(name, mode_names[i]) == 0) {
			return i;
		}
	}
	return -1;
}

/* ---- Random numbers ------------------------------------------------------- */

static uint32_t rng_state = 0x9E3779B9;

static inline uint32_t rng_u32(void)
{
	uint32_t x = rng_state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng_state = x;
	return x;
}

/* Uniform 0..1 */
static inline float rng_f(void)
{
	return (rng_u32() >> 8) * (1.0f / 16777216.0f);
}

/* Triangular -1..1: small values are more likely than large ones. */
static inline float rng_tri(void)
{
	return rng_f() + rng_f() - 1.0f;
}

/* ---- Flame model ---------------------------------------------------------- */

struct flame {
	float level;
	float target;
	int hold;
};

/* How each physical channel behaves relative to the others. */
struct flame_char {
	float base;  /* resting level */
	float depth; /* flicker amplitude multiplier */
	float tempo; /* flicker speed multiplier */
};

static const struct flame_char flame_chars[4] = {
	[CH_AMBER] = { 0.95f, 0.90f, 1.00f },
	[CH_YELLOW] = { 0.80f, 1.20f, 1.30f },
	[CH_RED] = { 0.60f, 0.70f, 0.60f },
	[CH_CLUSTER] = { 0.65f, 0.80f, 0.80f },
};
static const struct flame_char core_char = { 0.85f, 0.60f, 0.70f };

struct mode_params {
	float depth;          /* flicker amplitude at full intensity */
	float tempo;          /* base speed multiplier */
	float gust_rate;      /* gusts per second at full intensity */
	float gust_depth;     /* how far a gust pulls the light down */
	float flame_scale[4]; /* per-flame level multiplier */
	float core_scale;     /* RGB core level multiplier */
};

static const struct mode_params mode_params[CANDLE_MODE_COUNT] = {
	[CANDLE_MODE_STEADY] = { 0.00f, 1.0f, 0.00f, 0.00f, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f },
	[CANDLE_MODE_CALM] = { 0.25f, 0.7f, 0.05f, 0.40f, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f },
	[CANDLE_MODE_NORMAL] = { 0.45f, 1.0f, 0.20f, 0.60f, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f },
	[CANDLE_MODE_WINDY] = { 0.80f, 1.6f, 0.60f, 0.85f, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f },
	[CANDLE_MODE_EMBER] = { 0.50f, 0.35f, 0.03f, 0.50f, { 0.3f, 0.1f, 1.0f, 1.0f }, 0.6f },
	[CANDLE_MODE_STORM] = { 0.50f, 1.2f, 0.30f, 0.70f, { 0.45f, 0.45f, 0.45f, 0.45f }, 0.3f },
	[CANDLE_MODE_COLOR_CYCLE] = { 0.30f, 0.8f, 0.05f, 0.40f, { 0.5f, 0.5f, 0.5f, 0.5f }, 1.0f },
	[CANDLE_MODE_BREATHE] = { 0.00f, 1.0f, 0.00f, 0.00f, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f },
};

struct engine {
	struct flame flames[4];
	struct flame core;
	float wind;
	/* gust envelope */
	int gust_t, gust_dip, gust_total;
	float gust_depth;
	/* lightning */
	int storm_wait, storm_on, storm_gap, storm_left;
	/* cyclic modes */
	float hue, breathe_phase;
	/* smoothed outputs and what was last written to the PWM */
	float out[CH_COUNT];
	uint32_t last_pulse[CH_COUNT];
};

static struct engine eng;

static void flame_step(struct flame *f, const struct flame_char *c, float depth, float tempo,
		       bool gusting)
{
	float t = tempo * c->tempo;

	if (--f->hold <= 0) {
		float dip = fabsf(rng_tri());
		float spark = (rng_f() - 0.5f) * 0.10f;
		float min_hold = 1.5f / t;
		float range = 6.0f / t;

		f->target = c->base * (1.0f - depth * c->depth * dip) + spark;
		f->target = CLAMP(f->target, 0.02f, 1.0f);
		if (gusting) {
			min_hold *= 0.5f;
			range *= 0.4f;
		}
		f->hold = MAX(1, (int)(min_hold + rng_f() * range));
	}

	float alpha = CLAMP(0.10f + 0.20f * t, 0.08f, 0.85f);

	f->level += (f->target - f->level) * alpha;
}

/* Returns the gust envelope, 1.0 when no gust is in progress. */
static float gust_step(const struct mode_params *mp, float intensity)
{
	float env;

	if (eng.gust_total == 0) {
		float rate = mp->gust_rate * (0.3f + 1.4f * intensity);

		if (rng_f() >= rate / TICK_HZ) {
			return 1.0f;
		}
		eng.gust_total = (int)(TICK_HZ * (0.3f + rng_f() * 0.9f));
		eng.gust_dip = eng.gust_total / 3 + 1;
		eng.gust_depth = mp->gust_depth * (0.5f + 0.5f * rng_f()) * (0.4f + 0.6f * intensity);
		eng.gust_t = 0;
	}

	eng.gust_t++;
	if (eng.gust_t <= eng.gust_dip) {
		env = 1.0f - eng.gust_depth * ((float)eng.gust_t / eng.gust_dip);
	} else {
		float r = (float)(eng.gust_t - eng.gust_dip) / (eng.gust_total - eng.gust_dip);

		env = 1.0f - eng.gust_depth * (1.0f - r);
	}
	env += (rng_f() - 0.5f) * 0.1f * eng.gust_depth;
	if (eng.gust_t >= eng.gust_total) {
		eng.gust_total = 0;
	}
	return CLAMP(env, 0.05f, 1.0f);
}

/* Returns true while a lightning flash is lit. */
static bool storm_step(float speed, float intensity)
{
	if (eng.storm_on > 0) {
		if (--eng.storm_on == 0) {
			eng.storm_gap = 2 + rng_u32() % 7;
		}
		return true;
	}
	if (eng.storm_left > 0) {
		if (--eng.storm_gap <= 0) {
			eng.storm_left--;
			eng.storm_on = 1 + rng_u32() % 4;
			return true;
		}
		return false;
	}
	if (--eng.storm_wait <= 0) {
		float mean_s = 9.0f / ((0.5f + 1.5f * intensity) * speed);

		eng.storm_left = 1 + rng_u32() % 4;
		eng.storm_gap = 0;
		eng.storm_wait = (int)(TICK_HZ * mean_s * (0.4f + 1.2f * rng_f()));
	}
	return false;
}

static void hsv_to_rgb(float h, float s, float v, float rgb[3])
{
	float r = v, g = v, b = v;

	if (s > 0.0f) {
		float hh = (h - floorf(h)) * 6.0f;
		int i = (int)hh;
		float f = hh - i;
		float p = v * (1.0f - s);
		float q = v * (1.0f - s * f);
		float t = v * (1.0f - s * (1.0f - f));

		switch (i) {
		case 0: r = v; g = t; b = p; break;
		case 1: r = q; g = v; b = p; break;
		case 2: r = p; g = v; b = t; break;
		case 3: r = p; g = q; b = v; break;
		case 4: r = t; g = p; b = v; break;
		default: r = v; g = p; b = q; break;
		}
	}
	rgb[0] = r;
	rgb[1] = g;
	rgb[2] = b;
}

static void engine_tick(const struct candle_config *c, bool off)
{
	const struct mode_params *mp = &mode_params[c->mode];
	float bright = c->brightness / 100.0f;
	float intensity = c->intensity / 100.0f;
	float speed = 0.4f + 1.6f * (c->speed / 100.0f);
	float tempo = mp->tempo * speed;
	float depth = mp->depth * (0.2f + 1.3f * intensity);
	float colour[3] = { c->rgb[0] / 255.0f, c->rgb[1] / 255.0f, c->rgb[2] / 255.0f };
	float target[CH_COUNT] = { 0 };
	float alpha = 0.5f;

	if (off) {
		alpha = 0.12f;
	} else if (c->mode == CANDLE_MODE_STEADY) {
		for (int i = 0; i < 4; i++) {
			target[i] = bright * flame_chars[i].base;
		}
		for (int k = 0; k < 3; k++) {
			target[CH_R + k] = bright * colour[k];
		}
	} else if (c->mode == CANDLE_MODE_BREATHE) {
		float hz = 0.08f + 0.35f * (c->speed / 100.0f);
		float env;

		eng.breathe_phase += TWO_PI_F * hz / TICK_HZ;
		if (eng.breathe_phase > TWO_PI_F) {
			eng.breathe_phase -= TWO_PI_F;
		}
		env = 0.5f - 0.5f * cosf(eng.breathe_phase);
		env = 0.03f + 0.97f * env * env;
		for (int i = 0; i < 4; i++) {
			target[i] = bright * flame_chars[i].base * env;
		}
		for (int k = 0; k < 3; k++) {
			target[CH_R + k] = bright * colour[k] * env;
		}
	} else {
		float core_colour[3];
		float gust, wdepth, wlevel, core_level;
		bool gusting;

		/* Slow wind: an Ornstein-Uhlenbeck walk around 0.5 */
		eng.wind += (0.5f - eng.wind) * 0.02f + rng_tri() * 0.06f * (0.3f + depth);
		eng.wind = CLAMP(eng.wind, 0.0f, 1.0f);
		wdepth = depth * (0.5f + eng.wind);
		wlevel = 1.0f - 0.15f * depth * (1.0f - eng.wind);

		gust = gust_step(mp, intensity);
		gusting = eng.gust_total != 0;

		for (int i = 0; i < 4; i++) {
			flame_step(&eng.flames[i], &flame_chars[i], wdepth, tempo, gusting);
			target[i] = bright * mp->flame_scale[i] * eng.flames[i].level * wlevel * gust;
		}
		if (c->mode == CANDLE_MODE_EMBER && rng_f() < 0.02f) {
			/* a spark pops out of the embers */
			eng.flames[CH_YELLOW].target = 0.9f;
			eng.flames[CH_YELLOW].hold = 2;
		}

		flame_step(&eng.core, &core_char, wdepth * 0.7f, tempo * 0.8f, gusting);
		core_level = bright * mp->core_scale * eng.core.level * (1.0f - (1.0f - gust) * 0.5f);

		if (c->mode == CANDLE_MODE_COLOR_CYCLE) {
			eng.hue += speed / (TICK_HZ * 30.0f);
			if (eng.hue >= 1.0f) {
				eng.hue -= 1.0f;
			}
			hsv_to_rgb(eng.hue, 1.0f, 1.0f, core_colour);
		} else if (c->mode == CANDLE_MODE_EMBER) {
			static const float ember[3] = { 1.0f, 0.12f, 0.0f };

			for (int k = 0; k < 3; k++) {
				core_colour[k] = colour[k] * 0.3f + ember[k] * 0.7f;
			}
		} else {
			memcpy(core_colour, colour, sizeof(core_colour));
		}
		for (int k = 0; k < 3; k++) {
			target[CH_R + k] = core_colour[k] * core_level;
		}

		if (c->mode == CANDLE_MODE_STORM && storm_step(speed, intensity)) {
			for (int i = 0; i < CH_COUNT; i++) {
				target[i] = 1.0f;
			}
			alpha = 1.0f;
		} else if (c->mode == CANDLE_MODE_STORM) {
			alpha = 0.8f; /* flashes must also fall quickly */
		}
	}

	for (int i = 0; i < CH_COUNT; i++) {
		eng.out[i] += (target[i] - eng.out[i]) * alpha;
	}
}

static void apply_outputs(void)
{
	for (int i = 0; i < CH_COUNT; i++) {
		float v = eng.out[i];
		uint32_t pulse;

		if (i >= CH_R) {
			v *= rgb_balance[i - CH_R] / 100.0f;
		}
		v = CLAMP(v, 0.0f, 1.0f);
		levels[i] = (uint8_t)(v * 255.0f + 0.5f);

		/* gamma 2.2 so steps look even to the eye */
		pulse = (uint32_t)(powf(v, 2.2f) * channels[i].period + 0.5f);
		if (pulse != eng.last_pulse[i]) {
			int err = pwm_set_pulse_dt(&channels[i], pulse);

			if (err) {
				LOG_ERR("pwm_set_pulse_dt(%d) failed: %d", i, err);
			}
			eng.last_pulse[i] = pulse;
		}
	}
}

/* ---- Thread --------------------------------------------------------------- */

static K_SEM_DEFINE(start_sem, 0, 1);

static void candle_thread(void *p1, void *p2, void *p3)
{
	const uint32_t period = k_ms_to_ticks_ceil32(TICK_MS);
	int64_t next;

	k_sem_take(&start_sem, K_FOREVER);
	next = k_uptime_ticks();

	while (1) {
		struct candle_config c;

		next += period;
		k_sleep(K_TIMEOUT_ABS_TICKS(next));

		candle_get_config(&c);
		engine_tick(&c, !c.power || atomic_get(&lockout));
		apply_outputs();
	}
}
K_THREAD_DEFINE(candle_tid, 2048, candle_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0, 0);

int candle_init(void)
{
	int err;

	for (int i = 0; i < CH_COUNT; i++) {
		if (!pwm_is_ready_dt(&channels[i])) {
			LOG_ERR("PWM channel %d not ready", i);
			return -ENODEV;
		}
		eng.last_pulse[i] = UINT32_MAX;
		eng.out[i] = 0.0f;
	}
	apply_outputs();

	rng_state = sys_rand32_get() | 1;
	for (int i = 0; i < 4; i++) {
		eng.flames[i].level = flame_chars[i].base;
		eng.flames[i].target = flame_chars[i].base;
	}
	eng.core.level = core_char.base;
	eng.core.target = core_char.base;
	eng.wind = 0.5f;
	eng.storm_wait = 3 * TICK_HZ;

	err = settings_subsys_init();
	if (err) {
		LOG_WRN("Settings unavailable (err %d), using defaults", err);
	} else {
		err = settings_load_subtree("candle");
		if (err) {
			LOG_WRN("Loading settings failed (err %d)", err);
		}
	}

	LOG_INF("Candle: power %u, %u %%, mode %s, speed %u, intensity %u, colour %u,%u,%u",
		cfg.power, cfg.brightness, mode_names[cfg.mode], cfg.speed, cfg.intensity,
		cfg.rgb[0], cfg.rgb[1], cfg.rgb[2]);

	k_sem_give(&start_sem);
	return 0;
}
