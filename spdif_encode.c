#include "spdif_encode.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

static uint16_t bmc[256];

void spdif_encode_init(void) {
  for (unsigned value = 0; value < 256; ++value) {
    uint16_t encoded = 0x5555;
    for (unsigned bit = 0; bit < 8; ++bit) {
      encoded |= ((value >> bit) & 1u) << (2 * bit + 1);
    }
    bmc[value] = encoded;
  }
}

static uint8_t frequency_code(uint32_t rate) {
  switch (rate) {
    case 44100: return 0x0;
    case 48000: return 0x2;
    case 88200: return 0x8;
    case 96000: return 0xa;
    default: assert(false); return 0x1; // Frequency not indicated.
  }
}

void spdif_encode_block(uint32_t *out, const int32_t *pcm,
                        uint8_t bit_depth, uint32_t sample_rate, bool non_pcm) {
  assert(bit_depth == 16 || bit_depth == 24 || bit_depth == 32);
  assert(!non_pcm || bit_depth == 16);
  // Consumer, copying permitted, general category. Channel-status bit 1
  // identifies IEC 61937 compressed data (never interpret it as PCM).
  // Byte 3: sample frequency; byte 4: 16 or 24 significant bits.
  const uint8_t status[24] = {
      non_pcm ? 0x06 : 0x04, 0x00, 0x00, frequency_code(sample_rate),
      bit_depth == 16 ? 0x02 : 0x0b};
  for (unsigned frame = 0; frame < SPDIF_BLOCK_FRAMES; ++frame) {
    unsigned c = (status[frame / 8] >> (frame % 8)) & 1u;
    for (unsigned channel = 0; channel < 2; ++channel) {
      uint32_t sample = pcm ? (uint32_t)*pcm++ : 0;
      if (bit_depth == 16) sample <<= 8;
      if (bit_depth == 32) sample >>= 8;
      // IEC 60958 slots 4..27: audio/data, then V, U=0, C, P.
      // V=1 in non-PCM mode: these words are unsuitable for PCM D/A.
      uint32_t payload = ((sample & 0xffffffu) << 4) |
                         ((uint32_t)non_pcm << 28) | (c << 30);
      uint32_t parity = payload;
      parity ^= parity >> 16;
      parity ^= parity >> 8;
      parity ^= parity >> 4;
      parity ^= parity >> 2;
      parity ^= parity >> 1;
      payload |= (parity & 1u) << 31;
      // Same NRZI preambles as pico-extras: Z starts a 192-frame block,
      // X starts subsequent left subframes, Y starts every right subframe.
      uint32_t preamble = channel ? 0x69 : (frame ? 0xc9 : 0x39);
      *out++ = preamble | ((uint32_t)bmc[(payload >> 4) & 0xff] << 8) |
               ((uint32_t)bmc[(payload >> 12) & 0x0f] << 24);
      *out++ = bmc[(payload >> 16) & 0xff] |
               ((uint32_t)bmc[payload >> 24] << 16);
    }
  }
}
