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

Then in the repo folder /firmware:

```bash
west init
west zephyr-export
```

## Build

```bash
west build -b air_ctrl --pristine
```

## Flash

```bash
west flash
```
