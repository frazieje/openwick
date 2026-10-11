/*
 * Bluetooth LE: advertising, connections and the Candle GATT service.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CANDLE_BLE_H_
#define CANDLE_BLE_H_

#include <stdbool.h>

/* Enables Bluetooth and starts advertising. */
int candle_ble_init(void);

bool candle_ble_is_connected(void);

#endif /* CANDLE_BLE_H_ */
