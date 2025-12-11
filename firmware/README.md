# Firmware related stuff

## Setup (followed once)

Install dependencies west and zephyr (nordic SDK)

```bash
brew install cmake ninja gperf python3 ccache qemu dtc wget libmagic
brew install --cask gcc-arm-embedded
```

```bash
pip install west
west init -m https://github.com/nrfconnect/sdk-nrf --mr v3.1.1
west update
west zephyr-export
```

Then in the repo folder /firmware to initialize workspace

```bash
west init -l zephyr
west update
source zephyr/zephyr-env.sh
```

## Build

Clean build:

```bash
west build -b air_ctrl --pristine
```

## Flash

Note: to use a nRF52 DK as flashing device:

- Connect the tag-connect 10-pin interface to P19 on the nRF52 DK (this somehow makes it ignore the chip on the DK)
- Then connect the DK's USB interface to your computer and the tag-connect to the custom baord, then run the flashing command
- To check that actually flashed to the custom board disconnect the tag-connect from the custom board and run the flash command again, you should get an error (something about cannot find device)

```bash
west flash
```

## Features to test to validate hardware

- [x] Power from 5V USB c to 3.3V
- [x] MCU flashing and running
- [x] BME688 sensor via i2c
- [x] BLE and antenna
- [ ] display comms via SPI (not included in current firmware iteration)
- [ ] power from 3.7 battery (need connector and battery)
- [ ] charge battery from USB c

### Display Pinout (ST7789V 240x240)

**Pin mapping:**

- P0.10 - DISP_BACKLIGHT (PWM, active high)
- P0.11 - DISP_RST (active low - idle high, pull low to reset)
- P0.12 - DISP_CS (Chip Select, active low)
- P0.13 - DISP_SCLK (SPI clock)
- P0.14 - DISP_MOSI (SPI data)
- P0.15 - DISP_DC (Data/Command select - low=command, high=data)
