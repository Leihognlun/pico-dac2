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
static uint8_t last[17];
static unsigned last_len;
static bool pins[30], outputs[30], initialized[30];
static uint32_t now;
static usb_ep_out_handler led_out;
static usb_control_interface_out_handler control_out;
static usb_control_interface_in_handler control_in;
void gpio_init(unsigned pin) { initialized[pin] = true; }
void gpio_set_dir(unsigned pin, bool out) { outputs[pin] = out; }
void gpio_pull_up(unsigned pin) { pins[pin] = true; }
bool gpio_get(unsigned pin) { return pins[pin]; }
void gpio_put(unsigned pin, bool value) { pins[pin] = value; }
uint32_t time_us_32(void) { return now; }
static usb_ep_in_handler next_report;
static usb_device_set_interfacec_handler select_interface;

uint32_t audio_device_get_sampling_freq(void) { return 96000; }
void usb_ep_n_start_transfer(uint8_t ep, bool in, const uint8_t *buf, uint16_t len) {
  assert(ep == 2);
  if (in) {
    assert(len == 17 || len == 2);
    last_len = len;
    memcpy(last, buf, len);
  } else assert(len == 2 && !buf);
}
void usb_ep0_start_transfer(const uint8_t *buf, uint16_t len) {
  assert(len <= sizeof(last)); last_len = len; memcpy(last, buf, len);
}
void usb_device_set_control_in_handler(uint8_t itf, usb_control_interface_in_handler cb) {
  assert(itf == INTERFACE_HID); control_in = cb;
}
void usb_device_set_ep_in_handler(uint8_t ep, usb_ep_in_handler cb) {
  assert(ep == 2); next_report = cb;
}
void usb_device_set_ep_out_handler(uint8_t ep, usb_ep_out_handler cb) {
  assert(ep == 2); led_out = cb;
}
void usb_device_set_control_out_handler(uint8_t itf, usb_control_interface_out_handler cb) {
  assert(itf == INTERFACE_HID); control_out = cb;
}
void usb_device_set_set_interface_handler(uint8_t itf, usb_device_set_interfacec_handler cb) {
  assert(itf == INTERFACE_HID); select_interface = cb;
}
static void check(unsigned page, uint32_t a, uint32_t b, uint32_t c) {
  assert(last_len == 17 && last[0] == HID_REPORT_DIAGNOSTICS);
  assert(last[1] == 'A' && last[2] == 'D' && last[3] == 1 && last[4] == page);
  const uint32_t expected[] = {a, b, c};
  for (unsigned i = 0; i < 3; ++i) {
    const uint8_t *p = last + 5 + 4 * i;
    uint32_t value = p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    assert(value == expected[i]);
  }
}
int main(void) {
  usb_hid_init();
  assert(pins[10] && pins[13] && pins[21]);
  assert(!outputs[10] && !outputs[13] && !outputs[21]);
  assert(outputs[9] && outputs[12] && outputs[20]);
  assert(!pins[9] && !pins[12] && !pins[20]);
  assert(select_interface(0));
  assert(last_len == 2 && last[0] == 2 && last[1] == 0);
  next_report();
  check(0, 1, 0x12345678, PICODAC_OUTPUT_SPDIF ? 3 : 0);
  next_report();
  check(1, PICODAC_OUTPUT_SPDIF ? 4 : 0, PICODAC_OUTPUT_SPDIF ? 5 : 0, 6);
  next_report(); check(2, 96000, 7, 8);
  next_report(); check(3, 9, 0, 0);
  ++audio_underrun_count;
  next_report(); check(0, 2, 0x12345678, PICODAC_OUTPUT_SPDIF ? 3 : 0);
  // Reject bouncing edges; transmit both press and release, plus simultaneous keys.
  pins[10] = false; now = 1000; next_report(); assert(last[0] == 1);
  pins[10] = true; now = 11000; next_report(); assert(last[0] == 1);
  pins[10] = false; now = 21000; next_report(); assert(last[0] == 1);
  now = 40999; next_report(); assert(last[0] == 1);
  now = 41000; next_report(); assert(last_len == 2 && last[0] == 2 && last[1] == 1);
  now += 10000; next_report(); assert(last[0] == 1); // Held key: no extra press.
  pins[10] = true; pins[13] = pins[21] = false; next_report();
  now += 20000; next_report(); assert(last[0] == 2 && last[1] == 6);
  pins[13] = pins[21] = true; next_report();
  now += 20000; next_report(); assert(last[0] == 2 && last[1] == 0);
  // Interrupt OUT and EP0 SET_REPORT use the same validated LED report.
  uint8_t leds[] = {3, 5}; led_out(leds, 2);
  assert(pins[9] && !pins[12] && pins[20]);
  leds[1] = 8; led_out(leds, 2); assert(pins[9] && pins[20]);
  leds[1] = 2;
  struct usb_setup_packet_t pkt = {0x21, 9, 0x0203, INTERFACE_HID, 2};
  assert(!control_out(&pkt, leds, 1));
  assert(control_out(&pkt, leds, 2));
  assert(!pins[9] && pins[12] && !pins[20]);
  pkt.wIndex++; assert(!control_out(&pkt, leds, 2)); pkt.wIndex--;
  pkt.bmRequestType = 0xA1; pkt.bRequest = 1;
  assert(control_in(&pkt)); assert(last_len == 2 && last[0] == 3 && last[1] == 2);
  pkt.bmRequestType = 0x21; pkt.bRequest = 0x0A; pkt.wValue = 0x0502; pkt.wLength = 0;
  assert(control_out(&pkt, NULL, 0));
  now += 20000; next_report(); assert(last[0] == 2 && last[1] == 0);
  assert(select_interface(0)); assert(last[0] == 2 && last[1] == 0);
  assert(!select_interface(1));
  last_len = 0;
  usb_hid_reset(); assert(last_len == 0); // No transfers before endpoint setup.
  // Repeated EP0 Feature commands and synchronous command-counter readback.
  const uint8_t masks[] = {4, 6, 7, 7, 0};
  for (unsigned i = 0; i < sizeof(masks); ++i) {
    uint8_t feature[] = {4, masks[i], 0, 0, 0, 0};
    pkt = (struct usb_setup_packet_t){0x21, 9, 0x0304, INTERFACE_HID, 6};
    assert(!control_out(&pkt, feature, 5));
    assert(control_out(&pkt, feature, 6));
    assert(pins[9] == !!(masks[i] & 1));
    assert(pins[12] == !!(masks[i] & 2));
    assert(pins[20] == !!(masks[i] & 4));
    assert(!pins[11] && !pins[14] && !pins[22]);
    pkt.bmRequestType = 0xA1; pkt.bRequest = 1;
    assert(control_in(&pkt));
    assert(last_len == 6 && last[0] == 4 && last[1] == masks[i]);
    assert(last[2] == 3 + i && last[3] == 0 && last[4] == 0 && last[5] == 0);
  }
  puts("PASS: media debounce/press/release, three LED GPIOs, OUT/SET_REPORT/GET_REPORT/idle");
  puts("PASS: HID diagnostic pages, live counters and little-endian transport");
}
