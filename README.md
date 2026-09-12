Language: [English](README.md) | [日本語](README.ja.md)

# Raspberry Pi Pico USB DAC

This project provides firmware to enable the Raspberry Pi Pico as a USB DAC (Digital-to-Analog Converter). It supports USB Audio Class 2.0 (UAC2), allowing it to be used as a high-quality audio output device simply by connecting it to a host such as a PC or smartphone.

## Features

- **SPDIF output (default):** GPIO 22, stereo 44.1/48/88.2/96 kHz,
  16/24-bit output; 32-bit USB samples are truncated to their upper 24 bits.
  Select `PICODAC_OUTPUT=I2S` to build the original I2S output instead.
  See [SPDIF setup and validation (中文)](SPDIF.md).
- **Dolby Digital / DTS passthrough:** The SPDIF build exposes UAC2 Type III
  AC-3 and DTS-I/II/III formats. Already-packed IEC 61937 data bypasses software
  volume/mute and is transmitted with Non-PCM channel status. Requires a
  passthrough-capable player and an AC-3/DTS receiver; this firmware does not
  encode multichannel PCM. See [setup and limitations](SPDIF.md#dolby-digital--dts-透传).
- **USB Audio Class 2.0 Compliant:**
  - Works on many operating systems (Windows, macOS, Linux) without requiring driver installation.
  - Supports flow control via the Feedback Endpoint.
- **High-Resolution Audio Support:**
  - **Sampling Rates:** 44.1kHz, 48kHz, 88.2kHz, 96kHz
  - **Bit Depths:** 16bit, 24bit, 32bit
- **HID Control:**
  - Implements a Human Interface Device (HID) endpoint for custom firmware control. (Currently, only dummy data transmission/reception is implemented. Future feature additions are planned.)
- **Custom USB Stack Implementation:**
  - Features a custom-implemented USB stack for Raspberry Pi Pico with essential functionalities.
    - The USB protocol stack is planned to be refactored into an independent library in the future.

## Required Hardware

- Raspberry Pi Pico
- A 3.3 V logic-compatible optical SPDIF transmitter or coaxial SPDIF
  driver circuit; alternatively an I2S DAC module for the I2S build.
- USB cable

## How to Build

### 1. Set Up Development Environment

You need to set up the C/C++ development environment for Raspberry Pi Pico. Please refer to the official documentation to install the Pico SDK and toolchain.

- [Getting started with Raspberry Pi Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)

### 2. Get the Source Code

```bash
git clone https://github.com/td2sk/pico-dac2
cd pico-dac2
```

### 3. Build

Build the firmware using standard CMake build procedures.

```bash
mkdir build
cd build
cmake .. -DPICODAC_OUTPUT=SPDIF -DPICODAC_SPDIF_PIN=22
cmake --build .
```

Upon successful compilation, a file named `mdac_adc2.uf2` will be generated in the `build` directory.

### Changing GPIO Pins

The GPIO pins used for I2S can be modified in `CMakeLists.txt`. The default settings are as follows:

- **I2S DATA:** GPIO 18
- **I2S BCLK:** GPIO 16
- **I2S LRCLK:** GPIO 17

```cmake
# CMakeLists.txt

# user configurations
set (PICODAC_I2S_DATA_PIN 18 CACHE STRING "I2S Data Pin")
set (PICODAC_I2S_BASE_CLOCK_PIN 16 CACHE STRING "I2S Base Clock Pin. LRCLK is BASE + 1")
```

## Installation

1. Press and hold the `BOOTSEL` button on the Raspberry Pi Pico while connecting it to your PC via a USB cable.
2. Your PC will recognize it as a mass storage device named `RPI-RP2`.
3. Drag and drop the generated `mdac_adc2.uf2` file into the `RPI-RP2` drive.
4. Once the copy is complete, the Pico will automatically reboot and start executing the firmware.

## Usage

1. Connect the SPDIF transmitter to GPIO 22 and GND as described in
   [SPDIF.md](SPDIF.md), or connect your I2S DAC when building with
   `-DPICODAC_OUTPUT=I2S`. Outputs are selected at build time, not simultaneous.
2. Connect the Pico with the flashed firmware to a host (e.g., PC) via USB.
3. The host OS will automatically recognize a new audio output device named `mdac_adc2` (or similar).
4. Select this device as the output in your OS sound settings and play music or other audio.

## HID Communication

This firmware includes an HID interface for sending and receiving custom commands. `tools/comm.py` is a simple example script that communicates with the device using the Python `hid` library.

The Vendor ID and Product ID can be configured in `CMakeLists.txt`.

- **Vendor ID:** `PICODAC_VENDOR_ID` (default: `0xcafe`)
- **Product ID:** `PICODAC_PRODUCT_ID` (default: `0xbabe`)

## TODO

- [ ] Enhance the documentation
- [ ] Custom control via HID
- [ ] Debugging/statistics acquisition via HID
- [ ] Refactor USB protocol stack into an independent library
