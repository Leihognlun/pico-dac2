# C1 SPDIF pad 10 MHz / 12 mA test firmware

Standalone RP2040 firmware drives C1 `BOARD_ARC1_TX` (GPIO16) as a
push-pull 10 MHz square wave from power-up, continuously. Drive strength
is 12 mA with fast slew rate. Logic 0 is nominally 0 V and logic 1 is
nominally 3.3 V at the GPIO pad, relative to board GND.
Duty cycle is 50%: high for 50 ns, low for 50 ns, period 100 ns.

The hardware PWM uses a nominal 120 MHz system clock, integer divider 1,
wrap 11, and compare 6. There is no fractional divider jitter.
Absolute frequency follows the board crystal; measured voltage and edge
shape depend on the connected load and board circuit.

This is a raw GPIO waveform for probing the board circuit, not encoded
SPDIF audio. It does not initialize USB audio, TF playback, CEC, DDC,
HPD, buttons, or LEDs. Measure GPIO16 relative to GND.

Build from the repository root with the installed Pico SDK/toolchain:

```powershell
cmake -S tools/c1_spdif_10mhz -B build/c1-spdif-10mhz-12ma -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  "-DPICO_SDK_PATH=$env:USERPROFILE/.pico-sdk/sdk/2.3.1" `
  "-DPICO_TOOLCHAIN_PATH=$env:USERPROFILE/.pico-sdk/toolchain/15_2_Rel1" `
  "-DCMAKE_MAKE_PROGRAM=$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" `
  "-Dpicotool_DIR=$env:USERPROFILE/.pico-sdk/picotool/2.3.1/picotool"
cmake --build build/c1-spdif-10mhz-12ma
```

Output: `build/c1-spdif-10mhz-12ma/c1_spdif_10mhz_12ma.uf2`.
Hold BOOTSEL while connecting USB, then copy the UF2 to `RPI-RP2`.
The waveform starts after reboot and repeats until power-off or reflashing.
