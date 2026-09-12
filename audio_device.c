#include "audio_device.h"

#include <stdio.h>
#include <string.h>

#include "blink.h"
#include "audio_output.h"
#include "log.h"
#include "ringbuffer.h"

//--------------------------------------------------------------------+/
// MACRO CONSTANT TYPEDEF PROTOTYPES
//--------------------------------------------------------------------+/

// --- Configuration ---
#define I2S_DATA_PIN PICODAC_I2S_DATA_PIN
#define I2S_CLOCK_PIN_BASE PICODAC_I2S_BASE_CLOCK_PIN  // LRCLK = BASE + 1
#define SAFE_WATER_LEVEL 0.5
#define UNDERRUN_WATER_LEVEL 0.16
#define RECOVERY_WATER_LEVEL 0.4
#define PIO pio0

// 256 刻みで指定
enum {
  VOLUME_CTRL_0_DB = 0,
  VOLUME_CTRL_96_DB = 96 * 256,
};

// --- Module-level Static Variables ---
static ringbuffer_t rb;
static app_state_t g_current_state;
static audio_output_config_t audio_output_config;

static uint32_t current_sample_rate = 48000;
static uint8_t current_bit_depth = 16;

static float steady_buffer_fill_ratio = 0;

// Audio controls - Current states
static int8_t mute[3] = {0, 0, 0};  // 0: unmuted, 1: muted
static int16_t volume[3] = {VOLUME_CTRL_0_DB, VOLUME_CTRL_0_DB,
                            VOLUME_CTRL_0_DB};

// 事前計算した dB のゲインを 2^31 でスケールした LUT
// -96dB(16bitオーディオのダイナミックレンジ相当)までサポートする

// 計算式: gain = round(10^(db/20) * 2^31)
// 実際のテーブルは以下の python コードで生成した
// >>> print("[")
// ... for i in range(96, -1, -1):
// ...     print(f"0x{round(math.pow(10, -i/20) * (2**31)):08x}, ", end="")
// ... print("]")

static const uint32_t gain_lookup_table[97] = {
    0x000084f3, 0x0000952c, 0x0000a760, 0x0000bbcc, 0x0000d2b6, 0x0000ec6c,
    0x00010945, 0x000129a4, 0x00014df5, 0x000176b5, 0x0001a46d, 0x0001d7ba,
    0x00021149, 0x000251de, 0x00029a55, 0x0002eba3, 0x000346dc, 0x0003ad38,
    0x00042010, 0x0004a0ec, 0x00053181, 0x0005d3bb, 0x000689bf, 0x000755fa,
    0x00083b20, 0x00093c3b, 0x000a5cb6, 0x000ba064, 0x000d0b91, 0x000ea30e,
    0x00106c43, 0x00126d43, 0x0014acdb, 0x001732ae, 0x001a074f, 0x001d345b,
    0x0020c49c, 0x0024c42c, 0x002940a2, 0x002e4939, 0x0033ef0c, 0x003a454a,
    0x00416179, 0x00495bc1, 0x00524f3b, 0x005c5a4f, 0x00679f1c, 0x007443e8,
    0x008273a6, 0x00925e89, 0x00a43aa2, 0x00b8449c, 0x00cec08a, 0x00e7facc,
    0x01044915, 0x01240b8c, 0x0147ae14, 0x016fa9bb, 0x019c8651, 0x01cedc3d,
    0x0207567a, 0x0246b4e4, 0x028dcebc, 0x02dd958a, 0x0337184e, 0x039b8719,
    0x040c3714, 0x048aa70b, 0x05188480, 0x05b7b15b, 0x066a4a53, 0x0732ae18,
    0x08138562, 0x090fcbf8, 0x0a2adad2, 0x0b68737a, 0x0ccccccd, 0x0e5ca14c,
    0x101d3f2e, 0x12149a60, 0x144960c5, 0x16c310e3, 0x198a1357, 0x1ca7d768,
    0x2026f310, 0x241346f6, 0x287a26c5, 0x2d6a866f, 0x32f52cff, 0x392ced8e,
    0x4026e73d, 0x47faccf0, 0x50c335d4, 0x5a9df7ac, 0x65ac8c2f, 0x721482c0,
    0x80000000,
};

static uint32_t calc_buffer_size(uint32_t sample_rate) {
  // sample_rate (kHz) * 2ch * 16ms * 4bytes/sample
  // Round to complete stereo frames, including at 44.1/88.2 kHz.
  return (sample_rate * 16 / 1000) * 2 * sizeof(int32_t);
}

//--------------------------------------------------------------------+/
// Initialization
//--------------------------------------------------------------------+/
void audio_device_init(void) {
  g_current_state = STATE_STOPPED;

  // --- Ring Buffer Init ---
  memset(&rb, 0, sizeof(ringbuffer_t));
  ringbuffer_init(&rb, calc_buffer_size(current_sample_rate),
                  calc_buffer_size(SAMPLE_RATES[N_SAMPLE_RATES - 1]));

  // --- Audio output configuration ---
  audio_output_config = (audio_output_config_t){
      .data_pin = I2S_DATA_PIN,
      .clock_pin_base = I2S_CLOCK_PIN_BASE,
      .pio_instance = PIO,
      .bit_depth = current_bit_depth,
      .buffer_frames = current_sample_rate / 1000,
      .sample_rate = current_sample_rate,
  };

  // Initialize the selected output backend.
  audio_output_init(&audio_output_config);
  // audio_output_start(&audio_output_config);
  blink_set_period_us(1000000);
}

//--------------------------------------------------------------------+/
// Main loop tasks
//--------------------------------------------------------------------+/
// This is the equivalent of audio_output_task() in main.c
void audio_device_task(void) {
  switch (g_current_state) {
    case STATE_STOPPED:
      // Not playing, nothing to do
      break;

    case STATE_BUFFERING:
      // Buffering, wait for buffer to be sufficiently full
      if (SAFE_WATER_LEVEL <= ringbuffer_fill_ratio(&rb)) {
        LOG_DEBUG("Buffer reached safe level. Starting audio playback....");
        audio_output_start(&audio_output_config);
        audio_output_unmute();
        g_current_state = STATE_PLAYING;
        blink_led_on();
      }
      break;

    case STATE_STALLED:
      // Stalled, wait for buffer to recover
      if (RECOVERY_WATER_LEVEL <= ringbuffer_fill_ratio(&rb)) {
        LOG_DEBUG("Buffer recovered. Resuming playback.");
        g_current_state = STATE_PLAYING;
        blink_led_on();
      } else {
        // Keep feeding silence while stalled
        if (audio_output_is_buffer_ready()) {
          int32_t *audio_output_buf = audio_output_get_write_buffer();
          const uint32_t audio_output_buf_size_frames =
              audio_output_get_buffer_size_frames(&audio_output_config);
          memset(audio_output_buf, 0, audio_output_buf_size_frames * sizeof(int32_t) * 2);
          audio_output_submit_buffer();
        }
      }
      break;

    case STATE_PLAYING:
      // Playing, keep feeding the selected audio output
      if (audio_output_is_buffer_ready()) {
        int32_t *audio_output_buf = audio_output_get_write_buffer();
        const uint32_t audio_output_buf_size_frames =
            audio_output_get_buffer_size_frames(&audio_output_config);
        const uint32_t bytes_to_read =
            audio_output_buf_size_frames * sizeof(int32_t) * 2;

        // Check for underrun
        float buffer_level = steady_buffer_fill_ratio =
            ringbuffer_fill_ratio(&rb);
        if (buffer_level <= UNDERRUN_WATER_LEVEL ||
            ringbuffer_count(&rb) < bytes_to_read) {
          // Underrun: change state to STALLED
          LOG_DEBUG("Underrun! Ratio: %.2f. Entering STALLED state.",
                    buffer_level);
          g_current_state = STATE_STALLED;
          blink_set_period_us(250000);
          // Feed silence once to avoid noise
          memset(audio_output_buf, 0, audio_output_buf_size_frames * sizeof(int32_t) * 2);
        } else {
          // Space for one complete output block (192 frames for SPDIF).
          static int32_t temp_buf[AUDIO_OUTPUT_MAX_FRAMES * 2];
          assert(bytes_to_read <= sizeof(temp_buf));
          ringbuffer_read(&rb, (uint8_t *)temp_buf, bytes_to_read);

          // Get gain values for left, right, and master channels
          int16_t left_gain_db = volume[1] / 256;
          int16_t left_gain_idx = left_gain_db + 96;
          uint32_t left_gain_scaled = gain_lookup_table[left_gain_idx];

          int16_t right_gain_db = volume[2] / 256;
          int16_t right_gain_idx = right_gain_db + 96;
          uint32_t right_gain_scaled = gain_lookup_table[right_gain_idx];

          int16_t master_gain_db = volume[0] / 256;
          int16_t master_gain_idx = master_gain_db + 96;
          uint32_t master_gain_scaled = gain_lookup_table[master_gain_idx];

          // Pre-calculate effective mute states
          bool effective_left_mute = mute[0] || mute[1];
          bool effective_right_mute = mute[0] || mute[2];

          // Apply mute by setting gain to 0 if muted
          if (effective_left_mute) {
            left_gain_scaled = 0;
          }
          if (effective_right_mute) {
            right_gain_scaled = 0;
          }

          // Apply gain
          for (uint32_t i = 0; i < audio_output_buf_size_frames; ++i) {
            int64_t left = (int64_t)temp_buf[i * 2];
            int64_t right = (int64_t)temp_buf[i * 2 + 1];

            left = (left * left_gain_scaled) >> 31;
            left = (left * master_gain_scaled) >> 31;

            right = (right * right_gain_scaled) >> 31;
            right = (right * master_gain_scaled) >> 31;

            // Both backends consume signed, right-aligned PCM.
            audio_output_buf[2 * i] = (int32_t)left;
            audio_output_buf[2 * i + 1] = (int32_t)right;
          }
        }
        audio_output_submit_buffer();
      }
      break;
  }
}

//--------------------------------------------------------------------+/
// Data flow
//--------------------------------------------------------------------+/
// This is the equivalent of the logic inside audio_task() in main.c
void audio_device_on_usb_rx(const int32_t *buffer, uint32_t num_samples) {
  uint32_t bytes = num_samples * sizeof(int32_t);
  size_t written = ringbuffer_write(&rb, (void *)buffer, bytes);
  if (written != bytes) {
    // TODO
    // リングバッファに書き込めない場合、本来は再生に追いつくために
    // リングバッファの古いデータを捨てるのが望ましい
    // ここでは暫定的にログの出力とする

    // INFO だとログ出力による遅延で正のフィードバックがかかり
    // 問題が悪化する可能性が高いため DEBUG とする
    LOG_DEBUG("bytes: %d, but written: %d", bytes, written);
  }

  // バッファレベルの測定
  // 再生中は DMA 直前に測る
  if (g_current_state != STATE_PLAYING) {
    steady_buffer_fill_ratio = ringbuffer_fill_ratio(&rb);
  }
}

float audio_device_get_steady_buffer_fill_ratio() {
  return steady_buffer_fill_ratio;
}

bool audio_device_is_playing() { return g_current_state == STATE_PLAYING; }

//--------------------------------------------------------------------+/
// Audio Stream State Control
//--------------------------------------------------------------------+/
void audio_device_stream_start(uint8_t bit_depth) {
  LOG_INFO("Starting stream with %d bits, %lu Hz", bit_depth,
           current_sample_rate);
  current_bit_depth = bit_depth;
  audio_output_deinit(&audio_output_config);
  // --- Audio output configuration ---
  audio_output_config = (audio_output_config_t){
      .data_pin = I2S_DATA_PIN,
      .clock_pin_base = I2S_CLOCK_PIN_BASE,
      .bit_depth = bit_depth,
      .pio_instance = PIO,
      .buffer_frames = current_sample_rate / 1000,
      .sample_rate = current_sample_rate,
  };
  audio_output_init(&audio_output_config);
  // audio_output_start(current_sample_rate, bit_depth, current_sample_rate / 1000);

  ringbuffer_resize(&rb, calc_buffer_size(current_sample_rate));
  // resize is a no-op at the same rate; discard samples from the old stream
  // even when only the bit depth changes or playback restarts.
  ringbuffer_clear(&rb);
  g_current_state = STATE_BUFFERING;
  blink_set_period_us(500000);
}

void audio_device_stream_stop(void) {
  LOG_DEBUG("Stopping stream");
  audio_output_stop(&audio_output_config);
  g_current_state = STATE_STOPPED;
  blink_set_period_us(1000000);
}

//--------------------------------------------------------------------+/
// Audio Feature Control
//--------------------------------------------------------------------+/

void audio_device_set_mute(uint8_t channel, bool muted) {
  (void)channel;
  LOG_DEBUG("Set channel %d Mute: %d", channel, muted);
  mute[channel] = muted;
}

bool audio_device_get_mute(uint8_t channel) {
  (void)channel;
  // LOG_DEBUG("Get channel %u mute %d", channel, mute[channel]);
  return mute[channel];
}

void audio_device_set_volume(uint8_t channel, int16_t volume_db_256) {
  (void)channel;
  LOG_DEBUG("Set channel %d volume: %d dB", channel, volume_db_256 / 256);
  volume[channel] = volume_db_256;
}

int16_t audio_device_get_volume(uint8_t channel) {
  (void)channel;
  // LOG_DEBUG("Get channel %u volume %d dB", channel, volume[channel] / 256);
  return volume[channel];
}

// This logic is from tud_audio_feature_unit_get_request() in main.c
void audio_device_get_volume_range(uint8_t channel, int16_t *min, int16_t *max,
                                   int16_t *res) {
  (void)channel;
  LOG_DEBUG("Get channel %u volume range (%d, %d, %u) dB", channel,
            -VOLUME_CTRL_96_DB / 256, VOLUME_CTRL_0_DB / 256, 256 / 256);
  *min = -VOLUME_CTRL_96_DB;
  *max = VOLUME_CTRL_0_DB;
  *res = 256;  // 1dB steps
}

//--------------------------------------------------------------------+/
// Clock Control
//--------------------------------------------------------------------+/

void audio_device_set_sampling_freq(uint32_t freq) {
  LOG_DEBUG("Clock set current freq: %ld", freq);
  bool active = g_current_state != STATE_STOPPED;
  audio_device_stream_stop();
  current_sample_rate = freq;
  if (active) audio_device_stream_start(current_bit_depth);
}

uint32_t audio_device_get_sampling_freq(void) { return current_sample_rate; }

bool audio_device_is_clock_valid(void) {
  LOG_DEBUG("Clock get is valid %u", 1);
  return true;
}
