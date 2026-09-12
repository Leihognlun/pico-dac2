#ifndef _AUDIO_DEVICE_H_
#define _AUDIO_DEVICE_H_

#include <stdbool.h>
#include <stdint.h>

// List of supported sample rates
#if defined(__RX__)
static const uint32_t SAMPLE_RATES[] = {44100, 48000};
#else
static const uint32_t SAMPLE_RATES[] = {44100, 48000, 88200, 96000};
#endif

#define N_SAMPLE_RATES (sizeof(SAMPLE_RATES) / sizeof(SAMPLE_RATES[0]))

// Audio device state
typedef enum {
  STATE_STOPPED,
  STATE_BUFFERING,
  STATE_PLAYING,
  STATE_STALLED
} app_state_t;

// --- Initialization ---
void audio_device_init(void);

// --- Main loop tasks ---
// Consumes the ring buffer and feeds the selected I2S or SPDIF output.
// This should be called periodically in the main loop.
void audio_device_task(void);

// --- Data flow ---
// Call this when audio data is received from the USB host.
void audio_device_on_usb_rx(const int32_t *buffer, uint32_t num_samples);

float audio_device_get_steady_buffer_fill_ratio();

bool audio_device_is_playing();

// --- Audio Stream State Control ---
void audio_device_stream_start(uint8_t bit_depth);
void audio_device_stream_stop(void);

// --- Audio Feature Control (to be called from USB control request handlers)
// ---

void audio_device_set_mute(uint8_t channel, bool muted);
bool audio_device_get_mute(uint8_t channel);

// Volume is handled as dB * 256 (UAC2 native format)
void audio_device_set_volume(uint8_t channel, int16_t volume_db_256);
int16_t audio_device_get_volume(uint8_t channel);
void audio_device_get_volume_range(uint8_t channel, int16_t *min, int16_t *max,
                                   int16_t *res);

// --- Clock Control (to be called from USB control request handlers) ---

void audio_device_set_sampling_freq(uint32_t freq);
uint32_t audio_device_get_sampling_freq(void);

bool audio_device_is_clock_valid(void);

#endif
