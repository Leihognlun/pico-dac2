#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "audio_diagnostics.h"
#include "usb.h"
#include "usb_hid.h"
#include "usb_config.h"

volatile uint32_t audio_underrun_count = 1, audio_dropped_frames = 0x12345678;
volatile uint32_t usb_audio_bad_packets = 6, usb_audio_rx_packets = 7;
volatile uint32_t usb_audio_rx_frames = 8;
volatile uint32_t usb_audio_queue_drops = 9;
volatile uint32_t spdif_silence_block_count = 3, spdif_tx_stall_count = 4;
volatile uint32_t i2s_tx_stall_count = 5;
static uint8_t last[16];
static usb_ep_in_handler next_report;
static usb_device_set_interfacec_handler select_interface;

uint32_t audio_device_get_sampling_freq(void) { return 96000; }
void usb_ep_n_start_transfer(uint8_t ep, bool in, const uint8_t *buf, uint16_t len) {
  assert(ep == 2 && len == 16);
  if (in) memcpy(last, buf, 16);
}
void usb_device_set_ep_in_handler(uint8_t ep, usb_ep_in_handler cb) {
  assert(ep == 2); next_report = cb;
}
void usb_device_set_ep_out_handler(uint8_t ep, usb_ep_out_handler cb) {
  assert(ep == 2); (void)cb;
}
void usb_device_set_control_out_handler(uint8_t itf, usb_control_interface_out_handler cb) {
  assert(itf == INTERFACE_HID); (void)cb;
}
void usb_device_set_set_interface_handler(uint8_t itf, usb_device_set_interfacec_handler cb) {
  assert(itf == INTERFACE_HID); select_interface = cb;
}
static void check(unsigned page, uint32_t a, uint32_t b, uint32_t c) {
  assert(last[0] == 'A' && last[1] == 'D' && last[2] == 1 && last[3] == page);
  const uint32_t expected[] = {a, b, c};
  for (unsigned i = 0; i < 3; ++i) {
    const uint8_t *p = last + 4 + 4 * i;
    uint32_t value = p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    assert(value == expected[i]);
  }
}
int main(void) {
  usb_hid_init();
  assert(select_interface(0));
  check(0, 1, 0x12345678, PICODAC_OUTPUT_SPDIF ? 3 : 0);
  next_report();
  check(1, PICODAC_OUTPUT_SPDIF ? 4 : 0, PICODAC_OUTPUT_SPDIF ? 5 : 0, 6);
  next_report(); check(2, 96000, 7, 8);
  next_report(); check(3, 9, 0, 0);
  ++audio_underrun_count;
  next_report(); check(0, 2, 0x12345678, PICODAC_OUTPUT_SPDIF ? 3 : 0);
  puts("PASS: HID diagnostic pages, live counters and little-endian transport");
}
