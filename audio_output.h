#pragma once

#include "i2s.h"

// The common PCM path retains right-aligned samples for both backends.
typedef i2s_config_t audio_output_config_t;

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
#else
#define AUDIO_OUTPUT_MAX_FRAMES 96u
static inline void audio_output_init(const audio_output_config_t *config,
                                     bool non_pcm) {
  (void)non_pcm; // Type III alternate settings are absent in I2S builds.
  i2s_init(config);
}
#define audio_output_deinit i2s_deinit
#define audio_output_start i2s_start
#define audio_output_stop i2s_stop
#define audio_output_unmute i2s_unmute
#define audio_output_get_buffer_size_frames i2s_get_buffer_size_frames
#define audio_output_is_buffer_ready i2s_is_buffer_ready
#define audio_output_get_write_buffer i2s_get_write_buffer
#define audio_output_submit_buffer() ((void)0)
#endif
