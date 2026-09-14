#pragma once

#include <stdint.h>

extern volatile uint32_t audio_underrun_count;
extern volatile uint32_t audio_dropped_frames;
extern volatile uint32_t usb_audio_bad_packets;
extern volatile uint32_t usb_audio_rx_packets;
extern volatile uint32_t usb_audio_rx_frames;
extern volatile uint32_t usb_audio_queue_drops;
#if PICODAC_OUTPUT_SPDIF
extern volatile uint32_t spdif_silence_block_count;
extern volatile uint32_t spdif_tx_stall_count;
extern volatile uint32_t i2s_tx_stall_count;
#endif

// 16 字节 HID 输入报告：AD、版本 1、页号、三个小端 32 位计数。
static inline void audio_diagnostic_report(uint8_t *report, uint8_t page,
                                           uint32_t a, uint32_t b, uint32_t c) {
  report[0] = 'A';
  report[1] = 'D';
  report[2] = 1;
  report[3] = page;
  const uint32_t values[3] = {a, b, c};
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 4; ++j)
      report[4 + 4 * i + j] = (uint8_t)(values[i] >> (8 * j));
}
