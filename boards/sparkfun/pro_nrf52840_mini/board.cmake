# SPDX-License-Identifier: Apache-2.0

board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000")
board_runner_args(pyocd "--target=nrf52840" "--frequency=4000000")

# "west flash" copies zephyr.uf2 onto the mounted bootloader drive.
if(CONFIG_BUILD_OUTPUT_UF2)
  include(${ZEPHYR_BASE}/boards/common/uf2.board.cmake)
endif()
# SWD alternatives (e.g. an nRF52840 DK's Debug Out header wired to the SWD pads).
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
