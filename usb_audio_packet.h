#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "usb_config.h"

// 队列拥有数据副本，USB DPRAM 可立即用于下一包。
typedef struct {
  uint32_t generation;
  uint16_t length;
  uint32_t data[(AUDIO_MAX_PACKET_SIZE + 3) / 4];
} usb_audio_packet_t;

static inline bool usb_audio_packet_capture(usb_audio_packet_t *packet,
    const void *data, uint16_t length, uint32_t generation) {
  if (length > sizeof(packet->data)) return false;
  packet->generation = generation;
  packet->length = length;
  memcpy(packet->data, data, length);
  return true;
}

static inline bool usb_audio_packet_current(const usb_audio_packet_t *packet,
    bool enabled, uint32_t generation) {
  return enabled && packet->generation == generation;
}
