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
- Connect the GND of the DK to the GND of the custom board (maybe not needed, just precaution)
- Then connect the DK's USB interface to your computer and the tag-connect to the custom baord, then run the flashing command
- To check that actually flashed to the custom board disconnect the tag-connect from the custom board and run the flash command again, you should get an error (something about cannot find device)

```bash
west flash
```
