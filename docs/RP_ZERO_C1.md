# RP-ZERO-C1 firmware configuration

Select `-DPICODAC_BOARD=c1` (the default). GPIO definitions live in `boards/c1.h`.
For the C2 dual-ARC board and five-button mapping, see [RP_ZERO_C2.md](RP_ZERO_C2.md).

The C1 configuration has one audio output only:
SPDIF on GPIO16, converted by the board circuit to HDMI ARC TX. I2S and the
dual-output build mode are not part of this target.

CEC polls the Soundbar volume. After the device has no playing output for
`BOARD_CEC_VOLUME_RESET_DELAY_MS`, it steps the volume into
`BOARD_CEC_DEFAULT_VOLUME +/- BOARD_CEC_VOLUME_TOLERANCE`. The defaults in
`boards/c1.h` are 20, 2, and 30000 ms, so reported levels 18 through 22 are
accepted without adjustment.

## GPIO assignment

| Function | GPIO |
|---|---:|
| UART0 TX / RX | 0 / 1 |
| TF SPI0 SCK / MOSI / MISO / CS | 2 / 3 / 4 / 5 |
| DDC SDA / SCL | 6 / 7 |
| SPDIF (ARC TX) | 16 |
| HDMI 5V detect / HPD (active low) / CEC | 17 / 18 / 19 |
| Green code/error LED | 25 |
| Yellow ARC status LED | 15 |
| B1 key / LED+ / LED- | 10 / 9 / 11 |
| B2 key / LED+ / LED- | 13 / 12 / 14 |
| B3 key / LED+ / LED- | 21 / 20 / 22 |
| B4 key / LED+ / LED- | 27 / 26 / 28 |

## TF audio build

Select the TF player and set the fixed boot file with CMake:

```powershell
cmake -S . -B build/zero-c1-sd -G Ninja `
  -DPICODAC_BOARD=c1 `
  -DPICODAC_INPUT=SD_AUDIO `
  -DPICODAC_SD_FILE="0:/TRACK.WAV"
cmake --build build/zero-c1-sd
```

The file type is detected from its contents, so `PICODAC_SD_FILE` may point to
a `.wav`, `.ac3`, `.ec3`, or `.eac3` file. Supported TF formats are:

- stereo PCM RIFF/WAVE: 44.1, 48, 96, or 192 kHz; 16-bit or packed 24-bit;
- raw 48 kHz AC-3, packed into IEC 61937 type `0x01` bursts;
- the existing raw 48 kHz E-AC-3 path, using a 192 kHz IEC 61937 carrier.

WAV samples are sent without resampling or bit-depth conversion. AC-3 and
E-AC-3 payloads are not decoded or re-encoded. The USB Audio input remains
available in the default `PICODAC_INPUT=USB` build.

At startup the player scans the TF root directory for up to 32 supported audio
files. Their FAT directory order is the playback/next-track order;
`PICODAC_SD_FILE` is used as a fallback when directory enumeration finds none.

## Buttons and LEDs

The yellow ARC LED on GPIO15 blinks one second on / one second off while ARC
is not enabled, and stays on when its CEC ARC link is enabled and HDMI 5V is
detected. B1 stop/play does not change this indicator. With CEC disabled,
ARC cannot be confirmed and the yellow LED continues blinking.
The green LED on GPIO25 is normally on; errors produce counted ON/OFF patterns.
TF/file errors use four ON/OFF pairs (8 states), with a configurable 2-second
gap between rounds. See [LED_STATUS.md](LED_STATUS.md) for all error codes.

- B1 toggles play/stop. Its LED is always on.
- B2 selects the next root-directory track while playing.
- B3/B4 send CEC volume-up/volume-down press and release events while playing.
- B2, B3 and B4 are ignored while stopped; their LEDs are on only while playing.

Stopping does not disable HPD, CEC, ARC, or the SPDIF transmitter. The current
track position is retained and silent SPDIF blocks are sent continuously, so
the attached Soundbar does not see the ARC link disappear or immediately enter
standby.
