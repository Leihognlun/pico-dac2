#include <assert.h>
#include <stdio.h>
#include "usb_audio_packet.h"

int main(void) {
  uint8_t dpram[AUDIO_MAX_PACKET_SIZE];
  usb_audio_packet_t event, queued[3];
  const unsigned lengths[] = {768, 776, 760};
  for (unsigned p = 0; p < 3; ++p) {
    memset(dpram, 0x20 + p, sizeof(dpram));
    assert(usb_audio_packet_capture(&event, dpram, lengths[p], 10));
    // pico_queue 按值复制；生产者和 DPRAM 随后均可被覆盖。
    queued[p] = event;
  }
  memset(dpram, 0xff, sizeof(dpram));
  memset(&event, 0, sizeof(event));
  for (unsigned p = 0; p < 3; ++p) {
    assert(queued[p].length == lengths[p]);
    const uint8_t *bytes = (const void *)queued[p].data;
    for (unsigned i = 0; i < lengths[p]; ++i) assert(bytes[i] == 0x20 + p);
    assert(usb_audio_packet_current(&queued[p], true, 10));
    assert(!usb_audio_packet_current(&queued[p], false, 10));
    assert(!usb_audio_packet_current(&queued[p], true, 11));
  }
  assert(!usb_audio_packet_capture(&event, dpram, sizeof(dpram) + 1, 10));
  assert(usb_audio_packet_capture(&event, dpram, 0, 11));
  assert(usb_audio_packet_current(&event, true, 11));
  puts("PASS: audio packet snapshots survive DPRAM reuse; stale streams rejected");
}
