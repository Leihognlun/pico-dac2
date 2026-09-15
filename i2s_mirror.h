#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline bool i2s_mirror_enabled(bool non_pcm) { return !non_pcm; }

// I2S always has 32-bit slots. Match the effective samples carried by SPDIF:
// 16/24-bit PCM is left aligned; 32-bit USB PCM loses its bottom eight bits.
static inline uint32_t i2s_mirror_sample(int32_t sample, unsigned depth,
                                        bool non_pcm) {
  if (!i2s_mirror_enabled(non_pcm)) return 0;
  uint32_t value = (uint32_t)sample;
  if (depth == 16) return value << 16;
  if (depth == 24) return value << 8;
  return value & 0xffffff00u;
}
