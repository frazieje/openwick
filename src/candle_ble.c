/*
 * Candle GATT service.
 *
 * One characteristic per setting, each readable, writable and notifiable, with
 * a user description so a generic tool such as nRF Connect shows what it is.
 * Battery level uses the standard Battery Service; a vendor characteristic
 * adds the raw millivolts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/services/bas.h>

#include "battery.h"
#include "candle.h"
#include "candle_ble.h"

LOG_MODULE_REGISTER(candle_ble, CONFIG_LOG_DEFAULT_LEVEL);

/* a3c1XXXX-7f6e-4b1a-9c2d-5e8f0a1b2c3d */
#define CANDLE_UUID_VAL(n) BT_UUID_128_ENCODE(0xa3c10000 + (n), 0x7f6e, 0x4b1a, 0x9c2d, 0x5e8f0a1b2c3d)

static const struct bt_uuid_128 uuid_svc = BT_UUID_INIT_128(CANDLE_UUID_VAL(1));
static const struct bt_uuid_128 uuid_power = BT_UUID_INIT_128(CANDLE_UUID_VAL(2));
static const struct bt_uuid_128 uuid_brightness = BT_UUID_INIT_128(CANDLE_UUID_VAL(3));
static const struct bt_uuid_128 uuid_mode = BT_UUID_INIT_128(CANDLE_UUID_VAL(4));
static const struct bt_uuid_128 uuid_speed = BT_UUID_INIT_128(CANDLE_UUID_VAL(5));
static const struct bt_uuid_128 uuid_intensity = BT_UUID_INIT_128(CANDLE_UUID_VAL(6));
static const struct bt_uuid_128 uuid_color = BT_UUID_INIT_128(CANDLE_UUID_VAL(7));
static const struct bt_uuid_128 uuid_batt_mv = BT_UUID_INIT_128(CANDLE_UUID_VAL(8));

static atomic_t connected;
static atomic_t pending_fields;

/* ---- Characteristic access ------------------------------------------------ */

static uint8_t field_value(const struct candle_config *c, uint32_t field)
{
	switch (field) {
	case CANDLE_FIELD_POWER:
		return c->power;
	case CANDLE_FIELD_BRIGHTNESS:
		return c->brightness;
	case CANDLE_FIELD_MODE:
		return c->mode;
	case CANDLE_FIELD_SPEED:
		return c->speed;
	case CANDLE_FIELD_INTENSITY:
		return c->intensity;
	default:
		return 0;
	}
}

static int field_set(uint32_t field, uint8_t value)
{
	switch (field) {
	case CANDLE_FIELD_POWER:
		return value <= 1 ? candle_set_power(value) : -EINVAL;
	case CANDLE_FIELD_BRIGHTNESS:
		return candle_set_brightness(value);
	case CANDLE_FIELD_MODE:
		return candle_set_mode(value);
	case CANDLE_FIELD_SPEED:
		return candle_set_speed(value);
	case CANDLE_FIELD_INTENSITY:
		return candle_set_intensity(value);
	default:
		return -EINVAL;
	}
}

static ssize_t read_u8(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
		       uint16_t len, uint16_t offset)
{
	struct candle_config c;
	uint8_t value;

	candle_get_config(&c);
	value = field_value(&c, (uint32_t)(uintptr_t)attr->user_data);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

static ssize_t write_u8(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (field_set((uint32_t)(uintptr_t)attr->user_data, *(const uint8_t *)buf)) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	return len;
}

static ssize_t read_color(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	struct candle_config c;

	candle_get_config(&c);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, c.rgb, sizeof(c.rgb));
}

static ssize_t write_color(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != 3) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	candle_set_rgb(buf);
	return len;
}

static ssize_t read_batt_mv(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			    uint16_t len, uint16_t offset)
{
	uint16_t mv = sys_cpu_to_le16(battery_get_millivolts());

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &mv, sizeof(mv));
}

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(value);
}

#define CANDLE_SETTING_CHRC(_uuid, _field, _desc)                                                 \
	BT_GATT_CHARACTERISTIC(&(_uuid).uuid,                                                      \
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |                            \
				       BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,        \
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_u8, write_u8,          \
			       (void *)(uintptr_t)(_field)),                                       \
		BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),                  \
		BT_GATT_CUD(_desc, BT_GATT_PERM_READ)

BT_GATT_SERVICE_DEFINE(candle_svc,
	BT_GATT_PRIMARY_SERVICE(&uuid_svc),
	CANDLE_SETTING_CHRC(uuid_power, CANDLE_FIELD_POWER, "Power: 0 off, 1 on"),
	CANDLE_SETTING_CHRC(uuid_brightness, CANDLE_FIELD_BRIGHTNESS, "Brightness 0-100 %"),
	CANDLE_SETTING_CHRC(uuid_mode, CANDLE_FIELD_MODE,
			    "Mode: 0 steady 1 calm 2 normal 3 windy 4 ember 5 storm "
			    "6 colour cycle 7 breathe"),
	CANDLE_SETTING_CHRC(uuid_speed, CANDLE_FIELD_SPEED, "Flicker speed 0-100"),
	CANDLE_SETTING_CHRC(uuid_intensity, CANDLE_FIELD_INTENSITY, "Flicker intensity 0-100"),
	BT_GATT_CHARACTERISTIC(&uuid_color.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_WRITE_WITHOUT_RESP | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_color, write_color,
			       NULL),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CUD("Core colour: R, G, B bytes", BT_GATT_PERM_READ),
	BT_GATT_CHARACTERISTIC(&uuid_batt_mv.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_batt_mv, NULL, NULL),
	BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CUD("Battery millivolts, uint16 little endian", BT_GATT_PERM_READ),
);

/* ---- Notifications -------------------------------------------------------- */

static void notify_work_handler(struct k_work *work)
{
	uint32_t fields = atomic_clear(&pending_fields);
	struct candle_config c;
	uint8_t v;

	if (!atomic_get(&connected)) {
		return;
	}
	candle_get_config(&c);

	if (fields & CANDLE_FIELD_POWER) {
		v = c.power;
		bt_gatt_notify_uuid(NULL, &uuid_power.uuid, candle_svc.attrs, &v, 1);
	}
	if (fields & CANDLE_FIELD_BRIGHTNESS) {
		v = c.brightness;
		bt_gatt_notify_uuid(NULL, &uuid_brightness.uuid, candle_svc.attrs, &v, 1);
	}
	if (fields & CANDLE_FIELD_MODE) {
		v = c.mode;
		bt_gatt_notify_uuid(NULL, &uuid_mode.uuid, candle_svc.attrs, &v, 1);
	}
	if (fields & CANDLE_FIELD_SPEED) {
		v = c.speed;
		bt_gatt_notify_uuid(NULL, &uuid_speed.uuid, candle_svc.attrs, &v, 1);
	}
	if (fields & CANDLE_FIELD_INTENSITY) {
		v = c.intensity;
		bt_gatt_notify_uuid(NULL, &uuid_intensity.uuid, candle_svc.attrs, &v, 1);
	}
	if (fields & CANDLE_FIELD_COLOR) {
		bt_gatt_notify_uuid(NULL, &uuid_color.uuid, candle_svc.attrs, c.rgb, 3);
	}
}
static K_WORK_DEFINE(notify_work, notify_work_handler);

static void on_candle_change(uint32_t fields)
{
	atomic_or(&pending_fields, fields);
	k_work_submit(&notify_work);
}

static void on_battery(uint16_t mv, uint8_t percent)
{
	uint16_t le = sys_cpu_to_le16(mv);

	bt_bas_set_battery_level(percent);
	if (atomic_get(&connected)) {
		bt_gatt_notify_uuid(NULL, &uuid_batt_mv.uuid, candle_svc.attrs, &le, sizeof(le));
	}
}

/* ---- Advertising and connections ----------------------------------------- */

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, CANDLE_UUID_VAL(1)),
};

static void adv_start_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err == -EALREADY) {
		return;
	}
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}
	LOG_INF("Advertising as \"%s\"", CONFIG_BT_DEVICE_NAME);
}
static K_WORK_DEFINE(adv_start_work, adv_start_work_handler);

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_WRN("Connection failed (err 0x%02x)", err);
		k_work_submit(&adv_start_work);
		return;
	}
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	atomic_set(&connected, 1);
	LOG_INF("Connected: %s", addr);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	atomic_set(&connected, 0);
	LOG_INF("Disconnected (reason 0x%02x)", reason);
	k_work_submit(&adv_start_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

bool candle_ble_is_connected(void)
{
	return atomic_get(&connected);
}

int candle_ble_init(void)
{
	int err = bt_enable(NULL);

	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}
	candle_set_change_callback(on_candle_change);
	battery_set_callback(on_battery);
	k_work_submit(&adv_start_work);
	return 0;
}
