Language: [English](README.md) | [日本語](README.ja.md)

# Raspberry Pi Pico USB DAC

This project provides firmware to enable the Raspberry Pi Pico as a USB DAC (Digital-to-Analog Converter). It supports USB Audio Class 2.0 (UAC2), allowing it to be used as a high-quality audio output device simply by connecting it to a host such as a PC or smartphone.

## Features

- **ARC test branch (`arc-tx-eac3`):** CEC TV at GPIO18 (physical pin 24),
  SPDIF-to-external-ARC circuit at GPIO8. CEC is enabled by default and requires
  SPDIF-only output; I2S is disabled. GPIO10 toggles ARC, GPIO26/27 control Soundbar
  volume. See [CEC/ARC wiring, firmware and limitations](CEC_ARC.md).
  This addition is not yet hardware-validated; earlier playback results below
  refer to the non-CEC baseline. Use `PICODAC_CEC=OFF` for legacy output modes.

- **Optional standalone TF-card E-AC-3 player:** Build with
  `PICODAC_INPUT=SD_EAC3` to read raw 48 kHz E-AC-3 from SPI0 microSD
  (SCLK=2, MOSI=3, MISO=4, CS=5), package IEC 61937 and output a 192 kHz
  SPDIF carrier. Default file: `0:/TRACK.EC3`. USB remains the default input;
  the standalone build does not enumerate as a USB sound card.
  See [TF wiring, build and limitations (中文)](SD_EAC3.md).
- **Optional legacy synchronized I2S + SPDIF output:** SPDIF GPIO 22;
  I2S DATA=18, BCLK=16, LRCLK=17. PCM plays on both outputs; AC-3/DTS
  plays only on SPDIF while I2S sends zeros with its clocks running.
  Stereo 44.1/48/88.2/96 kHz,
  16/24-bit output; 32-bit USB samples are truncated to their upper 24 bits.
  Select `PICODAC_OUTPUT=BOTH`, `SPDIF`, or `I2S` at build time.
  See [SPDIF setup and validation (中文)](SPDIF.md).
- **Dolby Digital / DTS passthrough:** The SPDIF build exposes UAC2 Type III
  AC-3 and DTS-I/II/III formats. Alternate settings 4/5/6/7 additionally support
  a 192 kHz stereo/16-bit carrier in BOTH and SPDIF builds. All alternate
  settings share one UAC2 terminal and clock for Windows compatibility.
  PCM retains 44.1/48/88.2/96 kHz, with endpoint packet sizes limited accordingly.
  Already-packed IEC 61937 data bypasses software
  volume/mute and is transmitted with Non-PCM channel status. Requires a
  passthrough-capable player and an AC-3/DTS receiver; this firmware does not
  encode multichannel PCM. See [setup and limitations](SPDIF.md#dolby-digital--dts-透传).
- **User-verified on the current firmware (`bcdDevice=0x010a`):** Windows
  recognizes the sound card; E-AC-3 (Dolby Digital Plus) and Dolby Atmos
  passthrough work on the user's Raspberry Pi Linux playback setup.
  The USB descriptors still advertise AC-3/DTS, not a separate E-AC-3 format.
  This result does not establish Windows E-AC-3/Atmos playback, TrueHD Atmos,
  or compatibility with every player/receiver. See [test details](SPDIF.md).
- **USB Audio Class 2.0 Compliant:**
  - Works on many operating systems (Windows, macOS, Linux) without requiring driver installation.
  - Supports flow control via the Feedback Endpoint.
- **High-Resolution Audio Support:**
  - **PCM Sampling Rates:** 44.1kHz, 48kHz, 88.2kHz, 96kHz
  - **IEC 61937 Carrier Rates (altsets 4–7):** the above rates plus 192kHz
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
cmake .. -DPICODAC_CEC=OFF -DPICODAC_OUTPUT=BOTH -DPICODAC_SPDIF_PIN=22
cmake --build .
```

Upon successful compilation, a file named `mdac_adc2.uf2` will be generated in the `build` directory.

For an already-configured Windows build, the project's VS Code task uses the
Pico SDK's Ninja v1.13.2 directly:

```powershell
& "$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" -C build
```

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
   `-DPICODAC_CEC=OFF -DPICODAC_OUTPUT=I2S`. A legacy `BOTH` build can drive both devices.
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
