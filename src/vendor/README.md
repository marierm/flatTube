# Vendored: rpi_ws281x driver

These files are the Raspberry Pi PWM/DMA driver for WS281x/SK6812 LED
strips (originally by Jeremy Garff, BSD-licensed), copied here unmodified
from `../../fpTube/fpTubeC`. They talk to the BCM283x DMA/PWM/PCM
peripherals directly and only build on a Raspberry Pi.

Not touched by this project other than being copied in — application code
lives in `src/` (the parent directory).
