#include <assert.h>
#include <stdio.h>
#include "spdif_encode.h"

// Decode the serialized transition stream independently of the lookup encoder.
static uint32_t decode(const uint32_t *words) {
  uint64_t wire = words[0] | ((uint64_t)words[1] << 32);
  uint32_t payload = 0;
  for (unsigned slot = 4; slot < 32; ++slot) {
    assert((wire >> (2 * slot)) & 1); // Every data cell starts with a transition.
    payload |= ((wire >> (2 * slot + 1)) & 1) << slot;
  }
  unsigned ones = 0;
  for (unsigned slot = 4; slot < 32; ++slot) ones += (payload >> slot) & 1;
  assert((ones & 1) == 0);
  assert((payload & 0x30000000) == 0); // Valid PCM, no user data.
  return payload;
}

static void check_block(const int32_t *pcm, unsigned depth, unsigned rate,
                        unsigned code) {
  uint32_t words[SPDIF_BLOCK_WORDS + 2];
  words[0] = words[SPDIF_BLOCK_WORDS + 1] = 0xdeadbeef;
  spdif_encode_block(words + 1, pcm, depth, rate);
  assert(words[0] == 0xdeadbeef && words[SPDIF_BLOCK_WORDS + 1] == 0xdeadbeef);
  uint8_t status[2][24] = {{0}};
  for (unsigned frame = 0; frame < 192; ++frame) {
    for (unsigned channel = 0; channel < 2; ++channel) {
      const uint32_t *sf = words + 1 + frame * 4 + channel * 2;
      // Integrate NRZI to verify actual B/M/W preamble levels, allowing
      // either polarity. Observe levels after each requested transition;
      // the PIO pipeline delays the entire stream by one half-cell.
      unsigned level = 0, levels = 0;
      for (unsigned half = 0; half < 8; ++half) {
        level ^= (*sf >> half) & 1;
        levels = (levels << 1) | level;
      }
      unsigned expected = channel ? 0xe4 : (frame ? 0xe2 : 0xe8);
      assert(levels == expected || levels == (expected ^ 255));
      uint32_t data = decode(sf);
      uint32_t sample = pcm ? (uint32_t)pcm[frame * 2 + channel] : 0;
      unsigned wanted = depth == 16 ? (sample & 0xffff) * 256 :
                        depth == 24 ? sample & 0xffffff : sample / 256;
      assert(((data >> 4) & 0xffffff) == wanted);
      status[channel][frame / 8] |= ((data >> 30) & 1) << (frame % 8);
    }
  }
  for (unsigned channel = 0; channel < 2; ++channel) {
    assert(status[channel][0] == 4);
    assert(status[channel][1] == 0 && status[channel][2] == 0);
    assert(status[channel][3] == code);
    assert(status[channel][4] == (depth == 16 ? 2 : 11));
    for (unsigned byte = 5; byte < 24; ++byte) assert(status[channel][byte] == 0);
  }
}

int main(void) {
  const unsigned rates[] = {44100, 48000, 88200, 96000};
  const unsigned codes[] = {0, 2, 8, 10};
  const unsigned depths[] = {16, 24, 32};
  int32_t samples[384];
  uint32_t random = 12345;
  spdif_encode_init();
  for (unsigned d = 0; d < 3; ++d) {
    unsigned depth = depths[d];
    for (unsigned i = 0; i < 384; ++i) {
      random = random * 1664525u + 1013904223u;
      samples[i] = (int32_t)random >> (32 - depth);
    }
    samples[0] = 0;
    samples[1] = -1;
    samples[2] = (int32_t)(1u << (depth - 1));
    samples[3] = (int32_t)((1u << (depth - 1)) - 1);
    for (unsigned r = 0; r < 4; ++r) {
      check_block(samples, depth, rates[r], codes[r]);
      check_block(NULL, depth, rates[r], codes[r]);
    }
  }
  puts("PASS: 12 rate/depth combinations, PCM and silence, preambles, parity, channel status");
  return 0;
}
