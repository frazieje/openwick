# UF2 bootloader for the SparkFun Pro nRF52840 Mini

`sparkfun_nrf52840_mini_bootloader-0.11.0_s140_6.1.1.hex` is a build of the
Adafruit nRF52 bootloader (the same family as the board's factory bootloader)
with a board definition for the SparkFun Pro nRF52840 Mini. Adafruit's
releases do not include this board, and SparkFun does not publish a binary.

Layout of the hex (verified by parsing it):

| Region              | Address             |
|---------------------|---------------------|
| MBR                 | 0x00000 - 0x00AFF   |
| SoftDevice S140 6.1.1 | 0x01000 - 0x25DE7 |
| application start   | 0x26000             |
| bootloader          | 0xF4000 - 0xFBFFF   |
| bootloader config   | 0xFD800             |
| UICR NRFFW[0]/[1]   | 0xF4000 / 0xFE000   |

This matches `nrf52840_partition_uf2_sdv6.dtsi`, which the project's
`sparkfun_pro_nrf52840_mini/nrf52840/uf2` board target uses.

Board facts baked in (`board/board.h`): blue LED P0.07, DFU button P0.13,
pin 2 (P0.02) sampled with the button for OTA mode, USB 1B4F:0029 (UF2) and
1B4F:002A (CDC only), volume label `NRF52BOOT`.

## Rebuilding

```
git clone --recurse-submodules https://github.com/adafruit/Adafruit_nRF52_Bootloader
cd Adafruit_nRF52_Bootloader && git checkout 0.11.0
mkdir src/boards/sparkfun_nrf52840_mini
cp <this dir>/board/* src/boards/sparkfun_nrf52840_mini/
make BOARD=sparkfun_nrf52840_mini CROSS_COMPILE=arm-zephyr-eabi- PYTHON=python3 all
```

Run the `make` inside the nRF Connect SDK toolchain environment
(`nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- make ...`) so
that `arm-zephyr-eabi-gcc` and `intelhex` are on the path. The final `.zip`
step fails without `adafruit-nrfutil`; it is not needed. Outputs land in
`_build/build-sparkfun_nrf52840_mini/`.

`update-sparkfun_nrf52840_mini_bootloader-0.11.0_nosd.uf2` updates a running
bootloader of the same board ID by dropping it on the `NRF52BOOT` drive. It
cannot bring back a missing bootloader; use `tools/restore_bootloader.sh` and
SWD for that.

## Licensing

The rest of this project is Apache-2.0; this directory is not. `board/` is
MIT licensed, like the Adafruit nRF52 bootloader it is built into. The `.hex`
and `.uf2` files are binaries of that bootloader (MIT) and, in the `.hex`,
Nordic's S140 SoftDevice, redistributed in binary form under Nordic's
SoftDevice license for use on Nordic SoCs.
