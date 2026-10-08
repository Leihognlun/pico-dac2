#pragma once

// RP-ZERO-C1 has a single SPDIF output; PCM remains signed/right-aligned.
typedef struct {
  unsigned data_pin, clock_pin_base;
  void *pio_instance;
  unsigned bit_depth, buffer_frames, sample_rate;
} audio_output_config_t;

#if PICODAC_OUTPUT_SPDIF
#include "spdif.h"
#include "spdif_encode.h"
#define AUDIO_OUTPUT_MAX_FRAMES SPDIF_BLOCK_FRAMES
static inline void audio_output_init(const audio_output_config_t *config,
                                     bool non_pcm) {
  spdif_init(PICODAC_SPDIF_PIN, config->sample_rate, config->bit_depth, non_pcm);
}
static inline void audio_output_deinit(const audio_output_config_t *config) {
  (void)config;
  spdif_deinit();
}
static inline void audio_output_start(const audio_output_config_t *config) {
  (void)config;
  spdif_start();
}
static inline void audio_output_stop(const audio_output_config_t *config) {
  (void)config;
  spdif_stop();
}
static inline uint32_t audio_output_get_buffer_size_frames(
    const audio_output_config_t *config) {
  (void)config;
  return SPDIF_BLOCK_FRAMES;
}
#define audio_output_unmute() ((void)0)
#define audio_output_is_buffer_ready spdif_buffer_ready
#define audio_output_get_write_buffer spdif_write_buffer
#define audio_output_submit_buffer spdif_submit_buffer
#endif
