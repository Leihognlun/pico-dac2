# C1 SPDIF pad 10 Hz test firmware

Standalone RP2040 firmware drives C1 `BOARD_ARC1_TX` (GPIO16) as a
push-pull 10 Hz square wave from power-up, continuously. Logic 0 is nominally
0 V and logic 1 is nominally 3.3 V at the GPIO pad, relative to board GND.
Duty cycle is 50%: high for 50 ms, low for 50 ms, period 100 ms.
Drive strength is 2 mA with fast slew rate.

This is a raw GPIO waveform for probing the board circuit, not an encoded
SPDIF audio stream. It does not initialize USB audio, TF playback, CEC, DDC,
HPD, buttons, or LEDs. Measure GPIO16 against GND; levels after the external
ARC circuit depend on that circuit.

The hardware PWM uses a nominal 125 MHz system clock, integer divider 250,
wrap 49999, and compare 25000. Absolute frequency follows the board crystal.

Build from the repository root with the installed Pico SDK/toolchain:

```powershell
cmake -S tools/c1_spdif_10hz -B build/c1-spdif-10hz -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DPICO_SDK_PATH=$env:USERPROFILE/.pico-sdk/sdk/2.3.1" `
  "-DPICO_TOOLCHAIN_PATH=$env:USERPROFILE/.pico-sdk/toolchain/15_2_Rel1" `
  "-DCMAKE_MAKE_PROGRAM=$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" `
  "-Dpicotool_DIR=$env:USERPROFILE/.pico-sdk/picotool/2.3.1/picotool"
cmake --build build/c1-spdif-10hz
```

Output: `build/c1-spdif-10hz/c1_spdif_10hz.uf2`.
Hold BOOTSEL while connecting USB, then copy the UF2 to `RPI-RP2`.
The waveform starts after reboot and repeats until power-off or reflashing.
