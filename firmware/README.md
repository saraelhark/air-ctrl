# Firmware related stuff

## Setup (reproducible)

Install dependencies:

```bash
brew install cmake ninja gperf python3 ccache qemu dtc wget libmagic
brew install --cask gcc-arm-embedded
pip install west
```

Initialize the workspace from the local manifest and fetch pinned dependencies:

```bash
cd firmware
west init -l .
west update
```

Optional (useful for IDE integration and direct CMake builds):

```bash
west zephyr-export
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

### Connect to RTT

Note: this requires the SEGGER JLink RTT Logger.

```bash
/Applications/SEGGER/JLink/JLinkRTTLoggerExe \
  -Device NRF52 \
  -If SWD \
  -Speed 4000 \
  -RTTChannel 0 \
  -LogFile \
  -TimeStamp \
  -Append
```

### Display Pinout (ST7789V 240x240)

**Pin mapping:**

- P0.10 - DISP_BACKLIGHT (PWM, active high)
- P0.11 - DISP_RST (active low - idle high, pull low to reset)
- P0.12 - DISP_CS (Chip Select, active low)
- P0.13 - DISP_SCLK (SPI clock)
- P0.14 - DISP_MOSI (SPI data)
- P0.15 - DISP_DC (Data/Command select - low=command, high=data)

## Gas Sensor config

The firmware uses BSEC (Bosch Sensortec Environmental Cluster) for gas sensing.
The raw sensor values are polled using the Zephyr driver for the BME680 (compatible with BME688) since the Bosch driver did not work (humidity and pressure compensation gave physical readings out of bounds).

The firmware can also be built without the BSEC library and just print the raw sensor values.

This is the setup:

- BME688 sensor connected via I2C
- BSEC algorithm for IAQ (Indoor Air Quality) calculations
- Forced mode: periodic sampling 3s and bsec library estimates Indoor Air Quality, CO2 and VOC levels

### How BSEC sampling + calibration works (step-by-step)

1) BSEC initialization

   - `bsec_init()` creates the BSEC instance.
   - `bsec_set_configuration()` loads the Bosch-provided config blob (defines the IAQ model and tuning).

2) Restore previous calibration (optional but recommended)

   - On boot, the firmware attempts to load a previously saved BSEC state blob from flash (Zephyr `settings`/NVS).
   - If found, it calls `bsec_set_state()` to restore calibration/baseline so IAQ accuracy can recover faster.

3) Subscribe to virtual sensors

   - `bsec_update_subscription()` is used to request which virtual outputs you want (IAQ, CO2 equivalent, VOC equivalent, raw values, run-in/stabilization, etc.) and at what sample rate (LP in this firmware).
   - BSEC responds with which physical sensor signals it requires.

4) Ask BSEC what to do next

   - Each loop iteration calls `bsec_sensor_control(timestamp_ns, &bme_settings)`.
   - BSEC returns *when* the next sample should happen and *which* physical inputs must be provided for that timestamp.

5) Trigger a measurement and read raw sensor signals

   - When `bme_settings.trigger_measurement` is set, the firmware triggers a forced-mode measurement on the BME688 via the Zephyr driver.
   - The firmware reads:
     - temperature (°C)
     - humidity (%RH)
     - pressure (Pa)
     - gas resistance (Ohm)

6) Feed raw signals into BSEC

   - The firmware builds a `bsec_input_t[]` array (only for the signals BSEC requested in `bme_settings.process_data`).
   - It passes these inputs to `bsec_do_steps()`.

7) Consume BSEC outputs

   - `bsec_do_steps()` returns a list of virtual sensor outputs (IAQ, CO2 equivalent, VOC equivalent, heat-compensated temperature/humidity, etc.).
   - Each output has an `accuracy` value:
     - 0: stabilizing
     - 1: low
     - 2: medium
     - 3: high

8) Ongoing calibration (run-in + stabilization)

   - Early after boot, BSEC is building a baseline; outputs can be noisy and the CO2/VOC equivalents may temporarily saturate.
   - The firmware also prints BSEC run-in/stabilization status so you can see whether the algorithm considers the baseline “ready”.

9) Periodically save BSEC state

   - While running, the firmware periodically calls `bsec_get_state()` and saves the returned blob to flash.
   - Restoring this blob on the next boot helps preserve long-term calibration progress.

example output:

```log
<inf> app: === BME688 BSEC Data ===
<inf> app:   Temperature: 22.29 °C (raw: 22.40 °C)
<inf> app:   Humidity: 38.01 %RH (raw: 37.77 %RH)
<inf> app:   Pressure: 1013.27 hPa
<inf> app:   Gas Resistance: 12858838 Ohm
<inf> app:   IAQ: 92.1
<inf> app:   IAQ Accuracy: Low (1)
<inf> app:   Static IAQ: 68.4
<inf> app:   CO2 Equivalent: 684 ppm
<inf> app:   Breath VOC: 0.759 ppm
<inf> app:   Gas Percentage: 28.0 %
<inf> app:   Stabilization: Done, Run-in: Done
<dbg> bsec: process_data: BSEC inputs: n=5 T=22.40 H=37.77 P=101329 Gas=12858838
```

### Notes

- using the zephyr driver makes things work but it hardcodes the heating profile for the sensor
- the sensor library (bsec) calibrates over time, so it needs to be left running for a while to stabilize
- the sensor library (bsec) has support to store the configuration to flash to be able to load it at the next boot and not lose the stabilization/calibration efforts

## Important

The BSEC library is closed source and you need to register and accept the agreement to download it from Bosch Sensortec's website [version 3.2.1.0](https://www.bosch-sensortec.com/software-tools/double-opt-in-forms/bsec-software-3-2-1-0-form.html).

By default, the firmware builds without BSEC and prints raw sensor values.

`ext/` is ignored and not fetched by west.

To enable BSEC:

1) Download the archive and extract it in a local folder called `ext/`.
2) Build with the BSEC overlay config:

```bash
west build -b air_ctrl --pristine -- -DOVERLAY_CONFIG=prj_bsec.conf
```

Example expected library path:
`/ext/algo/bsec_IAQ_Sel/bin/gcc/Cortex_M4F/libalgobsec.a`
