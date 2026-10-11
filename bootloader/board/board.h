/*
 * SparkFun Pro nRF52840 Mini (DEV-15025), Raytac MDBT50Q module.
 * Pin facts from the SparkFun schematic (nrf52840-breakout-mdbt50q v10):
 *   - blue user LED on P0.07 through 1k to GND (active high)
 *   - "13" button on P0.13 with a 10k pull-up (active low)
 *   - pin 2 (P0.02 / AIN0) is sampled at reset, as on the factory bootloader
 * These files are meant to be dropped into the Adafruit nRF52 bootloader
 * source tree and carry its license.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _SPARKFUN_NRF52840_MINI_H_
#define _SPARKFUN_NRF52840_MINI_H_

/*------------------------------------------------------------------*/
/* LED
 *------------------------------------------------------------------*/
#define LEDS_NUMBER     1
#define LED_PRIMARY_PIN PINNUM(0, 7)
#define LED_STATE_ON    1

/*------------------------------------------------------------------*/
/* BUTTON
 *------------------------------------------------------------------*/
#define BUTTON_DFU     PINNUM(0, 13)
#define BUTTON_DFU_OTA PINNUM(0, 2)
#define BUTTON_PULL    NRF_GPIO_PIN_PULLUP

//--------------------------------------------------------------------+
// BLE OTA
//--------------------------------------------------------------------+
#define BLEDIS_MANUFACTURER "SparkFun"
#define BLEDIS_MODEL        "Pro nRF52840 Mini"

//--------------------------------------------------------------------+
// USB
//--------------------------------------------------------------------+
#define USB_DESC_VID          0x1B4F
#define USB_DESC_UF2_PID      0x0029
#define USB_DESC_CDC_ONLY_PID 0x002A

//------------- UF2 -------------//
#define UF2_PRODUCT_NAME "SparkFun Pro nRF52840 Mini"
#define UF2_VOLUME_LABEL "NRF52BOOT"
#define UF2_BOARD_ID     "nRF52840-SparkFunMini-v10"

#define UF2_INDEX_URL    "https://www.sparkfun.com/products/15025"

#endif /* _SPARKFUN_NRF52840_MINI_H_ */
