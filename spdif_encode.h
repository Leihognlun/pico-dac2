#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SPDIF_BLOCK_FRAMES 192u
#define SPDIF_BLOCK_WORDS (SPDIF_BLOCK_FRAMES * 4u)

// Input: interleaved signed PCM, or untouched 16-bit IEC 61937 words,
// right aligned in int32_t containers. NULL generates zero data.
// Output: LSB-first NRZI transition bits for the pico-extras PIO decoder.
void spdif_encode_init(void);
void spdif_encode_block(uint32_t *out, const int32_t *pcm,
                        uint8_t bit_depth, uint32_t sample_rate, bool non_pcm);
