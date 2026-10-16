# SPDIF192.WAV: 192 kHz / 24-bit shortest-pulse test

`SPDIF192.WAV` is a 30-second stereo integer PCM WAV, sampled at 192000 Hz
with packed 24-bit samples. Every sample in both channels is `FF FF FF`,
the signed 24-bit value -1. This is a nearly silent constant signal, chosen
for its serialized bit pattern rather than its audible waveform.

The project's `src/spdif_encode.c` transmits all 24 sample bits as ones.
Biphase-mark encoding therefore requests a transition every half-cell
throughout each 24-bit audio field (48 consecutive half-cells). This gives
the maximum transition density in the audio fields, not a continuous
uniform square wave over the entire SPDIF frame.

At 192 kHz stereo, a frame contains two 32-slot subframes:

- Slot rate: 192000 * 64 = 12.288 MHz.
- Half-cell rate: 192000 * 128 = 24.576 MHz.
- Nominal shortest edge interval: 1 / 24.576 MHz = 40.6901 ns.
- Square-wave frequency during all-one data fields: 12.288 MHz
  (two edges per square-wave cycle).

Preambles and V/U/C/P fields necessarily introduce longer pulses.
An audio file cannot remove those fields or turn standards-compliant
192 kHz SPDIF into a uniform 10 MHz square wave.

Use the normal C1 TF audio firmware to play this file directly as
192 kHz / 24-bit PCM. The filename fits FAT 8.3. It can be copied to the
TF root and selected using the next-track button; naming it `TRACK.WAV`
also matches the existing configured fallback. Playback must preserve
the sample values: resampling, digital gain, mute, DSP, and bit-depth
conversion can change the wire pattern. The current C1 TF PCM path
copies decoded samples without scaling. GPIO drive strength comes from
the flashed firmware, not from the WAV file.

Current normal C1 TF firmware uses a 120 MHz system clock and a fractional
PIO divider of 625/256 at 192 kHz. Each shortest half-cell occupies 4 or 5
system-clock cycles (approximately 33.33 or 41.67 ns), averaging 40.69 ns.
The all-one data fields thus approximate a 12.288 MHz square wave with
divider jitter; edge shape and actual timing require hardware measurement.

Verification used the actual `src/wav_reader.c` to decode all 5760000
stereo frames, confirming every sample is -1, then the actual
`src/spdif_encode.c` to encode two repeated blocks. Independent decoding
confirmed all 24 data bits are ones, valid PCM V/U fields, even parity,
and correct transition requests. Across those two blocks, about 88.56%
of adjacent-edge intervals were one half-cell; the others were two or
three half-cells. This is software verification, not a board measurement.

Reference: [Texas Instruments S/PDIF coding description](https://www.ti.com/lit/ug/spru878b/spru878b.pdf).
