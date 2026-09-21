#if HID_ENABLE

#include "usb_hid.h"

#include <assert.h>
#include "hardware/gpio.h"
#include "pico/time.h"

#include "audio_device.h"
#include "audio_diagnostics.h"
#include "log.h"
#include "usb.h"
#include "usb_config.h"

static const uint8_t button_pins[] = {10, 13, 21};
static const uint8_t led_pins[] = {9, 12, 20};
static const uint8_t led_pins_n[] = {11, 14, 22};
static uint8_t raw_buttons, stable_buttons, sent_buttons, led_mask;
static uint32_t changed_at[3];
static uint8_t idle_rate[4];
static uint32_t media_sent_at;
static uint32_t led_commands;

static void sample_buttons(void) {
  // In ARC builds these physical keys control the Soundbar, not the USB host.
#if PICODAC_CEC
  stable_buttons = 0; return;
#endif
  uint32_t now = time_us_32();
  for (unsigned i = 0; i < 3; ++i) {
    uint8_t bit = 1u << i;
    bool pressed = !gpio_get(button_pins[i]);
    if (pressed != !!(raw_buttons & bit)) {
      raw_buttons ^= bit;
      changed_at[i] = now;
    }
    if ((uint32_t)(now - changed_at[i]) >= 20000)
      stable_buttons = (stable_buttons & ~bit) | (raw_buttons & bit);
  }
}

static bool set_led_report(const uint8_t *buf, uint16_t len) {
  if (!buf || len != HID_OUT_PACKET_SIZE || buf[0] != HID_REPORT_LEDS ||
      (buf[1] & 0xF8)) return false;
  led_mask = buf[1];
#if PICODAC_CEC
  led_mask &= 7u;
#endif
  for (unsigned i = 0; i < 3; ++i)
    gpio_put(led_pins[i], !!(led_mask & (1u << i)));
  ++led_commands;
  return true;
}

static void send_diagnostics(void) {
  static uint8_t page;
  uint8_t packet[HID_IN_PACKET_SIZE] = {HID_REPORT_DIAGNOSTICS};
  uint8_t *report = packet + 1;
  uint32_t silence = 0, spdif_stalls = 0, i2s_stalls = 0;
#if PICODAC_OUTPUT_SPDIF
  silence = spdif_silence_block_count;
  spdif_stalls = spdif_tx_stall_count;
  i2s_stalls = i2s_tx_stall_count;
#endif
  // 每页字段独立采样，不作为跨页原子快照；计数从芯片启动累计。
  if (page == 0)
    audio_diagnostic_report(report, page, audio_underrun_count,
                            audio_dropped_frames, silence);
  else if (page == 1)
    audio_diagnostic_report(report, page, spdif_stalls, i2s_stalls,
                            usb_audio_bad_packets);
  else if (page == 2)
    audio_diagnostic_report(report, page, audio_device_get_sampling_freq(),
                            usb_audio_rx_packets, usb_audio_rx_frames);
  else
    audio_diagnostic_report(report, page, usb_audio_queue_drops, led_mask, led_commands);
  // USB 发送函数会立即复制报告内容到 DPRAM。
  usb_ep_n_start_transfer(EP_HID_IN & 0x7F, true, packet, sizeof(packet));
  page = (page + 1) % 4;
}

static void send_next_report(void) {
  sample_buttons();
  uint32_t now = time_us_32();
  if (stable_buttons != sent_buttons ||
      (idle_rate[HID_REPORT_MEDIA] &&
       (uint32_t)(now - media_sent_at) >= 4000u * idle_rate[HID_REPORT_MEDIA])) {
    uint8_t report[] = {HID_REPORT_MEDIA, stable_buttons};
    usb_ep_n_start_transfer(EP_HID_IN & 0x7F, true, report, sizeof(report));
    sent_buttons = stable_buttons;
    media_sent_at = now;
  } else {
    send_diagnostics();
  }
}

void usb_hid_reset(void) {
  raw_buttons = stable_buttons = sent_buttons = 0;
  media_sent_at = time_us_32();
  for (unsigned i = 0; i < 3; ++i) changed_at[i] = time_us_32();
  for (unsigned i = 0; i < 4; ++i) idle_rate[i] = 0;
}

bool usb_hid_set_interface(uint8_t alt) {
  LOG_INFO("Set interface HID alt %d\r", alt);
  if (alt != 0) return false;
  usb_hid_reset();
  // Always clear the host's previous key state when configuring/resetting.
  uint8_t released[] = {HID_REPORT_MEDIA, 0};
  usb_ep_n_start_transfer(EP_HID_IN & 0x7F, true, released, sizeof(released));
  usb_ep_n_start_transfer(EP_HID_OUT, false, NULL, HID_OUT_PACKET_SIZE);
  return true;
}

bool usb_hid_control_out_request(const struct usb_setup_packet_t *pkt,
                                 const uint8_t *buf, uint16_t len) {
  if (pkt->bmRequestType != 0x21 || pkt->wIndex != INTERFACE_HID)
    return false;
  if (pkt->bRequest == 0x09 && pkt->wValue == (0x0300 | HID_REPORT_LED_FEATURE) &&
      pkt->wLength == HID_LED_FEATURE_SIZE && len == HID_LED_FEATURE_SIZE &&
      buf && buf[0] == HID_REPORT_LED_FEATURE) {
    uint8_t output[] = {HID_REPORT_LEDS, buf[1]};
    return set_led_report(output, sizeof(output));
  }
  if (pkt->bRequest == 0x09 && pkt->wValue == (0x0200 | HID_REPORT_LEDS) &&
      pkt->wLength == HID_OUT_PACKET_SIZE)
    return set_led_report(buf, len);
  if (pkt->bRequest == 0x0A && pkt->wLength == 0 && len == 0 &&
      (uint8_t)pkt->wValue <= HID_REPORT_MEDIA) {
    uint8_t id = pkt->wValue;
    if (!id) idle_rate[1] = idle_rate[2] = pkt->wValue >> 8;
    else idle_rate[id] = pkt->wValue >> 8;
    return true;
  }

  LOG_INFO(
      "unhandled OUT request(type=0x%02x, request=0x%02x, value=0x%04x, "
      "index=0x%04x, length=0x%04x)",
      pkt->bmRequestType, pkt->bRequest, pkt->wValue, pkt->wIndex,
      pkt->wLength);
  // 默认不处理
  return false;
}

static void ep_hid_out_handler(const uint8_t *buf, uint16_t len) {
  set_led_report(buf, len);
  usb_ep_n_start_transfer(EP_HID_OUT, false, NULL, HID_OUT_PACKET_SIZE);
}

static void ep_hid_in_handler() {
  // LOG_INFO("ep_hid_in_handler");
  send_next_report();
}

static bool control_in(const struct usb_setup_packet_t *pkt) {
  if (pkt->bmRequestType != 0xA1 || pkt->wIndex != INTERFACE_HID) return false;
  if (pkt->bRequest == 0x01 && pkt->wValue == (0x0300 | HID_REPORT_LED_FEATURE) &&
      pkt->wLength) {
    uint8_t report[] = {HID_REPORT_LED_FEATURE, led_mask,
                       (uint8_t)led_commands, (uint8_t)(led_commands >> 8),
                       (uint8_t)(led_commands >> 16), (uint8_t)(led_commands >> 24)};
    usb_ep0_start_transfer(report, pkt->wLength < sizeof(report) ? pkt->wLength : sizeof(report));
    return true;
  }
  uint8_t id = pkt->wValue;
  if (pkt->bRequest == 0x02 && pkt->wValue <= HID_REPORT_MEDIA && id &&
      pkt->wLength == 1) {
    usb_ep0_start_transfer(&idle_rate[id], 1);
    return true;
  }
  if (pkt->bRequest == 0x01 && pkt->wLength) {
    uint8_t report[2] = {id, 0};
    if (pkt->wValue == (0x0100 | HID_REPORT_MEDIA)) report[1] = stable_buttons;
    else if (pkt->wValue == (0x0200 | HID_REPORT_LEDS)) report[1] = led_mask;
    else return false;
    usb_ep0_start_transfer(report, pkt->wLength < 2 ? pkt->wLength : 2);
    return true;
  }
  return false;
}

void usb_hid_init() {
  for (unsigned i = 0; i < 3; ++i) {
    gpio_init(button_pins[i]);
    gpio_set_dir(button_pins[i], GPIO_IN);
    gpio_pull_up(button_pins[i]);
  }
  for (unsigned i = 0; i < 3; ++i) {
    gpio_init(led_pins[i]);
    gpio_put(led_pins[i], false);
    gpio_set_dir(led_pins[i], GPIO_OUT);
  }
  for (unsigned i = 0; i < 3; ++i) {
    gpio_init(led_pins_n[i]);
    gpio_put(led_pins_n[i], false);
    gpio_set_dir(led_pins_n[i], GPIO_OUT);
  }
  usb_device_set_control_in_handler(INTERFACE_HID, control_in);
  usb_device_set_ep_out_handler(EP_HID_OUT, ep_hid_out_handler);
  usb_device_set_ep_in_handler(EP_HID_IN & 0x7F, ep_hid_in_handler);

  usb_device_set_control_out_handler(INTERFACE_HID,
                                     usb_hid_control_out_request);

  usb_device_set_set_interface_handler(INTERFACE_HID, usb_hid_set_interface);
}

#endif
