# Changelog

## v0.2.1 - 2026-09-15

- Use one shared UAC2 Clock Source and Input Terminal for every AudioStreaming
  alternate setting, matching the CM6646-style topology.
- Add 192 kHz to the shared clock range for the E-AC-3 IEC 61937 carrier.
- Fix Windows `usbaudio2.sys` startup failure (Device Manager Code 10) while
  retaining Linux E-AC-3 Dolby Atmos passthrough.
- Stop the I2S state machine and hold DATA/BCLK/LRCLK low during non-PCM
  passthrough so an attached I2S DAC cannot render carrier-clock noise.
- Treat the Linux Type-I S16 device at 192 kHz as the E-AC-3 IEC 61937 carrier
  path. This keeps the working ALSA `device 0` flow on S/PDIF only instead of
  leaking the carrier into I2S.

## v0.2.0 - 2026-09-15

- Add E-AC-3 passthrough over S/PDIF for Dolby Atmos playback on compatible
  soundbars and receivers.
- Retain Dolby Digital (AC-3) and DTS passthrough support.
- Keep synchronized I2S clocks active while non-PCM audio is sent over S/PDIF.
- Add automated coverage for USB descriptors, passthrough, S/PDIF encoding,
  dual output, media controls, and audio diagnostics.

Hardware validation: E-AC-3 Dolby Atmos audio plays successfully through a
compatible soundbar.
